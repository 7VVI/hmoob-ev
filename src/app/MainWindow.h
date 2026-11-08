#pragma once

#include <QElapsedTimer>
#include <QRect>
#include <QString>
#include <QWidget>
#include <memory>
#include <vector>

#include "app/AppSettings.h"
#include "app/IconRenderer.h"
#include "core/audio/AudioCodec.h"
#include "core/gfx/D3D11Device.h"
#include "core/preview/DesktopPreview.h"
#include "core/recorder/Recorder.h"

class QAbstractButton;
class QAction;
class QCloseEvent;
class QEvent;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QTimer;
class QButtonGroup;
class LevelMeter;
class ModeTile;
class NavTabButton;
class PreviewWidget;
class RecordButton;
class StyledComboBox;
class QSystemTrayIcon;
class TitleBar;

// Optional capture-mode selection for an automated smoke run. Region and
// window values bypass the interactive selector/picker.
struct SmokeOptions {
    QString mode = QStringLiteral("screen");  // screen|region|window|camera
    QRect region;                             // display-local pixels
    int window = 0;                           // 1-based capturable window
    int camera = 0;
};

class MainWindow : public QWidget {
    Q_OBJECT

public:
    // `automation` suppresses the tray icon and every other desktop side
    // effect of the general settings (used by --smoke and the settings
    // screenshot hook, which must stay deterministic).
    explicit MainWindow(bool automation = false, QWidget* parent = nullptr);
    ~MainWindow() override;

    // True when the saved preferences ask for a tray-only start. Automation
    // runs always return false so they never begin hidden.
    [[nodiscard]] bool StartHiddenToTray() const;

    // Verification hook: renders every settings page to <directory> and
    // exits the application. Used by --settings-screenshot.
    void CaptureSettingsScreenshots(const QString& directory);

    // Automated verification hook: starts a recording after the window is
    // shown, stops it after `seconds`, writes "<path>.smoke.txt" with the
    // stats, then exits 0 (success) or 1 (failure).
    void StartSmoke(int seconds, const QString& path,
                    const SmokeOptions& options = SmokeOptions());

protected:
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message,
                     qintptr* result) override;

