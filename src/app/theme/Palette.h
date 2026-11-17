// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_graphics/juce_graphics.h>

/** "Grafit" colour palette, chosen by Baybars on 2026-11-17.
    Source: design drafts, direction A. Change colours here only. */
namespace mxj::palette
{
    inline const juce::Colour background { 0xff1a1d21 }; // window background
    inline const juce::Colour panel      { 0xff22262c }; // panels
    inline const juce::Colour panelRaised{ 0xff2a2f36 }; // hovered items, inputs
    inline const juce::Colour line       { 0xff363c45 }; // 1 px separators
    inline const juce::Colour text       { 0xffd8dce2 };
    inline const juce::Colour textDim    { 0xff878f9b };
    inline const juce::Colour accent     { 0xfff0a23b }; // amber: selection, focus, waveform
    inline const juce::Colour accentDark { 0xffa86c1f }; // waveform second tone
    inline const juce::Colour playhead   { 0xff4cc2d8 }; // cyan
    inline const juce::Colour selection  { juce::Colour (76, 194, 216).withAlpha (0.20f) };
    inline const juce::Colour onAccent   { 0xff1a1d21 }; // text drawn on amber
    inline const juce::Colour record     { 0xffff4d4d };
    inline const juce::Colour meterGreen { 0xff6fbf4a }; // level meters: safe levels

    /** Track colours, in order. */
    inline const juce::Colour tracks[] { juce::Colour (0xfff0a23b), juce::Colour (0xff4cc2d8),
                                         juce::Colour (0xff8bc34a), juce::Colour (0xffc46be0),
                                         juce::Colour (0xffe86a5a) };

    inline constexpr float cornerRadius = 2.0f;
}
