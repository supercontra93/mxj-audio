// SPDX-License-Identifier: AGPL-3.0-only
#include "plugins/lua/LuaScript.h"
#include "core/Version.h"

// Lua is compiled as C++ (see cmake/MxjDependencies.cmake), so no extern "C" here.
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <cstdlib>
#include <cstring>

namespace mxj::plugins
{
    struct LuaScript::State
    {
        lua_State* L = nullptr;
        LuaScript* owner = nullptr;
        size_t used = 0;
        size_t limit = (size_t) 512 * 1024 * 1024;
        juce::uint32 deadline = 0; ///< 0 = none
    };

    namespace
    {
        constexpr const char* blockType = "mxj.block";

        LuaScript::State* stateOf (lua_State* L)
        {
            LuaScript::State* s = nullptr;
            std::memcpy (&s, lua_getextraspace (L), sizeof (s));
            return s;
        }

        void* allocate (void* ud, void* ptr, size_t oldSize, size_t newSize)
        {
            auto* s = static_cast<LuaScript::State*> (ud);
            const auto old = ptr != nullptr ? oldSize : 0;

            if (newSize == 0)
            {
                s->used -= old;
                std::free (ptr);
                return nullptr;
            }

            if (s->used - old + newSize > s->limit)
                return nullptr; // Lua reports "not enough memory"

            auto* p = std::realloc (ptr, newSize);
            if (p != nullptr)
                s->used = s->used - old + newSize;

            return p;
        }

        /** Stops scripts that run too long or were cancelled. */
        void hook (lua_State* L, lua_Debug*)
        {
            auto* s = stateOf (L);
            if (s->deadline != 0 && juce::Time::getMillisecondCounter() > s->deadline)
                luaL_error (L, "the plugin took too long and was stopped");

            if (s->owner != nullptr && s->owner->stopRequested())
                luaL_error (L, "stopped");
        }

        int traceback (lua_State* L)
        {
            const char* message = lua_tostring (L, 1);
            if (message == nullptr)
                message = luaL_tolstring (L, 1, nullptr);

            luaL_traceback (L, L, message, 1);
            return 1;
        }

        juce::String joinArguments (lua_State* L)
        {
            juce::String text;
            const auto n = lua_gettop (L);
            for (int i = 1; i <= n; ++i)
            {
                if (i > 1)
                    text << "  ";

                size_t length = 0;
                const auto* s = luaL_tolstring (L, i, &length);
                text << juce::String::fromUTF8 (s, (int) length);
                lua_pop (L, 1);
            }

            return text;
        }

        //==============================================================================
        // The block: audio of one call, read and written in place.
        struct BlockRef
        {
            LuaScript::Block* block = nullptr; ///< nullptr once the call is over
        };

        LuaScript::Block& checkBlock (lua_State* L)
        {
            auto* ref = static_cast<BlockRef*> (luaL_checkudata (L, 1, blockType));
            if (ref->block == nullptr)
                luaL_error (L, "this block can only be used during the call it was given to");

            return *ref->block;
        }

        float* sampleAt (lua_State* L, LuaScript::Block& b)
        {
            const auto channel = (int) luaL_checkinteger (L, 2);
            const auto index = luaL_checkinteger (L, 3);

            if (channel < 1 || channel > b.numChannels)
                luaL_error (L, "channel %d does not exist (the block has %d)", channel, b.numChannels);
            if (index < 1 || index > b.numFrames)
                luaL_error (L, "sample %d is outside the block (1 to %d)", (int) index, b.numFrames);

            return b.channels[channel - 1] + (index - 1);
        }

        int blockGet (lua_State* L)
        {
            auto& b = checkBlock (L);
            lua_pushnumber (L, (lua_Number) *sampleAt (L, b));
            return 1;
        }

        int blockSet (lua_State* L)
        {
            auto& b = checkBlock (L);
            auto* sample = sampleAt (L, b);
            const auto value = (float) luaL_checknumber (L, 4);
            *sample = std::isfinite (value) ? value : 0.0f;
            return 0;
        }

        int blockIndex (lua_State* L)
        {
            auto& b = checkBlock (L);
            const juce::String key (luaL_checkstring (L, 2));

            if (key == "get")             lua_pushcfunction (L, blockGet);
            else if (key == "set")        lua_pushcfunction (L, blockSet);
            else if (key == "channels")   lua_pushinteger (L, b.numChannels);
            else if (key == "frames")     lua_pushinteger (L, b.numFrames);
            else if (key == "sampleRate") lua_pushnumber (L, b.sampleRate);
            else if (key == "position")   lua_pushinteger (L, (lua_Integer) b.position);
            else if (key == "length")     lua_pushinteger (L, (lua_Integer) b.totalFrames);
            else                          lua_pushnil (L);

            return 1;
        }

