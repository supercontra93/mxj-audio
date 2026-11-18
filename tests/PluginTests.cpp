// SPDX-License-Identifier: AGPL-3.0-only
#include "TestHelpers.h"
#include "core/Version.h"
#include "core/audio/BlockStore.h"
#include "core/audio/SampleSource.h"
#include "dsp/effects/EffectCatalogue.h"
#include "dsp/process/Render.h"
#include "plugins/catalogue/PluginCatalogue.h"
#include "plugins/clap/ClapFormat.h"
#include "plugins/host/PluginHost.h"
#include "plugins/host/PluginScanner.h"
#include "plugins/lua/LuaPlugins.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace mxj;
using namespace mxj::plugins;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;

    AudioSequence constant (int channels, int frames, float value)
    {
        juce::AudioBuffer<float> b (channels, frames);
        for (int c = 0; c < channels; ++c)
            juce::FloatVectorOperations::fill (b.getWritePointer (c), value, frames);

        return AudioSequence::fromSource (std::make_shared<MemorySampleSource> (std::move (b), rate));
    }

    juce::AudioBuffer<float> readAll (const AudioSequence& s)
    {
        juce::AudioBuffer<float> b (s.getNumChannels(), (int) s.getNumFrames());
        s.read (b.getArrayOfWritePointers(), b.getNumChannels(), 0, b.getNumSamples());
        return b;
    }

    /** Writes a plugin folder with plugin.json and main.lua. */
    juce::File writePlugin (const juce::File& parent, const juce::String& id, const juce::String& type,
                            const juce::String& lua, const juce::String& extraJson = {})
    {
        auto folder = parent.getChildFile (id);
        folder.createDirectory();
        folder.getChildFile ("plugin.json").replaceWithText (
            "{ \"id\": \"" + id + "\", \"name\": \"" + id + "\", \"version\": \"1.0.0\", \"author\": \"Test\", "
            "\"license\": \"MIT\", \"type\": \"" + type + "\"" + extraJson + " }");
        folder.getChildFile ("main.lua").replaceWithText (lua);
        return folder;
    }

    juce::File zipFolder (const juce::File& folder, const juce::File& zip)
    {
        juce::ZipFile::Builder builder;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true))
            if (! entry.isDirectory())
                builder.addFile (entry.getFile(), 9, entry.getFile().getRelativePathFrom (folder).replaceCharacter ('\\', '/'));

        juce::FileOutputStream out (zip);
        builder.writeToStream (out, nullptr);
        return zip;
    }

    const char* gainEffect = R"(
        function process(block, params)
          local g = mxj.dbToGain(params.gain)
          for i = 1, block.frames do
            for c = 1, block.channels do block:set(c, i, block:get(c, i) * g) end
          end
        end)";

    const char* gainParams = R"(, "parameters": [ { "id": "gain", "name": "Gain", "type": "number", "min": -60, "max": 12, "default": -6, "unit": "dB" } ])";

    juce::File vst3Bundle()
    {
        // .../MXJ Test Gain.vst3/Contents/<arch>/MXJ Test Gain.so|.vst3
        return juce::File (MXJ_TEST_VST3_BINARY).getParentDirectory().getParentDirectory().getParentDirectory();
    }
}

