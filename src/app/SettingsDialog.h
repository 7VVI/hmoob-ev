#pragma once

#include <QDialog>
#include <QVector>

#include "app/AppSettings.h"

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
class ToggleCheckBox;

// Modal editor for every preference (general, recording, output). The dialog
// only edits a copy; MainWindow keeps the authoritative AppSettings and calls
// SaveAppSettings() + applies the accepted result after exec() returns
// Accepted.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const AppSettings& settings,
                            QWidget* parent = nullptr);

    // Values currently shown in the dialog. Fields that have no control here
    // (codec, fps, capture mode, device ids) are carried through unchanged.
    [[nodiscard]] AppSettings settings() const;

    // Switches the visible page; used by the screenshot automation hook.
    void SelectPage(int page);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QWidget* BuildHeader();
    QWidget* BuildNav();
    QWidget* BuildGeneralPage();
    QWidget* BuildRecordingPage();
    QWidget* BuildShortcutsPage();
    QWidget* BuildOutputPage();
    QWidget* BuildAboutPage();
    QWidget* BuildFooter();
    void UpdateCompatibility();
    void SetThemeMode(int mode, bool announce);
    void ResetToDefaults();
    void BrowseOutputDirectory();
    AppSettings Collect() const;

    AppSettings base_;

    QStackedWidget* stack_ = nullptr;
    QVector<QAbstractButton*> nav_buttons_;

    // 通用设置
    ToggleCheckBox* auto_start_check_ = nullptr;
    ToggleCheckBox* start_hidden_check_ = nullptr;
    ToggleCheckBox* close_to_tray_check_ = nullptr;
    ToggleCheckBox* hide_notifications_check_ = nullptr;
    ToggleCheckBox* show_complete_check_ = nullptr;
    QButtonGroup* theme_group_ = nullptr;
    QPushButton* light_button_ = nullptr;
    QPushButton* dark_button_ = nullptr;
    QPushButton* system_button_ = nullptr;
    QLabel* theme_hint_ = nullptr;
    QTimer* theme_hint_timer_ = nullptr;
    QComboBox* language_combo_ = nullptr;

    // 录制设置
    QComboBox* container_combo_ = nullptr;
    QComboBox* bitrate_combo_ = nullptr;
    ToggleCheckBox* hdr_check_ = nullptr;
    QComboBox* audio_codec_combo_ = nullptr;
    QComboBox* sample_rate_combo_ = nullptr;
    QComboBox* audio_bitrate_combo_ = nullptr;
    QComboBox* camera_combo_ = nullptr;
    QLabel* compatibility_hint_ = nullptr;

    // 输出设置
    QLineEdit* directory_edit_ = nullptr;
    ToggleCheckBox* auto_timestamp_check_ = nullptr;

    QPushButton* save_button_ = nullptr;
    bool hevc_ = false;
};
