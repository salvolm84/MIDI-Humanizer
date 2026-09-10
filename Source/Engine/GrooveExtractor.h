#pragma once

#include "LaneSettings.h"
#include "../Model/GridDivision.h"
#include "../Model/NoteSequence.h"

#include <vector>

namespace mh
{

/** What one lane of a reference performance actually does. */
struct GrooveLane
{
    int    note = 36;
    int    hits = 0;

    /** Mean signed deviation from the nearest grid position, in ms. Negative means the
        drummer plays ahead of the beat.
    */
    double meanDeviationMs = 0.0;

    /** Standard deviation of that deviation, in ms: how tight the playing is. */
    double deviationSpreadMs = 0.0;

    double meanVelocity = 0.0;
    double velocitySpread = 0.0;   ///< standard deviation of velocity
};

struct GrooveAnalysis
{
    std::vector<GrooveLane> lanes;
    int    totalHits = 0;
    double bpm = 120.0;
    juce::String sourceName;

    const GrooveLane* find (int noteNumber) const;
};

/** Measures the feel of a played performance so it can be applied to a programmed clip.

    This is the natural inverse of the bias-and-spread model the humanizer uses: a lane's
    mean deviation from the grid *is* its bias, and the spread of those deviations *is*
    its spread. Rather than guessing at numbers, the settings can be measured off a take
    that already feels right.

    Swing is deliberately not extracted. A swung reference shows up as a large spread
    because its off-beats sit far from the straight grid, and separating genuine swing
    from looseness reliably needs more than one clip's worth of evidence. Analyse against
    a triplet grid if the reference swings.
*/
class GrooveExtractor
{
public:
    /** `gridDivisionIndex` indexes the shared grid table and decides what counts as "on
        the grid"; deviations are measured to the nearest step of it.
    */
    static GrooveAnalysis analyse (const NoteSequence& reference, int gridDivisionIndex, double bpm);

    /** Turns one analysed lane into settings that reproduce its feel.

        Only the four values the analysis can actually measure are written. Velocity
        Dynamics is left alone, because how much of the *target* clip's own accenting to
        keep is a taste decision the reference cannot answer.
    */
    static void applyTo (LaneSettings& lane, const GrooveLane& measured);

    /** Converts a standard deviation into the peak value the spread controls expect.

        Spread is applied as a triangular distribution over [-peak, peak], whose standard
        deviation is peak / sqrt(6). Scaling by sqrt(6) therefore makes the humanized
        result vary about as much as the reference actually did, instead of a factor of
        two too tight.
    */
    static double standardDeviationToPeak (double standardDeviation);
};

} // namespace mh