//==============================================================================
TEST_CASE ("plugin.json is checked before a plugin is used", "[plugins]")
{
    LuaPluginInfo info;
    juce::String error;

    const auto parse = [&] (const juce::String& json)
    {
        error.clear();
        return LuaPluginInfo::parse (juce::JSON::parse (json), info, error);
    };

    CHECK (parse (R"({ "id": "com.example.ok", "name": "Ok", "version": "1.0", "author": "A", "license": "MIT", "type": "effect" })"));
    CHECK_FALSE (parse (R"({ "id": "Bad Id", "name": "x", "version": "1", "author": "a", "license": "MIT", "type": "effect" })"));
    CHECK_FALSE (parse (R"({ "id": "com.example.x", "name": "x", "version": "1", "author": "a", "license": "MIT", "type": "synth" })"));
    CHECK_FALSE (parse (R"({ "id": "com.example.x", "name": "x", "version": "1", "author": "a", "license": "MIT", "type": "effect", "apiVersion": 99 })"));
    CHECK_FALSE (parse (R"({ "id": "com.example.x", "name": "x", "version": "1", "author": "a", "license": "MIT", "type": "effect", "minMxjVersion": "99.0" })"));
    CHECK_FALSE (parse (R"({ "id": "com.example.x", "name": "x", "version": "1", "author": "a", "license": "MIT", "type": "effect", "permissions": ["network"] })"));
    CHECK_THAT (error.toStdString(), ContainsSubstring ("permission"));
    CHECK_FALSE (parse (R"({ "id": "com.example.x", "name": "x", "version": "1", "author": "a", "license": "MIT", "type": "effect", "main": "../evil.lua" })"));
    CHECK_FALSE (parse (R"({ "id": "com.example.x", "name": "x", "version": "1", "author": "a", "license": "MIT", "type": "effect",
                             "parameters": [ { "id": "g", "type": "number", "min": 1, "max": 0 } ] })"));

    // A generator always gets a duration.
    REQUIRE (parse (R"({ "id": "com.example.gen", "name": "G", "version": "1", "author": "a", "license": "MIT", "type": "generator" })"));
    REQUIRE (! info.parameters.empty());
    CHECK (info.parameters[0].id == "duration");

    CHECK (compareVersions ("0.6.0", "0.6") == 0);
    CHECK (compareVersions ("0.10.0", "0.9.5") > 0);
    CHECK (compareVersions ("1.2.3", "1.2.4") < 0);
}

