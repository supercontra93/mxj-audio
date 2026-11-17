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
    inline constexpr const char* lastSaveFolder      = "files.lastSaveFolder";
    inline constexpr const char* recordInputDevice   = "recording.inputDevice";   // empty = system default
    inline constexpr const char* recordFirstChannel  = "recording.firstChannel";  // 0-based input channel
    inline constexpr const char* recordChannels      = "recording.channels";      // 1 = mono, 2 = stereo
    inline constexpr const char* exportFormat        = "export.format";           // "wav", "aiff", "flac", "ogg", "opus"
    inline constexpr const char* exportBits          = "export.bitsPerSample";    // 16, 24, 32 (32 = float WAV)
    inline constexpr const char* exportQuality       = "export.quality";          // quality option index, -1 = default
}
