#pragma once

#include <QString>
#include <cstdint>

#include "core/audio/AudioCodec.h"

// Everything the application remembers between runs. MainWindow keeps the
// authoritative values while a recording is being configured; the settings
// dialog edits a copy and MainWindow writes the accepted result back with
// SaveAppSettings().
struct AppSettings {
    // Recording options (recording page of the settings dialog).
    QString container_extension = QStringLiteral("mp4");
    int64_t video_bitrate = 50000000;
    bool hdr = false;
    int camera_index = 0;
    hmmob::AudioCodec audio_codec = hmmob::AudioCodec::Aac;
    int audio_sample_rate = 48000;
    int audio_bitrate = 192000;

    // Options that live on the main window itself.
    QString video_codec = QStringLiteral("hevc");
    int fps = 60;
    int capture_mode = 0;  // 0 screen, 1 region, 2 window, 3 camera
    QString mic_device_id;
    QString system_device_id;
    bool microphone_enabled = true;
    bool system_audio_enabled = true;

    // Output settings.
    // Empty means the platform Movies directory (resolved lazily so a
    // relocated/renamed user profile keeps working).
    QString output_directory;
    bool auto_timestamp_filename = true;

    // Interface language. Only 简体中文 exists today; the value is stored so
    // the choice survives even when more languages are added.
    QString language = QStringLiteral("zh-CN");

    // General settings.
    bool auto_start = false;
    bool minimize_to_tray_on_start = false;
    bool close_to_tray = false;
    bool hide_notifications_while_recording = false;
    bool show_recording_complete_notification = true;
};

// Reads the persisted preferences (org "Hmmob", app "Hmmob EV"). Missing or
// malformed values fall back to the defaults above.
AppSettings LoadAppSettings();

// Serializes `settings` with QSettings. Does not touch the auto-start
// registry; call ApplyAutoStart() separately so the two stay independent.
void SaveAppSettings(const AppSettings& settings);

// Writes or removes the HKCU Run entry "HmmobEV" for the current executable.
void ApplyAutoStart(bool enabled);

// QStandardPaths::MoviesLocation with a temp-directory fallback.
QString DefaultOutputDirectory();
