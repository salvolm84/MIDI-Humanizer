// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "Humanizer.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace mh
{

namespace
{
    double msToPpq (double ms, double bpm) noexcept  { return ms * bpm / 60000.0; }

    /** The seed a lane draws from. Mixing the lane's offset in lets one drum be re-rolled
        on its own; an offset of zero leaves the lane exactly on the global seed, so this
        cannot disturb existing results.
    */
    int seedForLane (int globalSeed, const LaneSettings& lane) noexcept
    {
        return globalSeed ^ (lane.seedOffset * 0x9e37);
    }

    /** Indices of the notes on each drum lane, in the order they appear in `notes`.
        Assumes `notes` is already sorted by time.
    */
    std::map<int, std::vector<size_t>> groupByLane (const std::vector<Note>& notes)
    {
        std::map<int, std::vector<size_t>> byLane;

        for (size_t i = 0; i < notes.size(); ++i)
            byLane[notes[i].noteNumber].push_back (i);

        return byLane;
    }

    /** Average and quietest velocity of each lane in the source clip.

        Retargeting a lane's level needs to know where that lane currently sits, and it
        has to be measured on the source rather than mid-transform, otherwise accents and
        spread would feed back into the average they are being measured against.
    */
    struct LaneMeans
    {
        std::array<double, LaneSettingsTable::numLanes> mean {};
        std::array<double, LaneSettingsTable::numLanes> lowest {};
        std::array<int, LaneSettingsTable::numLanes> count {};

        explicit LaneMeans (const std::vector<Note>& notes)
        {
            for (auto& n : notes)
            {
                const auto lane = (size_t) juce::jlimit (0, LaneSettingsTable::numLanes - 1, n.noteNumber);
                mean[lane] += (double) n.velocity;
                lowest[lane] = count[lane] == 0 ? (double) n.velocity
                                                : juce::jmin (lowest[lane], (double) n.velocity);
                ++count[lane];
            }

            for (size_t lane = 0; lane < mean.size(); ++lane)
                if (count[lane] > 0)
                    mean[lane] /= (double) count[lane];
        }

        double meanFor (int noteNumber, double fallback) const
        {
            const auto lane = (size_t) juce::jlimit (0, LaneSettingsTable::numLanes - 1, noteNumber);
            return count[lane] > 0 ? mean[lane] : fallback;
        }

        double lowestFor (int noteNumber, double fallback) const
        {
            const auto lane = (size_t) juce::jlimit (0, LaneSettingsTable::numLanes - 1, noteNumber);
            return count[lane] > 0 ? lowest[lane] : fallback;
        }
    };
}

double Humanizer::gridWeight (double ppq, double barLengthPpq)
{
    const auto sitsOn = [ppq] (double step)
    {
        if (step <= 0.0)
            return false;

        const auto ratio = ppq / step;
        return std::abs (ratio - std::round (ratio)) < 1.0e-4;
    };

    if (sitsOn (barLengthPpq)) return 1.00;   // bar line
    if (sitsOn (1.0))          return 0.78;   // beat
    if (sitsOn (0.5))          return 0.55;   // eighth
    if (sitsOn (0.25))         return 0.34;   // sixteenth
    if (sitsOn (0.125))        return 0.22;   // thirty-second
    return 0.12;                              // off the grid entirely
}

NoteSequence Humanizer::process (const NoteSequence& source,
                                 const HumanizeParams& p,
                                 HumanizeReport* reportOut)
{
    HumanizeReport report;
    auto out = source;

    if (! p.enabled || out.notes.empty())
    {
        if (reportOut != nullptr)
            *reportOut = report;

        return out;
    }

    const auto bpm     = juce::jlimit (20.0, 999.0, p.referenceBpm);
    const auto bar     = out.barLengthPpq();
    const auto loopLen = juce::jmax (0.25, out.lengthPpq);
    const LaneMeans means (source.notes);

    out.sortByTime();

    // ---- 1. tighten -------------------------------------------------------------
    // Pull toward the grid before adding any deviation, so "tight but human" is
    // reachable from a sloppily recorded source clip.
    if (p.tightenPercent > 0.01f)
    {
        const auto step   = gridDivision (p.tightenDivision).ppq;
        const auto amount = juce::jlimit (0.0, 1.0, (double) p.tightenPercent / 100.0);

        for (auto& n : out.notes)
        {
            if (! p.forNote (n.noteNumber).enabled)
                continue;

            const auto target = std::round (n.startPpq / step) * step;
            n.startPpq += (target - n.startPpq) * amount;
        }
    }

    // ---- 2. velocity level: retarget, then accents -------------------------------
    // Done here, while positions still sit on the grid, so a note's accent reflects
    // where it was *written* rather than where swing and spread later push it.
    for (auto& n : out.notes)
    {
        const auto& lane = p.forNote (n.noteNumber);

        if (! lane.enabled)
            continue;

        auto velocity = (double) n.velocity;

        if (lane.velocityTarget > 0)
        {
            // Move the lane's average onto the target, keeping as much of the written
            // dynamics as Dynamics asks for. At 0 every hit lands on the target; at 100
            // the accenting is untouched and merely re-centred. With a one-sided spread
            // the target is the lane's floor instead, so it anchors the quietest hit.
            const auto anchor = lane.velocitySpreadUpOnly ? means.lowestFor (n.noteNumber, velocity)
                                                          : means.meanFor (n.noteNumber, velocity);
            const auto keep = juce::jlimit (0.0, 1.0, (double) lane.velocityDynamics / 100.0);
            velocity = (double) lane.velocityTarget + keep * (velocity - anchor);
        }

        if (lane.accentAmount > 0.01f)
        {
            const auto weight = gridWeight (n.startPpq, bar);
            velocity += ((double) lane.accentAmount / 100.0) * (weight - 0.45) * 44.0;
        }

        n.velocity = (int) std::lround (velocity);
    }

    // ---- 3. swing ---------------------------------------------------------------
    {
        const auto step      = gridDivision (p.swingDivision).ppq;
        const auto maxDelay  = step / 3.0;   // 100% lands the off-beat on a triplet
        const auto tolerance = step * 0.25;

        for (auto& n : out.notes)
        {
            const auto& lane = p.forNote (n.noteNumber);

            if (! lane.enabled || std::abs (lane.swingPercent) < 0.01f)
                continue;

            const auto slot = std::llround (n.startPpq / step);

            if ((slot % 2) == 0)
                continue;   // only the off-steps of the pair move

            // Notes that are not really on this grid (triplet fills, drags) are left
            // alone rather than dragged onto a swing they were never part of.
            if (std::abs (n.startPpq - (double) slot * step) > tolerance)
                continue;

            n.startPpq += maxDelay * juce::jlimit (-1.0, 1.0, (double) lane.swingPercent / 100.0);
        }
    }

    // ---- 4. timing bias (where the lane sits on average) -------------------------
    for (auto& n : out.notes)
    {
        const auto& lane = p.forNote (n.noteNumber);

        if (lane.enabled && std::abs (lane.timingBiasMs) > 0.001f)
            n.startPpq += msToPpq (lane.timingBiasMs, bpm);
    }

    // ---- 5. snare ghost notes ---------------------------------------------------
    if (p.ghostAmount > 0.01f)
    {
        // Ghost on whichever snare-family lane the clip actually uses most.
        std::map<int, int> snareHits;

        for (auto& n : out.notes)
            if (familyOf (n.noteNumber) == DrumFamily::snare)
                ++snareHits[n.noteNumber];

        if (! snareHits.empty())
        {
            int lane = -1, bestCount = -1, laneChannel = 10;

            for (auto& [note, count] : snareHits)
                if (count > bestCount)
                    std::tie (bestCount, lane) = std::tie (count, note);

            // A lane the user has switched off should not sprout generated notes.
            if (p.forNote (lane).enabled)
            {
                std::vector<double> existing;

                for (auto& n : out.notes)
                    if (n.noteNumber == lane)
                    {
                        existing.push_back (n.startPpq);
                        laneChannel = n.channel;
                    }

                const auto step        = 0.25;   // ghosts live on sixteenths
                const auto probability = juce::jlimit (0.0, 1.0, (double) p.ghostAmount / 100.0) * 0.45;
                const auto numSlots    = (int) std::floor (loopLen / step + 1.0e-6);

                const auto anyWithin = [&existing] (double pos, double window)
                {
                    return std::any_of (existing.begin(), existing.end(),
                                        [pos, window] (double e) { return std::abs (e - pos) <= window; });
                };

                for (int slot = 0; slot < numSlots; ++slot)
                {
                    if ((slot % 4) == 0)
                        continue;   // a ghost is a filler between hits, never on the beat

                    const auto pos = (double) slot * step;

                    if (anyWithin (pos, step * 0.5))
                        continue;   // a real hit already owns this slot

                    if (! anyWithin (pos, 1.0))
                        continue;   // ghosts cluster around the backbeat, not in empty bars

                    const auto id = ghostIdBase + slot;

                    if (det::uniform (p.seed, id, det::ghostGateStream) > probability)
                        continue;

                    Note ghost;
                    ghost.startPpq   = pos;
                    ghost.lengthPpq  = 0.1;
                    ghost.noteNumber = lane;
                    ghost.channel    = laneChannel;
                    ghost.sourceId   = id;
                    ghost.isGhost    = true;
                    ghost.velocity   = (int) std::lround ((double) p.ghostVelocity
                                                          + 7.0 * det::bipolar (p.seed, id, det::ghostVelStream));

                    out.notes.push_back (ghost);
                    ++report.ghostsAdded;
                }

                out.sortByTime();
            }
        }
    }

    // ---- 6. hand alternation and round-robin variation --------------------------
    if (p.handAlternation > 0.01f || p.variationAmount > 0.01f)
    {
        const auto runThreshold = 0.5 * 1.02;   // hits within an eighth count as one run
        const auto handAmount   = juce::jlimit (0.0, 1.0, (double) p.handAlternation / 100.0);
        const auto varyAmount   = juce::jlimit (0.0, 1.0, (double) p.variationAmount / 100.0);

        for (auto& [lane, indices] : groupByLane (out.notes))
        {
            if (! p.forNote (lane).enabled)
                continue;

            int positionInRun = 0;

            for (size_t k = 0; k < indices.size(); ++k)
            {
                auto& n = out.notes[indices[k]];

                const auto continuesRun = k > 0
                    && (n.startPpq - out.notes[indices[k - 1]].startPpq) <= runThreshold;

                positionInRun = continuesRun ? positionInRun + 1 : 0;

                // Fast runs alternate hands, and the weaker hand lands a touch late
                // and a touch softer. This is what stops a 16th hat line sounding
                // like a machine gun.
                if (handAmount > 0.0 && continuesRun && (positionInRun % 2) != 0)
                {
                    n.velocity -= (int) std::lround (handAmount * 16.0);
                    n.startPpq += msToPpq (handAmount * 2.5, bpm);
                }

                if (varyAmount > 0.0 && k > 0)
                {
                    const auto r = det::bipolar (p.seed, n.sourceId, det::variationStream);
                    n.velocity += (int) std::lround (varyAmount * 9.0 * r);
                }
            }
        }
    }

    // ---- 7. spread (how far each hit wanders from the lane's bias) ---------------
    for (auto& n : out.notes)
    {
        const auto& lane = p.forNote (n.noteNumber);

        if (! lane.enabled)
            continue;

        if (lane.timingSpreadMs > 0.0f)
        {
            const auto deviationMs = (double) lane.timingSpreadMs
                                     * det::bipolar (seedForLane (p.seed, lane), n.sourceId,
                                                     det::timingStream);
            n.startPpq += msToPpq (deviationMs, bpm);
            report.peakTimingShiftMs = juce::jmax (report.peakTimingShiftMs, std::abs (deviationMs));
        }

        if (lane.velocitySpread > 0.0f)
        {
            // Folding the same random field keeps a one-sided spread on the same
            // performance, still clustered near the level, just never below it.
            auto r = det::bipolar (seedForLane (p.seed, lane), n.sourceId, det::velocityStream);

            if (lane.velocitySpreadUpOnly)
                r = std::abs (r);

            n.velocity += (int) std::lround ((double) lane.velocitySpread * r);
        }
    }

    // ---- 8. flams ---------------------------------------------------------------
    // A doubled hit on one drum is physically a flam, so one stick leads, quieter.
    if (p.flamAmount > 0.01f && p.flamSpreadMs > 0.0f)
    {
        out.sortByTime();

        const auto spread     = msToPpq ((double) p.flamSpreadMs * (double) p.flamAmount / 100.0, bpm);
        const auto coincident = msToPpq (4.0, bpm);

        for (size_t i = 0; i + 1 < out.notes.size(); ++i)
        {
            if (! p.forNote (out.notes[i].noteNumber).enabled)
                continue;

            for (size_t j = i + 1; j < out.notes.size(); ++j)
            {
                if (out.notes[j].startPpq - out.notes[i].startPpq > coincident)
                    break;

                if (out.notes[j].noteNumber != out.notes[i].noteNumber)
                    continue;

                const auto quieter = out.notes[i].velocity <= out.notes[j].velocity ? i : j;
                const auto louder  = quieter == i ? j : i;

                out.notes[quieter].startPpq = out.notes[louder].startPpq - spread;
                out.notes[quieter].velocity = (int) std::lround (out.notes[louder].velocity * 0.62);
                out.notes[quieter].isFlam   = true;
                out.notes[louder].isFlam    = true;
                ++report.flamsCreated;
                break;
            }
        }
    }

    // ---- 9. wrap into the loop --------------------------------------------------
    // The clip loops, so a note nudged before bar 1 belongs at the tail of the previous
    // pass. Clamping to zero instead would pile every early note up on the downbeat.
    for (auto& n : out.notes)
    {
        n.startPpq = std::fmod (n.startPpq, loopLen);

        if (n.startPpq < 0.0)
            n.startPpq += loopLen;
    }

    out.sortByTime();

    // ---- 10. playability limits -------------------------------------------------
    // These describe what a kit can physically do, so they apply even to lanes the user
    // has excluded from humanization.
    if (p.limitsEnabled)
    {
        std::vector<bool> dropped (out.notes.size(), false);

        for (auto& [lane, indices] : groupByLane (out.notes))
        {
            const auto minGap = msToPpq ((double) p.forNote (lane).minGapMs, bpm);

            if (minGap <= 0.0)
                continue;

            size_t previous = 0;
            bool havePrevious = false;

            for (auto i : indices)
            {
                // Flams are a deliberate close pair, so they are exempt.
                if (out.notes[i].isFlam)
                {
                    havePrevious = false;
                    continue;
                }

                if (! havePrevious)
                {
                    previous = i;
                    havePrevious = true;
                    continue;
                }

                if (out.notes[i].startPpq - out.notes[previous].startPpq >= minGap)
                {
                    previous = i;
                    continue;
                }

                // One drum cannot be struck twice this fast: keep the louder hit.
                if (out.notes[i].velocity > out.notes[previous].velocity)
                {
                    dropped[previous] = true;
                    previous = i;
                }
                else
                {
                    dropped[i] = true;
                }

                ++report.notesDroppedByGap;
            }
        }

        if (report.notesDroppedByGap > 0)
        {
            std::vector<Note> kept;
            kept.reserve (out.notes.size());

            for (size_t i = 0; i < out.notes.size(); ++i)
                if (! dropped[i])
                    kept.push_back (out.notes[i]);

            out.notes = std::move (kept);
        }

        if (p.chokeEnabled)
        {
            for (size_t i = 0; i < out.notes.size(); ++i)
            {
                auto& n = out.notes[i];
                const auto chokers = chokedBy (n.noteNumber);

                if (chokers.isEmpty())
                    continue;

                for (size_t j = i + 1; j < out.notes.size(); ++j)
                {
                    const auto& other = out.notes[j];

                    if (! chokers.contains (other.noteNumber))
                        continue;

                    if (other.startPpq > n.startPpq && other.startPpq < n.endPpq())
                    {
                        n.lengthPpq = juce::jmax (0.02, other.startPpq - n.startPpq);
                        ++report.tailsChoked;
                    }

                    break;
                }
            }
        }
    }

    // ---- 11. final clamps, routing and audibility ------------------------------
    for (auto& n : out.notes)
    {
        n.velocity   = juce::jlimit (1, 127, n.velocity);
        n.channel    = juce::jlimit (1, 16, n.channel);
        n.lengthPpq  = juce::jmax (0.02, n.lengthPpq);

        const auto& lane = p.forNote (n.noteNumber);

        // A drum map's output note is applied here, leaving noteNumber as the lane the
        // note is drawn on, so remapping never disturbs the grid.
        n.outputNote = juce::jlimit (0, 127, p.outputNote[(size_t) juce::jlimit (0, 127, n.noteNumber)]);

        // Soloing anywhere silences everything that is not soloed.
        n.audible = ! lane.muted && (! p.anyLaneSoloed || lane.soloed);
    }

    // ---- 12. per-lane measurements ----------------------------------------------
    // Measured on the finished clip, so what the UI reports is what will be played.
    {
        std::map<int, const Note*> sourceById;

        for (auto& n : source.notes)
            sourceById[n.sourceId] = &n;

        std::array<double, LaneSettingsTable::numLanes> shiftTotal {};
        std::array<double, LaneSettingsTable::numLanes> velocityTotal {};

        for (auto& n : out.notes)
        {
            const auto index = (size_t) juce::jlimit (0, LaneSettingsTable::numLanes - 1, n.noteNumber);
            auto& stats = report.lane[index];

            if (stats.hits == 0)
            {
                stats.minVelocity = n.velocity;
                stats.maxVelocity = n.velocity;
            }
            else
            {
                stats.minVelocity = juce::jmin (stats.minVelocity, n.velocity);
                stats.maxVelocity = juce::jmax (stats.maxVelocity, n.velocity);
            }

            ++stats.hits;
            velocityTotal[index] += (double) n.velocity;

            if (const auto found = sourceById.find (n.sourceId);
                found != sourceById.end() && ! n.isGhost)
            {
                // Compare on the shorter way round the loop, so a note that wrapped past
                // the start is not reported as having moved almost a whole clip.
                auto shift = n.startPpq - found->second->startPpq;

                if (shift > loopLen * 0.5)
                    shift -= loopLen;
                else if (shift < -loopLen * 0.5)
                    shift += loopLen;

                const auto shiftMs = shift * 60000.0 / bpm;
                shiftTotal[index] += shiftMs;
                stats.peakShiftMs = juce::jmax (stats.peakShiftMs, std::abs (shiftMs));
            }
        }

        for (size_t index = 0; index < report.lane.size(); ++index)
        {
            auto& stats = report.lane[index];

            if (stats.hits > 0)
            {
                stats.meanShiftMs  = shiftTotal[index] / (double) stats.hits;
                stats.meanVelocity = velocityTotal[index] / (double) stats.hits;
            }
        }
    }

    if (reportOut != nullptr)
        *reportOut = report;

    return out;
}

} // namespace mh
