// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "plugins/lua/LuaManifest.h"

#include <functional>
#include <memory>

struct lua_State;

namespace mxj::plugins
{
    /** What a script may ask of the program. All optional. */
    struct LuaHost
    {
        std::function<void (const juce::String&)> log;          ///< mxj.log() and print()
        std::function<void (const juce::String&)> message;      ///< mxj.message(): shown to the user after a tool ran
        std::function<bool (const juce::String&)> runCommand;   ///< mxj.command(): only with the "commands" permission
        std::function<bool()> shouldStop;                       ///< checked while the script runs
    };

    /** One running MXJ plugin script in its own sandboxed Lua 5.4 state
        (master plan, section 7: "the plugin runs in a restricted
        environment"). See docs/plugins/lua-api.md for what scripts see.

        The sandbox has the base, string, table, math, utf8 and coroutine
        libraries, os.clock, os.time and os.date, and the `mxj` table. There is
        no file, network, process or module access, no bytecode loading, a
        memory limit (512 MB) and a time limit per call.

        Never used on the audio thread. One thread at a time.
    */
    class LuaScript
    {
    public:
        LuaScript (LuaPluginInfo info, LuaHost host);
        ~LuaScript();

        /** Runs the script's main chunk. Returns false with an error ("main.lua:12: ..."). */
        bool load (const juce::String& source, juce::String& error);

        bool hasFunction (const char* name) const;

        /** The audio a script works on: `channels` of `frames` samples, read and
            written in place. `position` is where this block starts within the
            whole selection (in frames). */
        struct Block
        {
            float* const* channels = nullptr;
            int numChannels = 0;
            int numFrames = 0;
            double sampleRate = 48000.0;
            juce::int64 position = 0;
            juce::int64 totalFrames = -1; ///< length of the whole selection, -1 if unknown
        };

        /** Calls `function (block, params)`. */
        bool callWithBlock (const char* function, Block& block, const std::vector<float>& values, juce::String& error);

        /** Calls `function (params)` (or `function (sampleRate, channels, params)` for "prepare").
            `result` gets a string the function returns. */
        bool call (const char* function, const std::vector<float>& values, juce::String& result, juce::String& error);
        bool callPrepare (double sampleRate, int numChannels, const std::vector<float>& values, juce::String& error);

        /** Seconds one call may run before it is stopped (0 = no limit). */
        void setTimeLimit (double seconds) noexcept { timeLimit = seconds; }

        const LuaPluginInfo& getInfo() const noexcept { return info; }

        struct State;

        // Used by the Lua functions of the mxj table.
        bool stopRequested() const;
        void hostLog (const juce::String&);
        void hostMessage (const juce::String&);
        bool hostCommand (const juce::String& id);

    private:
        bool protectedCall (int numArgs, int numResults, juce::String& error);
        void pushParams (const std::vector<float>& values);

        const LuaPluginInfo info;
        LuaHost host;
        std::unique_ptr<State> state;
        double timeLimit = 0.0;
    };
}
