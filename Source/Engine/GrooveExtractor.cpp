#include "GrooveExtractor.h"

#include <cmath>
#include <map>

namespace mh
{

const GrooveLane* GrooveAnalysis::find (int noteNumber) const
{
    for (auto& lane : lanes)
        if (lane.note == noteNumber)
            return &lane;

    return nullptr;
}

double GrooveExtractor::standardDeviationToPeak (double standardDeviation)
{
    return standardDeviation * std::sqrt (6.0);
}

GrooveAnalysis GrooveExtractor::analyse (const NoteSequence& reference, int gridDivisionIndex, double bpm)
{
    GrooveAnalysis analysis;
    analysis.bpm = juce::jlimit (20.0, 999.0, bpm > 0.0 ? bpm : reference.sourceBpm);
    analysis.sourceName = reference.name;

    if (reference.notes.empty())
        return analysis;

    const auto step = gridDivision (gridDivisionIndex).ppq;
    const auto ppqToMs = 60000.0 / analysis.bpm;

    struct Accumulator
    {
        int    hits = 0;
        double deviationSum = 0.0;
        double deviationSquareSum = 0.0;
        double velocitySum = 0.0;
        double velocitySquareSum = 0.0;
    };

    std::map<int, Accumulator> byLane;

    for (auto& n : reference.notes)
    {
        auto& accumulator = byLane[n.noteNumber];

        // Deviation from the nearest grid step, which is what a listener hears as the
        // note being early or late.
        const auto nearest = std::round (n.startPpq / step) * step;
        const auto deviationMs = (n.startPpq - nearest) * ppqToMs;

        ++accumulator.hits;
        accumulator.deviationSum += deviationMs;
        accumulator.deviationSquareSum += deviationMs * deviationMs;
        accumulator.velocitySum += (double) n.velocity;
        accumulator.velocitySquareSum += (double) n.velocity * (double) n.velocity;
    }

    const auto standardDeviation = [] (double sum, double squareSum, int count)
    {
        if (count < 2)
            return 0.0;

        const auto mean = sum / (double) count;
        const auto variance = squareSum / (double) count - mean * mean;
        return variance > 0.0 ? std::sqrt (variance) : 0.0;
    };

    for (auto& [note, accumulator] : byLane)
    {
        GrooveLane lane;
        lane.note = note;
        lane.hits = accumulator.hits;
        lane.meanDeviationMs = accumulator.deviationSum / (double) accumulator.hits;
        lane.deviationSpreadMs = standardDeviation (accumulator.deviationSum,
                                                    accumulator.deviationSquareSum,
                                                    accumulator.hits);
        lane.meanVelocity = accumulator.velocitySum / (double) accumulator.hits;
        lane.velocitySpread = standardDeviation (accumulator.velocitySum,
                                                  accumulator.velocitySquareSum,
                                                  accumulator.hits);

        analysis.totalHits += accumulator.hits;
        analysis.lanes.push_back (lane);
    }

    return analysis;
}

void GrooveExtractor::applyTo (LaneSettings& lane, const GrooveLane& measured)
{
    lane.hasOverride = true;
    lane.timingBiasMs = (float) juce::jlimit (-60.0, 60.0, measured.meanDeviationMs);
    lane.timingSpreadMs = (float) juce::jlimit (0.0, 60.0,
                                                standardDeviationToPeak (measured.deviationSpreadMs));
    lane.velocityTarget = juce::jlimit (1, 127, (int) std::lround (measured.meanVelocity));
    lane.velocitySpread = (float) juce::jlimit (0.0, 63.0,
                                                standardDeviationToPeak (measured.velocitySpread));
}

} // namespace mh