private:
    void BuildUi();
    QWidget* BuildNavRow();
    QWidget* BuildContentCard();
    QWidget* BuildAudioCard();
    QWidget* BuildVideoCard();
    QWidget* BuildBottomBar();
    QLabel* MakeIconLabel(Icon icon, int size, const QColor& color,
                          QWidget* parent);
    void PopulateDisplays();
    void PopulateCameras();
    void PopulateAudioDevices();
    void ConnectSignals();

    // Switches the four capture modes, keeping the nav tabs, mode tiles and
    // the idle preview in sync.
    void SetMode(int mode);
    bool CanStart() const;
    QRect SelectedDisplayRect() const;

    void SelectDisplay(int display_index);
    void UpdateBadgeText();
    // timeout_ms > 0 clears the line after that delay; 0 keeps it until
    // another message replaces it. Preview errors never use this line.
    void ShowStatus(const QString& text, bool error, int timeout_ms);
    void ShowPreviewError(const QString& detail);

    void StartIdlePreview();
    void Tick();
    void RefreshStats();
    bool StartRecording();
    void StopRecording();
    void FinalizeRecording();
    void FinishSmoke(const hmmob::RecorderStats& stats, const QString& error);

    hmmob::RecorderConfig BuildConfig() const;
    QString DefaultOutputPath() const;
    QString RecordingCompatibilityError(const QString& path) const;
    void BrowseOutput();

    void SetConfigEnabled(bool enabled);
    void SetError(const QString& error);
    void UpdateStatsLabels(const hmmob::RecorderStats& stats);
    void UpdateAudioUi(bool microphone);
    void OpenSettings();

    void SyncSettingsFromUi();
    void ApplyPersistedSettings();
    void PersistSettings();
    void CreateTray();
    void UpdateTrayMenu();
    void ShowMainWindow();
    void ShowTrayMessage(const QString& title, const QString& text);

    void UpdateStatusGeometry();
    void ToggleMaximize();
    void SyncMaximizedState();
    void SetIdleSuspended(bool suspended);
    void ApplyWindowEffects();

    hmmob::Recorder recorder_;
    hmmob::DesktopPreview preview_source_;
    std::vector<hmmob::DisplayInfo> displays_;

    AppSettings settings_;
    QSystemTrayIcon* tray_icon_ = nullptr;
    QAction* tray_start_action_ = nullptr;
    QAction* tray_stop_action_ = nullptr;
    bool automation_ = false;
    bool quitting_ = false;

    TitleBar* title_bar_ = nullptr;
    PreviewWidget* preview_ = nullptr;
    NavTabButton* screen_tab_ = nullptr;
    NavTabButton* window_tab_ = nullptr;
    NavTabButton* region_tab_ = nullptr;
    NavTabButton* camera_tab_ = nullptr;

    QButtonGroup* mode_group_ = nullptr;
    ModeTile* full_tile_ = nullptr;
    int selected_mode_ = 0;
    int selected_display_index_ = -1;

    StyledComboBox* resolution_combo_ = nullptr;
    StyledComboBox* fps_combo_ = nullptr;
    StyledComboBox* codec_combo_ = nullptr;
    StyledComboBox* mic_device_combo_ = nullptr;
    StyledComboBox* system_device_combo_ = nullptr;
    QPushButton* mic_toggle_ = nullptr;
    QPushButton* system_toggle_ = nullptr;
    LevelMeter* mic_meter_ = nullptr;
    LevelMeter* system_meter_ = nullptr;
    QPushButton* settings_button_ = nullptr;

    // Authoritative recording options edited by the settings dialog. They
    // survive the dialog and apply to the next recording.
    qint64 bitrate_ = 50000000;
    bool hdr_ = false;
    int camera_index_ = 0;
    int camera_device_count_ = 0;
    // Container extension ("mp4", "mov", ...) chosen in the settings dialog.
    // The muxer picks the actual container from the output path; this value
    // drives the auto-generated path and the compatibility checks.
    QString container_ = QStringLiteral("mp4");
    hmmob::AudioCodec audio_codec_ = hmmob::AudioCodec::Aac;
    int audio_sample_rate_ = 48000;
    int audio_bitrate_ = 192000;

    QLineEdit* output_edit_ = nullptr;
    QPushButton* browse_button_ = nullptr;
    RecordButton* start_button_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* duration_label_ = nullptr;
    QLabel* size_label_ = nullptr;
    QTimer* poll_timer_ = nullptr;
    QTimer* status_timer_ = nullptr;

    bool has_display_ = false;
    bool preview_started_ = false;
    bool recording_ = false;
    bool stop_requested_ = false;
    bool closing_ = false;
    bool maximized_ = false;
    bool minimized_ = false;
    int tick_count_ = 0;
    // Resolved size of the active (or last) recording, used by the badge.
    int capture_width_ = 0;
    int capture_height_ = 0;
    QString output_path_;
    // Last path this window generated itself (as opposed to one the user
    // typed or picked). Decides whether the next run may regenerate a fresh
    // filename instead of keeping the user's choice.
    QString auto_path_;
    QString last_error_;
    QString preview_error_detail_;
    std::shared_ptr<const hmmob::PreviewFrame> last_preview_;
    QElapsedTimer save_watchdog_;
    QElapsedTimer record_started_;

    bool smoke_ = false;
    bool smoke_finished_ = false;
    int smoke_seconds_ = 0;
    QString smoke_path_;
    QString smoke_mode_ = QStringLiteral("screen");
    QRect smoke_region_;
    int smoke_window_ = 0;
    int smoke_camera_ = 0;
    QString smoke_window_title_;
    QTimer* smoke_watchdog_ = nullptr;
};
