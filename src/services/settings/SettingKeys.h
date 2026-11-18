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
    inline constexpr const char* textSize            = "ui.textSize";             // text scale: 1.0 = phase 0-2 size, default 1.15
    inline constexpr const char* showMeters          = "ui.showMeters";           // level meter panel on the right
    inline constexpr const char* showSpectrum        = "ui.showSpectrum";         // spectrum analyser under the waveform
    inline constexpr const char* showRack            = "ui.showRack";             // effect rack panel on the left
    inline constexpr const char* effectRack          = "effects.rack";            // JSON of the rack (dsp::chainToVar)
    inline constexpr const char* newFileSampleRate   = "newFile.sampleRate";      // Hz, last choice in File > New Audio File
    inline constexpr const char* newFileChannels     = "newFile.channels";        // 1 = mono, 2 = stereo
    inline constexpr const char* showSpectral        = "ui.showSpectral";         // spectrogram under the waveform (phase 4)
    inline constexpr const char* spectralScale       = "spectral.scale";          // "linear", "log" or "mel"
    inline constexpr const char* spectralResolution  = "spectral.fftSize";        // 512, 1024, 2048, 4096 or 8192
    inline constexpr const char* spectralColours     = "spectral.colours";        // "amber", "gray" or "rainbow"
    inline constexpr const char* showMixer           = "ui.showMixer";            // mixer under the multitrack time line (phase 5)
    inline constexpr const char* showTrackEffects    = "ui.showTrackEffects";     // track effects panel right of the time line
    inline constexpr const char* sessionSnapping     = "session.snap";            // snap clips to edges, markers and the cursor
}