        //==============================================================================
        // The mxj table.
        int mxjLog (lua_State* L)
        {
            if (auto* s = stateOf (L); s->owner != nullptr)
                s->owner->hostLog (joinArguments (L));
            return 0;
        }

        int mxjMessage (lua_State* L)
        {
            if (auto* s = stateOf (L); s->owner != nullptr)
                s->owner->hostMessage (joinArguments (L));
            return 0;
        }

        int mxjCommand (lua_State* L)
        {
            const juce::String id (luaL_checkstring (L, 1));
            auto* s = stateOf (L);

            if (s->owner == nullptr || ! s->owner->getInfo().needsCommands())
                return luaL_error (L, "mxj.command needs the \"commands\" permission in plugin.json");

            lua_pushboolean (L, s->owner->hostCommand (id));
            return 1;
        }

        int mxjDbToGain (lua_State* L)
        {
            const auto db = luaL_checknumber (L, 1);
            lua_pushnumber (L, db <= -150.0 ? 0.0 : std::pow (10.0, db / 20.0));
            return 1;
        }

        int mxjGainToDb (lua_State* L)
        {
            const auto gain = std::abs (luaL_checknumber (L, 1));
            lua_pushnumber (L, gain <= 1.0e-8 ? -160.0 : 20.0 * std::log10 (gain));
            return 1;
        }

        void openSandbox (lua_State* L)
        {
            luaL_requiref (L, LUA_GNAME, luaopen_base, 1);
            luaL_requiref (L, LUA_TABLIBNAME, luaopen_table, 1);
            luaL_requiref (L, LUA_STRLIBNAME, luaopen_string, 1);
            luaL_requiref (L, LUA_MATHLIBNAME, luaopen_math, 1);
            luaL_requiref (L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
            luaL_requiref (L, LUA_COLIBNAME, luaopen_coroutine, 1);
            lua_settop (L, 0);

            // No files, no loading code from text or bytecode at run time.
            for (const auto* name : { "dofile", "loadfile", "load", "require", "collectgarbage" })
            {
                lua_pushnil (L);
                lua_setglobal (L, name);
            }

            lua_getglobal (L, "string");
            lua_pushnil (L);
            lua_setfield (L, -2, "dump");
            lua_pop (L, 1);

            // os: only the clock and dates.
            luaL_requiref (L, LUA_OSLIBNAME, luaopen_os, 0); // the full library, not made global
            lua_newtable (L);
            for (const auto* name : { "clock", "time", "date", "difftime" })
            {
                lua_getfield (L, -2, name);
                lua_setfield (L, -2, name);
            }
            lua_setglobal (L, "os");
            lua_pop (L, 1); // the full os library

            lua_pushcfunction (L, mxjLog);
            lua_setglobal (L, "print");

            // mxj
            lua_newtable (L);
            lua_pushinteger (L, luaApiVersion);
            lua_setfield (L, -2, "apiVersion");
            lua_pushstring (L, version::string);
            lua_setfield (L, -2, "version");
            lua_pushcfunction (L, mxjLog);
            lua_setfield (L, -2, "log");
            lua_pushcfunction (L, mxjMessage);
            lua_setfield (L, -2, "message");
            lua_pushcfunction (L, mxjCommand);
            lua_setfield (L, -2, "command");
            lua_pushcfunction (L, mxjDbToGain);
            lua_setfield (L, -2, "dbToGain");
            lua_pushcfunction (L, mxjGainToDb);
            lua_setfield (L, -2, "gainToDb");
            lua_setglobal (L, "mxj");

            // The block type.
            luaL_newmetatable (L, blockType);
            lua_pushcfunction (L, blockIndex);
            lua_setfield (L, -2, "__index");
            lua_pushstring (L, "block");
            lua_setfield (L, -2, "__name");
            lua_pop (L, 1);
        }
    }

    //==============================================================================
    LuaScript::LuaScript (LuaPluginInfo i, LuaHost h) : info (std::move (i)), host (std::move (h)), state (std::make_unique<State>())
    {
        state->owner = this;
        state->L = lua_newstate (allocate, state.get());
        if (state->L == nullptr)
            return;

        auto* s = state.get();
        std::memcpy (lua_getextraspace (state->L), &s, sizeof (s));
        lua_sethook (state->L, hook, LUA_MASKCOUNT, 10000);
        openSandbox (state->L);
    }

    LuaScript::~LuaScript()
    {
        if (state->L != nullptr)
            lua_close (state->L);
    }

    bool LuaScript::stopRequested() const
    {
        return host.shouldStop != nullptr && host.shouldStop();
    }

    void LuaScript::hostLog (const juce::String& text)
    {
        if (host.log != nullptr)
            host.log (text);
    }

    void LuaScript::hostMessage (const juce::String& text)
    {
        if (host.message != nullptr)
            host.message (text);
        else
            hostLog (text);
    }

