#pragma once

#include <QDialog>
#include <QString>
#include <cstdint>

#include "core/audio/AudioCodec.h"

class QComboBox;
class QLabel;
class QPushButton;
class ToggleCheckBox;

// Modal editor for the recording options that are not part of the always
// visible cards: container, video bitrate, HDR tone mapping, audio format and
// the camera device. The dialog only edits a copy; MainWindow keeps the
// authoritative values and applies them to the next recording after `exec()`
// returns Accepted.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // `video_codec` is the codec currently selected on the main window
    // ("hevc" or "h264"); it drives the live container compatibility check.
    SettingsDialog(int64_t bitrate, bool hdr, int camera_index,
                   const QString& container_extension,
                   hmmob::AudioCodec audio_codec, int audio_sample_rate,
                   int audio_bitrate, const QString& video_codec,
                   QWidget* parent = nullptr);

    [[nodiscard]] int64_t bitrate() const;
    [[nodiscard]] bool hdr() const;
    [[nodiscard]] int cameraIndex() const;
    [[nodiscard]] QString containerExtension() const;
    [[nodiscard]] hmmob::AudioCodec audioCodec() const;
    [[nodiscard]] int audioSampleRate() const;
    [[nodiscard]] int audioBitrate() const;

private:
    void UpdateCompatibility();

    QComboBox* container_combo_ = nullptr;
    QComboBox* bitrate_combo_ = nullptr;
    ToggleCheckBox* hdr_check_ = nullptr;
    QComboBox* audio_codec_combo_ = nullptr;
    QComboBox* sample_rate_combo_ = nullptr;
    QComboBox* audio_bitrate_combo_ = nullptr;
    QComboBox* camera_combo_ = nullptr;
    QLabel* compatibility_hint_ = nullptr;
    QPushButton* save_button_ = nullptr;
    bool hevc_ = false;
};
