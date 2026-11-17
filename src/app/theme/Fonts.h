// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace mxj::fonts
{
    /** IBM Plex Sans / Mono, embedded in the program (SIL Open Font License 1.1). */
    juce::Typeface::Ptr sansRegular();
    juce::Typeface::Ptr sansSemiBold();
    juce::Typeface::Ptr monoRegular();

    juce::Font sans (float height);
    juce::Font sansBold (float height);
    juce::Font mono (float height);

    /** Base text size of the interface, in logical pixels. */
    inline constexpr float baseSize = 13.0f;
}
