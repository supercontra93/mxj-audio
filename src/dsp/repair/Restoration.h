// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/process/Render.h"

namespace mxj::dsp
{
    /** Repair of damaged samples: clicks and clipped peaks (master plan, module E,
        "tık giderme, kırpılma onarma"). Both find the damaged samples and draw
        them again from the sound around them, using a linear prediction (AR)
        model and least squares interpolation (Janssen, Veldhuis and Vries, 1986). */
    namespace restore
    {
        /** Replaces x[gapStart, gapStart + gapLength) by the most likely samples
            given the samples around them. Needs at least `order` samples on each
            side of the gap. Returns false (and changes nothing) if it cannot. */
        bool interpolate (float* x, int length, int gapStart, int gapLength, int order);

        /** Linear prediction coefficients a[1..order] (a[0] = 1) of a signal,
            from its autocorrelation (Levinson-Durbin). Samples where `skip` is
            true are left out. */
        std::vector<double> predictionCoefficients (const float* x, int length, int order, const std::vector<bool>* skip = nullptr);

        struct Report
        {
            int repaired = 0; ///< clicks or clipped passages that were repaired
            float gainDb = 0.0f; ///< level change applied afterwards (clip repair)
        };

        /** Removes clicks and crackle. `sensitivity` 0..100 (higher finds quieter
            clicks), `maxClickMs`: longer disturbances are left alone. */
        AudioSequence removeClicks (const AudioSequence& input, float sensitivity, float maxClickMs,
                                    BlockStore& store, const WorkControl& control = {}, Report* report = nullptr);

        /** Rebuilds peaks that were cut off. Samples within `tolerancePercent` of
            the highest peak count as clipped. With `keepLevel` the result is
            turned down so its peaks are no higher than the input's. */
        AudioSequence repairClipping (const AudioSequence& input, float tolerancePercent, bool keepLevel,
                                      BlockStore& store, const WorkControl& control = {}, Report* report = nullptr);
    }
}
