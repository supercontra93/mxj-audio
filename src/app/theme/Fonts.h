// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace mxj::fonts
{
    /** IBM Plex Sans / Mono, embedded in the program (SIL Open Font License 1.1). */
    juce::Typeface::Ptr sansRegular();
    juce::Typeface::Ptr sansSemiBold();
    juce::Typeface::Ptr monoRegular();

    /** Fonts of `height` logical pixels times the text scale. Sizes in the code
        are written for scale 1.0; the user picks the scale (View > Text Size). */
    juce::Font sans (float height);
    juce::Font sansBold (float height);
    juce::Font mono (float height);

    /** Base text size of the interface (before scaling), in logical pixels. */
    inline constexpr float baseSize = 13.0f;

    /** Text size of the menu bar and the menus (before scaling), a little larger
        than the rest so the menus are easy to read. */
    inline constexpr float menuSize = 14.0f;

    /** Text scale: 1.0 is the size of versions 0.1 and 0.2. */
    void setTextScale (float scale);
    float getTextScale();

    /** The text scales the user can choose, smallest first, and the default. */
    inline constexpr float textScales[] { 1.0f, 1.15f, 1.3f, 1.5f };
    inline constexpr float defaultTextScale = 1.15f;

    /** A size in pixels that grows with the text (row heights, bars, dialogs). */
    int scaled (int pixels);
}
