// SPDX-License-Identifier: AGPL-3.0-only
//
// "MXJ Test Gain": the smallest useful CLAP effect, built only for the tests of
// our CLAP host. Multiplies stereo audio by a gain. Its state is the gain in
// dB (a float), so tests can check that state is saved and loaded.

#include <clap/clap.h>

#include <cmath>
#include <cstring>

namespace
{
    struct Gain
    {
        clap_plugin_t plugin {};
        const clap_host_t* host = nullptr;
        float gainDb = -6.0f;
    };

    Gain* self (const clap_plugin_t* p) { return static_cast<Gain*> (p->plugin_data); }

    const char* features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr };

    const clap_plugin_descriptor_t descriptor {
        CLAP_VERSION_INIT, "io.github.supercontra93.mxj.testgain", "MXJ Test Gain", "MXJ AUDIO",
        "https://github.com/supercontra93/mxj-audio", "", "", "1.0.0", "Test plugin: a gain", features };

    // Audio ports: one stereo in, one stereo out.
    const clap_plugin_audio_ports_t ports {
        [] (const clap_plugin_t*, bool) -> uint32_t { return 1; },
        [] (const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) -> bool
        {
            if (index != 0)
                return false;
            info->id = isInput ? 0 : 1;
            std::strcpy (info->name, isInput ? "In" : "Out");
            info->flags = CLAP_AUDIO_PORT_IS_MAIN;
            info->channel_count = 2;
            info->port_type = CLAP_PORT_STEREO;
            info->in_place_pair = CLAP_INVALID_ID;
            return true;
        } };

    // State: the gain in dB.
    const clap_plugin_state_t state {
        [] (const clap_plugin_t* p, const clap_ostream_t* out) -> bool
        {
            return out->write (out, &self (p)->gainDb, sizeof (float)) == (int64_t) sizeof (float);
        },
        [] (const clap_plugin_t* p, const clap_istream_t* in) -> bool
        {
            float v = 0.0f;
            if (in->read (in, &v, sizeof (float)) != (int64_t) sizeof (float))
                return false;
            self (p)->gainDb = v;
            return true;
        } };

    const clap_plugin_latency_t latency { [] (const clap_plugin_t*) -> uint32_t { return 0; } };

    const clap_plugin_t* create (const clap_host_t* host)
    {
        auto* g = new Gain();
        g->host = host;
        g->plugin.desc = &descriptor;
        g->plugin.plugin_data = g;
        g->plugin.init = [] (const clap_plugin_t*) { return true; };
        g->plugin.destroy = [] (const clap_plugin_t* p) { delete self (p); };
        g->plugin.activate = [] (const clap_plugin_t*, double, uint32_t, uint32_t) { return true; };
        g->plugin.deactivate = [] (const clap_plugin_t*) {};
        g->plugin.start_processing = [] (const clap_plugin_t*) { return true; };
        g->plugin.stop_processing = [] (const clap_plugin_t*) {};
        g->plugin.reset = [] (const clap_plugin_t*) {};
        g->plugin.process = [] (const clap_plugin_t* p, const clap_process_t* process) -> clap_process_status
        {
            const auto gain = std::pow (10.0f, self (p)->gainDb / 20.0f);
            const auto& in = process->audio_inputs[0];
            const auto& out = process->audio_outputs[0];
            for (uint32_t c = 0; c < out.channel_count; ++c)
                for (uint32_t i = 0; i < process->frames_count; ++i)
                    out.data32[c][i] = in.data32[c < in.channel_count ? c : 0][i] * gain;
            return CLAP_PROCESS_CONTINUE;
        };
        g->plugin.get_extension = [] (const clap_plugin_t*, const char* id) -> const void*
        {
            if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &ports;
            if (std::strcmp (id, CLAP_EXT_STATE) == 0)       return &state;
            if (std::strcmp (id, CLAP_EXT_LATENCY) == 0)     return &latency;
            return nullptr;
        };
        g->plugin.on_main_thread = [] (const clap_plugin_t*) {};
        return &g->plugin;
    }

    const clap_plugin_factory_t factory {
        [] (const clap_plugin_factory_t*) -> uint32_t { return 1; },
        [] (const clap_plugin_factory_t*, uint32_t index) -> const clap_plugin_descriptor_t* { return index == 0 ? &descriptor : nullptr; },
        [] (const clap_plugin_factory_t*, const clap_host_t* host, const char* id) -> const clap_plugin_t*
        {
            return std::strcmp (id, descriptor.id) == 0 ? create (host) : nullptr;
        } };
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    [] (const char*) { return true; },
    [] {},
    [] (const char* id) -> const void* { return std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; } };
