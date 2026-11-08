#pragma once

#include <QDialog>
#include <cstdint>

class QComboBox;
class ToggleCheckBox;

// Modal editor for the recording options that are not part of the always
// visible video card: bitrate, HDR tone mapping and the camera device. The
// dialog only edits a copy; MainWindow keeps the authoritative values and
// applies them to the next recording after `exec()` returns Accepted.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(int64_t bitrate, bool hdr, int camera_index,
                   QWidget* parent = nullptr);

    [[nodiscard]] int64_t bitrate() const;
    [[nodiscard]] bool hdr() const;
    [[nodiscard]] int cameraIndex() const;

private:
    QComboBox* bitrate_combo_ = nullptr;
    ToggleCheckBox* hdr_check_ = nullptr;
    QComboBox* camera_combo_ = nullptr;
};
