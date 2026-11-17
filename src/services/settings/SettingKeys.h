// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

/** All setting keys in one place. Keys are stored in settings.json, so never
    rename one without adding a migration in Settings::migrate(). */
namespace mxj::keys
{
    inline constexpr const char* language            = "ui.language";        // "tr" or "en"
    inline constexpr const char* uiScale             = "ui.scale";           // 1.0 = 100 %
    inline constexpr const char* windowState         = "ui.windowState";     // JUCE window state string
    inline constexpr const char* updateCheckOnStart  = "updates.checkOnStartup";
    inline constexpr const char* updateChannel       = "updates.channel";    // "stable" or "beta"
    inline constexpr const char* audioDeviceState    = "audio.deviceState";  // JUCE AudioDeviceManager XML, one line
    inline constexpr const char* lastOpenFolder      = "files.lastOpenFolder";
}
