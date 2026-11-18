// SPDX-License-Identifier: AGPL-3.0-only
//
// MXJ Plugin Scanner: opens one plugin file and lists the plugins inside it.
// MXJ AUDIO runs it once per file, so a plugin that crashes or hangs while
// being scanned only takes this small program down, never MXJ AUDIO itself
// (master plan, section 6). The file is then put on the block list.
//
//   mxj-plugin-scanner --format VST3|CLAP --file <plugin file> --out <result.xml>
//
// Exit code 0 = the result file was written (it may list no plugins).

#include "plugins/clap/ClapFormat.h"
#include "plugins/host/PluginScanner.h"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juce; // a message thread: some plugins need one while loading

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    const auto value = [&args] (const char* name) { return args[args.indexOf (name) + 1]; };
    const auto formatName = value ("--format");
    const auto file = value ("--file");
    const auto out = value ("--out");

    if (args.indexOf ("--format") < 0 || file.isEmpty() || out.isEmpty())
    {
        std::fputs ("usage: mxj-plugin-scanner --format VST3|CLAP --file <plugin> --out <result.xml>\n", stderr);
        return 2;
    }

    std::unique_ptr<juce::AudioPluginFormat> format;
    if (formatName == "VST3")
        format = std::make_unique<juce::VST3PluginFormatHeadless>();
    else if (formatName == "CLAP")
        format = std::make_unique<mxj::plugins::ClapPluginFormat>();
    else
        return 3;

    juce::OwnedArray<juce::PluginDescription> results;
    mxj::plugins::PluginScanner::scanFile (*format, file, results);
    return mxj::plugins::PluginScanner::writeResults (juce::File (out), results) ? 0 : 4;
}
