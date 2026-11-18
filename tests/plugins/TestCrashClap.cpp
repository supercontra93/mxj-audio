// SPDX-License-Identifier: AGPL-3.0-only
//
// A CLAP file that crashes as soon as it is opened. Built only for the tests:
// scanning it must block it without taking MXJ AUDIO down.

#include <clap/clap.h>

#include <cstdlib>

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    [] (const char*) -> bool { std::abort(); },
    [] {},
    [] (const char*) -> const void* { return nullptr; } };