TEST_CASE ("Lua plugins run in a sandbox with limits", "[plugins]")
{
    LuaPluginInfo info;
    info.id = "com.example.sandbox";
    info.name = "Sandbox";

    juce::StringArray logged;
    LuaHost host;
    host.log = [&logged] (const juce::String& s) { logged.add (s); };

    juce::String error, result;

    SECTION ("no files, processes, modules or bytecode")
    {
        LuaScript script (info, host);
        REQUIRE (script.load (R"(
            function run()
              return tostring(io) .. " " .. tostring(os.execute) .. " " .. tostring(require) .. " "
                  .. tostring(load) .. " " .. tostring(dofile) .. " " .. tostring(string.dump) .. " " .. tostring(debug)
            end
            print("hello", 42))", error));
        REQUIRE (script.call ("run", {}, result, error));
        CHECK (result == "nil nil nil nil nil nil nil");
        CHECK (logged.contains ("hello  42"));
    }

    SECTION ("an endless loop is stopped")
    {
        LuaScript script (info, host);
        script.setTimeLimit (0.3);
        REQUIRE (script.load ("function run() while true do end end", error));
        CHECK_FALSE (script.call ("run", {}, result, error));
        CHECK_THAT (error.toStdString(), ContainsSubstring ("too long"));
    }

    SECTION ("memory is limited")
    {
        LuaScript script (info, host);
        REQUIRE (script.load ("function run() local t = {} for i = 1, 1e9 do t[i] = string.rep('x', 1000) .. i end end", error));
        CHECK_FALSE (script.call ("run", {}, result, error));
        CHECK_THAT (error.toStdString(), ContainsSubstring ("memory"));
    }

    SECTION ("errors name the line")
    {
        LuaScript script (info, host);
        REQUIRE (script.load ("function run()\n  local x = nil\n  return x.y\nend", error));
        CHECK_FALSE (script.call ("run", {}, result, error));
        CHECK_THAT (error.toStdString(), ContainsSubstring ("main.lua:3"));
    }

    SECTION ("commands need the permission")
    {
        juce::StringArray ran;
        host.runCommand = [&ran] (const juce::String& id) { ran.add (id); return true; };

        LuaScript without (info, host);
        REQUIRE (without.load ("function run() mxj.command('edit.selectAll') end", error));
        CHECK_FALSE (without.call ("run", {}, result, error));
        CHECK (ran.isEmpty());

        info.permissions.add ("commands");
        LuaScript with (info, host);
        REQUIRE (with.load ("function run() return tostring(mxj.command('edit.selectAll')) end", error));
        REQUIRE (with.call ("run", {}, result, error));
        CHECK (result == "true");
        CHECK (ran == juce::StringArray { "edit.selectAll" });
    }
}

TEST_CASE ("Lua effect, generator, analysis and tool plugins work through the program", "[plugins]")
{
    test::TempFolder temp;
    const auto installed = temp.folder.getChildFile ("lua");
    installed.createDirectory();
    BlockStore store (temp.folder.getChildFile ("blocks"));

    writePlugin (installed, "test.gain", "effect", gainEffect, gainParams);
    writePlugin (installed, "test.dc", "generator", "function generate(b, p) for i = 1, b.frames do for c = 1, b.channels do b:set(c, i, 0.25) end end end");
    writePlugin (installed, "test.count", "analysis",
                 "local n = 0\nfunction prepare() n = 0 end\nfunction analyse(b) n = n + b.frames end\nfunction finish() return 'frames ' .. n end");
    writePlugin (installed, "test.tool", "tool", "function run() mxj.message('one') mxj.command('view.zoomFull') return 'two' end",
                 R"(, "permissions": ["commands"])");
    writePlugin (installed, "test.broken", "effect", "function process( this is not lua");

    LuaPluginManager manager (installed);
    manager.reload();

    REQUIRE (manager.getPlugins().size() == 5);
    REQUIRE (manager.find ("test.broken") != nullptr);
    CHECK (manager.find ("test.broken")->error.isNotEmpty());
    CHECK (dsp::catalogue::find ("lua:test.broken") == nullptr);

    SECTION ("effect: -6 dB through the effect catalogue")
    {
        const auto* type = dsp::catalogue::find ("lua:test.gain");
        REQUIRE (type != nullptr);
        CHECK (type->kind == dsp::EffectType::Kind::script);
        CHECK (type->category == LuaPluginManager::menuGroup);

        auto settings = dsp::EffectSettings::defaultsFor ("lua:test.gain");
        auto chain = dsp::ProcessorChain::create ({ settings });
        REQUIRE (chain->size() == 1);

        const auto input = constant (2, 30000, 0.5f);
        const auto output = dsp::render (input, *chain, store);
        REQUIRE (output.getNumFrames() == 30000);
        const auto b = readAll (output);
        CHECK_THAT (b.getSample (0, 0), WithinAbs (0.5 * std::pow (10.0, -6.0 / 20.0), 1.0e-4));
        CHECK_THAT (b.getSample (1, 29999), WithinAbs (0.5 * std::pow (10.0, -6.0 / 20.0), 1.0e-4));
    }

    SECTION ("an effect that fails stops the render with its error")
    {
        writePlugin (installed, "test.fails", "effect", "function process(b) error('boom') end");
        manager.reload();
        auto chain = dsp::ProcessorChain::create ({ dsp::EffectSettings::defaultsFor ("lua:test.fails") });
        const auto output = dsp::render (constant (1, 1000, 0.5f), *chain, store);
        CHECK (output.getNumChannels() == 0);
        CHECK_THAT (chain->getError().toStdString(), ContainsSubstring ("boom"));
        CHECK (manager.getConsole().joinIntoString ("\n").contains ("boom"));
    }

    SECTION ("generator")
    {
        juce::String error;
        auto settings = dsp::EffectSettings::defaultsFor ("lua:test.dc");
        REQUIRE (settings.typeId.isNotEmpty());
        const auto generated = dsp::generate (settings, 2, rate, 20000, store, {}, &error);
        REQUIRE (generated.getNumFrames() == 20000);
        CHECK (readAll (generated).getSample (1, 19999) == 0.25f);
    }

    SECTION ("analysis")
    {
        juce::String report, error;
        REQUIRE (manager.runAnalysis (*manager.find ("test.count"), constant (1, 40000, 0.1f), {}, {}, report, error));
        CHECK (report == "frames 40000");
    }

    SECTION ("tool")
    {
        juce::String messages, error;
        juce::StringArray ran;
        REQUIRE (manager.runTool ("test.tool", {}, [&ran] (const juce::String& id) { ran.add (id); return true; }, messages, error));
        CHECK (messages == "one\ntwo");
        CHECK (ran == juce::StringArray { "view.zoomFull" });
    }

    SECTION ("a switched-off plugin is not offered")
    {
        manager.setEnabled ("test.gain", false);
        CHECK (dsp::catalogue::find ("lua:test.gain") == nullptr);
        manager.setEnabled ("test.gain", true);
        CHECK (dsp::catalogue::find ("lua:test.gain") != nullptr);
    }

    SECTION ("uninstall removes the folder")
    {
        REQUIRE (manager.uninstall ("test.gain"));
        CHECK_FALSE (installed.getChildFile ("test.gain").exists());
        CHECK (dsp::catalogue::find ("lua:test.gain") == nullptr);
    }
}

TEST_CASE ("The example plugins load and the test catalogue installs them", "[plugins]")
{
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));

    // Every example is a valid plugin.
    for (const auto& entry : juce::RangedDirectoryIterator (juce::File (MXJ_LUA_EXAMPLES_DIR), false, "*", juce::File::findDirectories))
    {
        LuaPluginInfo info;
        juce::String error;
        INFO (entry.getFile().getFullPathName());
        CHECK (LuaPluginInfo::readFolder (entry.getFile(), info, error));
        CHECK (error.isEmpty());
    }

    // A catalogue in the layout the build makes: index.json and packages/.
    const auto catalogueFolder = temp.folder.getChildFile ("catalog");
    catalogueFolder.getChildFile ("packages").createDirectory();
    const auto package = zipFolder (juce::File (MXJ_LUA_EXAMPLES_DIR).getChildFile ("tremolo"),
                                    catalogueFolder.getChildFile ("packages/tremolo.mxjplugin"));
    const auto sha = PluginCatalogue::sha256Of (package);
    REQUIRE (sha.length() == 64);

    const auto writeIndex = [&] (const juce::String& checksum)
    {
        catalogueFolder.getChildFile ("index.json").replaceWithText (
            R"({ "format": 1, "name": "Test", "plugins": [ { "id": "mxj.examples.tremolo", "name": "Tremolo", "version": "1.0.0",
                 "author": "MXJ AUDIO", "license": "MIT", "type": "effect", "category": "Modulation", "description": "Volume up and down",
                 "package": "packages/tremolo.mxjplugin", "sha256": ")" + checksum + R"(", "size": )" + juce::String (package.getSize()) + " } ] }");
    };

    writeIndex (sha);
    PluginCatalogue catalogue;
    juce::String error;
    REQUIRE (catalogue.load (catalogueFolder.getChildFile ("index.json"), error));
    REQUIRE (catalogue.getEntries().size() == 1);
    CHECK (catalogue.search ("volume", {}).size() == 1);
    CHECK (catalogue.search ("volume", "Distortion").empty());
    CHECK (catalogue.search ("nothing like this", {}).empty());

    const auto verified = catalogue.getVerifiedPackage (catalogue.getEntries()[0], error);
    REQUIRE (verified.existsAsFile());

    LuaPluginManager manager (temp.folder.getChildFile ("installed"));
    manager.reload();
    REQUIRE (manager.install (verified, error));
    REQUIRE (manager.find ("mxj.examples.tremolo") != nullptr);
    CHECK (manager.find ("mxj.examples.tremolo")->error.isEmpty());
    CHECK (dsp::catalogue::find ("lua:mxj.examples.tremolo") != nullptr);

    // The tremolo changes the level over time, and keeps it finite.
    auto chain = dsp::ProcessorChain::create ({ dsp::EffectSettings::defaultsFor ("lua:mxj.examples.tremolo") });
    const auto out = readAll (dsp::render (constant (2, 48000, 0.5f), *chain, store));
    CHECK (out.getMagnitude (0, 48000) <= 0.5f);
    CHECK (out.findMinMax (0, 0, 48000).getLength() > 0.2f);

    // A changed package is refused.
    writeIndex (juce::String::repeatedString ("0", 64));
    REQUIRE (catalogue.load (catalogueFolder.getChildFile ("index.json"), error));
    error.clear();
    CHECK (catalogue.getVerifiedPackage (catalogue.getEntries()[0], error) == juce::File());
    CHECK_THAT (error.toStdString(), ContainsSubstring ("SHA-256"));

    // A package with a path out of its folder is refused.
    {
        juce::ZipFile::Builder builder;
        builder.addEntry (std::make_unique<juce::MemoryInputStream> ("x", 1, false), 9, "../evil.lua", juce::Time::getCurrentTime());
        const auto evil = temp.folder.getChildFile ("evil.mxjplugin");
        juce::FileOutputStream o (evil);
        builder.writeToStream (o, nullptr);
        o.flush();
        LuaPluginInfo info;
        CHECK_FALSE (LuaPluginManager::readPackage (evil, info, error));
    }
}

