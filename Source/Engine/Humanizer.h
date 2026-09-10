#pragma once

#include "HumanizeParams.h"
#include "../Model/NoteSequence.h"

namespace mh
{

/** Deterministic random field.

    Every random decision is a pure hash of (seed, note identity, stream), never a
    running generator. Two consequences that the whole design leans on: the humanized
    clip drawn on screen is bit-identical to the one played back, and nudging an amount
    slider scales the existing deviations instead of dealing a fresh hand.
*/
namespace det
{
    /** SplitMix64 finaliser: cheap, and mixes low bits well enough for our purposes. */
    inline juce::uint64 hash64 (juce::uint64 x) noexcept
    {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    /** Independent random streams, so turning velocity jitter up does not change the
        timing jitter of the same note.
    */
    enum Stream
    {
        timingStream = 1,
        velocityStream,
        ghostGateStream,
        ghostVelStream,
        ghostTimeStream,
        variationStream,
        flamStream
    };

    /** Uniform in [0, 1). */
    inline double uniform (int seed, int identity, int stream) noexcept
    {
        const auto h = hash64 (  ((juce::uint64) (juce::uint32) seed     * 0x9e3779b1ULL)
                               ^ ((juce::uint64) (juce::uint32) identity << 21)
                               ^ ((juce::uint64) (juce::uint32) stream   << 49));
        return (double) (h >> 11) * (1.0 / 9007199254740992.0);   // 2^53
    }

    /** Roughly bell-shaped in [-1, 1]. Human timing errors cluster near zero rather
        than spreading evenly, so a triangular distribution reads as more natural than
        a flat one for the same peak deviation.
    */
    inline double bipolar (int seed, int identity, int stream) noexcept
    {
        const auto a = uniform (seed, identity, stream);
        const auto b = uniform (seed, identity, stream + 0x4000);
        return a + b - 1.0;
    }
}

/** What actually came out for one drum lane.

    The Velocity Target control promises that a lane's average becomes a given number;
    measuring the result is what lets the UI show that it did, rather than asking the user
    to take it on trust.
*/
struct LaneStats
{
    int    hits          = 0;
    double meanShiftMs   = 0.0;
    double peakShiftMs   = 0.0;
    int    minVelocity   = 0;
    int    maxVelocity   = 0;
    double meanVelocity  = 0.0;
};

/** What the transform changed, for display in the UI. */
struct HumanizeReport
{
    int    ghostsAdded       = 0;
    int    notesDroppedByGap = 0;
    int    flamsCreated      = 0;
    int    tailsChoked       = 0;
    double peakTimingShiftMs = 0.0;

    std::array<LaneStats, LaneSettingsTable::numLanes> lane {};

    const LaneStats& forNote (int noteNumber) const noexcept
    {
        return lane[(size_t) juce::jlimit (0, LaneSettingsTable::numLanes - 1, noteNumber)];
    }
};

/** Applies humanization to a clip, producing a new clip.

    Pure: same inputs always give the same output. Runs on the message thread over the
    whole clip, and the result is what both the grid and the player consume.
*/
class Humanizer
{
public:
    static NoteSequence process (const NoteSequence& source,
                                 const HumanizeParams& params,
                                 HumanizeReport* reportOut = nullptr);

    /** Musical weight of a position, 0 (off-grid) to 1 (bar line). Exposed because the
        grid editor shades bar and beat lines using the same notion of strength.
    */
    static double gridWeight (double ppq, double barLengthPpq);

private:
    static constexpr int ghostIdBase = 1 << 20;
    static constexpr int flamIdBase  = 1 << 21;
};

} // namespace mh