    bool LuaScript::hostCommand (const juce::String& id)
    {
        return host.runCommand != nullptr && host.runCommand (id);
    }

    bool LuaScript::protectedCall (int numArgs, int numResults, juce::String& error)
    {
        auto* L = state->L;
        const auto base = lua_gettop (L) - numArgs;
        lua_pushcfunction (L, traceback);
        lua_insert (L, base);

        state->deadline = timeLimit > 0.0 ? juce::Time::getMillisecondCounter() + (juce::uint32) (timeLimit * 1000.0) : 0;
        const auto status = lua_pcall (L, numArgs, numResults, base);
        state->deadline = 0;
        lua_remove (L, base);

        if (status != LUA_OK)
        {
            error = juce::String::fromUTF8 (lua_tostring (L, -1) != nullptr ? lua_tostring (L, -1) : "unknown error");
            lua_pop (L, 1);
            return false;
        }

        return true;
    }

    bool LuaScript::load (const juce::String& source, juce::String& error)
    {
        if (state->L == nullptr)
        {
            error = "Lua could not start";
            return false;
        }

        auto* L = state->L;
        const auto utf8 = source.toStdString();
        const auto chunkName = ("=" + info.mainFile).toStdString();

        if (luaL_loadbufferx (L, utf8.data(), utf8.size(), chunkName.c_str(), "t") != LUA_OK)
        {
            error = juce::String::fromUTF8 (lua_tostring (L, -1));
            lua_pop (L, 1);
            return false;
        }

        return protectedCall (0, 0, error);
    }

    bool LuaScript::hasFunction (const char* name) const
    {
        if (state->L == nullptr)
            return false;

        lua_getglobal (state->L, name);
        const bool isFunction = lua_isfunction (state->L, -1);
        lua_pop (state->L, 1);
        return isFunction;
    }

    void LuaScript::pushParams (const std::vector<float>& values)
    {
        auto* L = state->L;
        lua_createtable (L, 0, (int) info.parameters.size());

        for (size_t i = 0; i < info.parameters.size(); ++i)
        {
            const auto& p = info.parameters[i];
            const auto v = i < values.size() ? values[i] : p.defaultValue;

            if (p.kind == dsp::ParameterInfo::Kind::toggle)
                lua_pushboolean (L, v > 0.5f);
            else if (p.kind == dsp::ParameterInfo::Kind::choice)
                lua_pushinteger (L, (lua_Integer) std::lround (v));
            else
                lua_pushnumber (L, (lua_Number) v);

            lua_setfield (L, -2, p.id.toRawUTF8());
        }
    }

    bool LuaScript::callWithBlock (const char* function, Block& block, const std::vector<float>& values, juce::String& error)
    {
        auto* L = state->L;
        if (L == nullptr)
            return false;

        lua_getglobal (L, function);
        if (! lua_isfunction (L, -1))
        {
            lua_pop (L, 1);
            error = "The plugin has no function " + juce::String (function) + "()";
            return false;
        }

        auto* ref = static_cast<BlockRef*> (lua_newuserdatauv (L, sizeof (BlockRef), 0));
        ref->block = &block;
        luaL_setmetatable (L, blockType);

        // Keep the block reachable so it can be switched off after the call.
        lua_pushvalue (L, -1);
        const auto refIndex = luaL_ref (L, LUA_REGISTRYINDEX);

        pushParams (values);
        const auto ok = protectedCall (2, 0, error);

        lua_rawgeti (L, LUA_REGISTRYINDEX, refIndex);
        static_cast<BlockRef*> (lua_touserdata (L, -1))->block = nullptr;
        lua_pop (L, 1);
        luaL_unref (L, LUA_REGISTRYINDEX, refIndex);

        return ok;
    }

    bool LuaScript::call (const char* function, const std::vector<float>& values, juce::String& result, juce::String& error)
    {
        auto* L = state->L;
        if (L == nullptr)
            return false;

        lua_getglobal (L, function);
        if (! lua_isfunction (L, -1))
        {
            lua_pop (L, 1);
            error = "The plugin has no function " + juce::String (function) + "()";
            return false;
        }

        pushParams (values);
        if (! protectedCall (1, 1, error))
            return false;

        if (lua_isstring (L, -1))
            result = juce::String::fromUTF8 (lua_tostring (L, -1));

        lua_pop (L, 1);
        return true;
    }

    bool LuaScript::callPrepare (double sampleRate, int numChannels, const std::vector<float>& values, juce::String& error)
    {
        auto* L = state->L;
        lua_getglobal (L, "prepare");
        if (! lua_isfunction (L, -1))
        {
            lua_pop (L, 1);
            return true; // optional
        }

        lua_pushnumber (L, sampleRate);
        lua_pushinteger (L, numChannels);
        pushParams (values);
        return protectedCall (3, 0, error);
    }
}