//==============================================================================
TEST_CASE ("CLAP plugins are found, loaded, played and their state kept", "[plugins][clap]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));

    ClapPluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, MXJ_TEST_CLAP_GAIN);
    REQUIRE (found.size() == 1);
    CHECK (found[0]->name == "MXJ Test Gain");
    CHECK (found[0]->pluginFormatName == "CLAP");
    CHECK_FALSE (found[0]->isInstrument);

    PluginHost host;
    host.addFormat (std::make_unique<ClapPluginFormat>());
    host.addScanResult ("CLAP", MXJ_TEST_CLAP_GAIN, found);
    host.registerEffects();

    const auto id = PluginHost::effectIdFor (*found[0]);
    CHECK (id.startsWith ("clap:"));
    const auto* type = dsp::catalogue::find (id);
    REQUIRE (type != nullptr);
    CHECK (type->isHostedPlugin());
    CHECK (type->category == "CLAP");

    // Default gain -6 dB.
    auto settings = dsp::EffectSettings::defaultsFor (id);
    auto chain = dsp::ProcessorChain::create ({ settings });
    auto out = readAll (dsp::render (constant (2, 10000, 0.5f), *chain, store));
    REQUIRE (out.getNumSamples() == 10000);
    CHECK_THAT (out.getSample (0, 5000), WithinAbs (0.5 * std::pow (10.0, -6.0 / 20.0), 1.0e-4));
    CHECK_THAT (out.getSample (1, 9999), WithinAbs (0.5 * std::pow (10.0, -6.0 / 20.0), 1.0e-4));

    // A saved state (gain 0 dB) travels with the settings into a new plugin.
    juce::MemoryBlock zero;
    juce::MemoryOutputStream (zero, false).writeFloat (0.0f);
    settings.state = zero.toBase64Encoding();
    chain = dsp::ProcessorChain::create ({ settings });
    out = readAll (dsp::render (constant (1, 3000, 0.5f), *chain, store));
    CHECK_THAT (out.getSample (0, 100), WithinAbs (0.5, 1.0e-5));

    // The settings keep the state through JSON.
    const auto back = dsp::EffectSettings::fromVar (settings.toVar());
    CHECK (back.state == settings.state);

    // Values made with the same instance name share one live plugin.
    settings.instance = "slot-1";
    const auto a = settings.createValues (true);
    const auto b = settings.createValues (true);
    const auto c = settings.createValues (false);
    CHECK (a->state == b->state);
    CHECK (a->state != c->state);
    CHECK (a->state->save() == settings.state);
}

