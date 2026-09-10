// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include <juce_core/juce_core.h>

namespace mh
{

/** A musical subdivision, shared by the visual grid, snapping, tightening and swing so
    the numbers on screen and the numbers in the engine can never drift apart.
*/
struct GridDivision
{
    const char* label;
    double      ppq;      ///< length of one step in quarter notes
    bool        triplet;
};

namespace detail
{
    inline const GridDivision* gridDivisionTable()
    {
        static const GridDivision table[] =
        {
            { "1/1",   4.0,        false },
            { "1/2",   2.0,        false },
            { "1/4",   1.0,        false },
            { "1/4T",  2.0 / 3.0,  true  },
            { "1/8",   0.5,        false },
            { "1/8T",  1.0 / 3.0,  true  },
            { "1/16",  0.25,       false },
            { "1/16T", 1.0 / 6.0,  true  },
            { "1/32",  0.125,      false },
            { "1/32T", 1.0 / 12.0, true  },
            { "1/64",  0.0625,     false },
        };

        return table;
    }

    constexpr int gridDivisionCount = 11;
}

inline int numGridDivisions() { return detail::gridDivisionCount; }

inline const GridDivision& gridDivision (int index)
{
    return detail::gridDivisionTable()[juce::jlimit (0, detail::gridDivisionCount - 1, index)];
}

/** Index of 1/16, the default grid for drum programming. */
constexpr int defaultGridDivisionIndex = 6;

/** Index of 1/8, the default swing grid. */
constexpr int defaultSwingDivisionIndex = 4;

inline juce::StringArray gridDivisionLabels()
{
    juce::StringArray labels;

    for (int i = 0; i < numGridDivisions(); ++i)
        labels.add (gridDivision (i).label);

    return labels;
}

} // namespace mh
