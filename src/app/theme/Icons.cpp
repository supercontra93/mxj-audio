// SPDX-License-Identifier: AGPL-3.0-only
#include "app/theme/Icons.h"

#include <MxjResources.h>

namespace mxj::icons
{
    namespace
    {
        juce::String loadSvg (const juce::String& resourceName)
        {
            int size = 0;
            if (const auto* data = mxjres::getNamedResource (resourceName.toRawUTF8(), size))
                return juce::String::fromUTF8 (data, size);

            jassertfalse; // unknown icon name
            return {};
        }
    }

    std::unique_ptr<juce::Drawable> create (const juce::String& name, juce::Colour colour)
    {
        auto svg = loadSvg (name.removeCharacters ("-") + "_svg");

        // Phosphor icons use currentColor; swap in the requested colour.
        svg = svg.replace ("currentColor", "#" + colour.toDisplayString (false));

        return juce::Drawable::createFromSVGString (svg);
    }

    std::unique_ptr<juce::Drawable> createLogo()
    {
        return juce::Drawable::createFromSVGString (loadSvg ("logo_svg"));
    }
}