TEST_CASE ("VST3 plugins are found, loaded and played", "[plugins][vst3]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    test::TempFolder temp;
    BlockStore store (temp.folder.getChildFile ("blocks"));

    REQUIRE (vst3Bundle().exists());

    PluginHost host;
    host.addFormat (std::make_unique<juce::VST3PluginFormatHeadless>());

    juce::OwnedArray<juce::PluginDescription> found;
    PluginScanner::scanFile (*host.findFormat ("VST3"), vst3Bundle().getFullPathName(), found);
    REQUIRE (found.size() == 1);
    CHECK (found[0]->name == "MXJ Test Gain");
    CHECK (PluginHost::isEffect (*found[0]));

    host.addScanResult ("VST3", vst3Bundle().getFullPathName(), found);
    host.registerEffects();
    const auto id = PluginHost::effectIdFor (*found[0]);
    REQUIRE (dsp::catalogue::find (id) != nullptr);

    auto settings = dsp::EffectSettings::defaultsFor (id);
    auto chain = dsp::ProcessorChain::create ({ settings });
    const auto out = readAll (dsp::render (constant (2, 10000, 0.5f), *chain, store));
    REQUIRE (out.getNumSamples() == 10000);
    CHECK_THAT (out.getSample (0, 5000), WithinAbs (0.5 * std::pow (10.0, -6.0 / 20.0), 1.0e-3));

    // Mono audio through a stereo plugin.
    const auto mono = readAll (dsp::render (constant (1, 2000, 0.5f), *chain, store));
    CHECK_THAT (mono.getSample (0, 1000), WithinAbs (0.5 * std::pow (10.0, -6.0 / 20.0), 1.0e-3));

    // The list is saved and read back.
    const auto listFile = temp.folder.getChildFile ("known-plugins.xml");
    REQUIRE (host.load (listFile) == false);
    host.addScanResult ("VST3", vst3Bundle().getFullPathName(), found);
    host.setUserFolders ("VST3", { temp.folder.getFullPathName() });
    REQUIRE (host.save());

    PluginHost again;
    again.addFormat (std::make_unique<juce::VST3PluginFormatHeadless>());
    REQUIRE (again.load (listFile));
    CHECK (again.getPlugins().size() == 1);
    CHECK (again.getUserFolders ("VST3") == juce::StringArray { temp.folder.getFullPathName() });
    CHECK (again.findFilesToScan (false).empty()); // nothing new
}

