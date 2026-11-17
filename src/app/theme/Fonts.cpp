// SPDX-License-Identifier: AGPL-3.0-only
#include "app/theme/Fonts.h"

#include <MxjResources.h>

namespace mxj::fonts
{
    namespace
    {
        juce::Typeface::Ptr load (const char* data, int size)
        {
            return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }
    }

    juce::Typeface::Ptr sansRegular()
    {
        static auto t = load (mxjres::IBMPlexSansRegular_ttf, mxjres::IBMPlexSansRegular_ttfSize);
        return t;
    }

    juce::Typeface::Ptr sansSemiBold()
    {
        static auto t = load (mxjres::IBMPlexSansSemiBold_ttf, mxjres::IBMPlexSansSemiBold_ttfSize);
        return t;
    }

    juce::Typeface::Ptr monoRegular()
    {
        static auto t = load (mxjres::IBMPlexMonoRegular_ttf, mxjres::IBMPlexMonoRegular_ttfSize);
        return t;
    }

    juce::Font sans (float height)     { return juce::Font (juce::FontOptions (sansRegular()).withHeight (height)); }
    juce::Font sansBold (float height) { return juce::Font (juce::FontOptions (sansSemiBold()).withHeight (height)); }
    juce::Font mono (float height)     { return juce::Font (juce::FontOptions (monoRegular()).withHeight (height)); }
}
