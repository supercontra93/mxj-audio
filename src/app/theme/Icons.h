// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace mxj::icons
{
    /** Phosphor icons (MIT), embedded as SVG. Names match resources/icons/<name>.svg. */
    std::unique_ptr<juce::Drawable> create (const juce::String& name, juce::Colour colour);

    /** The MXJ AUDIO logo. */
    std::unique_ptr<juce::Drawable> createLogo();
}