TEST_CASE ("Plugins are scanned in a separate program and a crashing one is blocked", "[plugins][scanner]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    test::TempFolder temp;

    PluginHost host;
    host.addFormat (std::make_unique<ClapPluginFormat>());
    host.load (temp.folder.getChildFile ("list.xml"));
    host.setUserFolders ("CLAP", { juce::File (MXJ_TEST_CLAP_GAIN).getParentDirectory().getFullPathName(),
                                   juce::File (MXJ_TEST_CLAP_CRASH).getParentDirectory().getFullPathName() });

    PluginScanner scanner (host, juce::File (MXJ_TEST_SCANNER));
    REQUIRE (juce::File (MXJ_TEST_SCANNER).existsAsFile());
    scanner.start (false);

    const auto start = juce::Time::getMillisecondCounter();
    while (scanner.isScanning() && juce::Time::getMillisecondCounter() - start < 60000)
        juce::Thread::sleep (20);

    REQUIRE_FALSE (scanner.isScanning());

    const auto plugins = host.getPlugins();
    REQUIRE (plugins.size() == 1);
    CHECK (plugins[0].name == "MXJ Test Gain");

    const auto blocked = host.getBlocked();
    REQUIRE (blocked.size() == 1);
    CHECK (juce::File (blocked[0].file) == juce::File (MXJ_TEST_CLAP_CRASH));
    CHECK_THAT (blocked[0].reason.toStdString(), ContainsSubstring ("Crashed"));

    // Blocked files are skipped next time, until they are unblocked.
    CHECK (host.findFilesToScan (true).size() == 1);
    host.unblock (blocked[0].file);
    CHECK (host.findFilesToScan (true).size() == 2);
}

TEST_CASE ("Effect types of plugins can be added and removed while pointers stay valid", "[plugins]")
{
    dsp::EffectType t;
    t.id = "test:added";
    t.name = "Added";
    t.category = "Test";
    t.source = "Test";
    const auto* added = dsp::catalogue::add (t);
    REQUIRE (added != nullptr);
    CHECK (dsp::catalogue::find ("test:added") == added);

    t.name = "Replaced";
    const auto* replaced = dsp::catalogue::add (t);
    CHECK (dsp::catalogue::find ("test:added") == replaced);
    CHECK (added->name == "Added"); // still readable

    dsp::catalogue::remove ("test:added");
    CHECK (dsp::catalogue::find ("test:added") == nullptr);

    // Built-in effects cannot be replaced.
    t.id = "amplify";
    CHECK (dsp::catalogue::add (t) == nullptr);
}
