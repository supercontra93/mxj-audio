// SPDX-License-Identifier: AGPL-3.0-only
#include "formats/metadata/CueMarkers.h"

#include <algorithm>
#include <map>

namespace mxj::cueMarkers
{
    MarkerList read (const juce::StringPairArray& metadata, juce::int64 numFrames)
    {
        std::map<int, juce::String> labels;
        const auto numLabels = metadata.getValue ("NumCueLabels", "0").getIntValue();
        for (int i = 0; i < numLabels; ++i)
        {
            const auto prefix = "CueLabel" + juce::String (i);
            labels[metadata.getValue (prefix + "Identifier", "-1").getIntValue()] = metadata.getValue (prefix + "Text", {});
        }

        MarkerList markers;
        const auto numCues = metadata.getValue ("NumCuePoints", "0").getIntValue();
        for (int i = 0; i < numCues; ++i)
        {
            const auto prefix = "Cue" + juce::String (i);
            const auto id = metadata.getValue (prefix + "Identifier", "-1").getIntValue();
            const auto position = (juce::int64) (juce::uint32) metadata.getValue (prefix + "Offset", "0").getLargeIntValue();

            if (position < 0 || position > numFrames)
                continue;

            const auto label = labels.find (id);
            markers.push_back ({ position, label != labels.end() && label->second.isNotEmpty() ? label->second
                                                                                                : "Marker " + juce::String (i + 1) });
        }

        std::stable_sort (markers.begin(), markers.end(), [] (const Marker& a, const Marker& b) { return a.position < b.position; });
        return markers;
    }

    std::unordered_map<juce::String, juce::String> toMetadata (const MarkerList& markers)
    {
        std::unordered_map<juce::String, juce::String> values;
        if (markers.empty())
            return values;

        const auto count = juce::String ((int) markers.size());
        values["NumCuePoints"] = count;
        values["NumCueLabels"] = count;

        for (int i = 0; i < (int) markers.size(); ++i)
        {
            const auto id = juce::String (i + 1);
            const auto position = juce::String ((juce::int64) markers[(size_t) i].position);
            const auto cue = "Cue" + juce::String (i);
            const auto label = "CueLabel" + juce::String (i);

            values[cue + "Identifier"] = id;
            values[cue + "Order"] = position;  // dwPosition: some programs read this one
            values[cue + "Offset"] = position; // dwSampleOffset: most programs read this one
            values[label + "Identifier"] = id;
            values[label + "Text"] = markers[(size_t) i].name;
        }

        return values;
    }
}
