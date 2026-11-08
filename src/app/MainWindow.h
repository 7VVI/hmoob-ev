#pragma once

#include <QElapsedTimer>
#include <QRect>
#include <QString>
#include <QWidget>
#include <memory>
#include <vector>

#include "app/IconRenderer.h"
#include "core/gfx/D3D11Device.h"
#include "core/preview/DesktopPreview.h"
#include "core/recorder/Recorder.h"

class QAbstractButton;
class QCloseEvent;
class QEvent;
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
class TitleBar;
class ToggleCheckBox;

class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Automated verification hook: starts a recording after the window is
    // shown, stops it after `seconds`, writes "<path>.smoke.txt" with the
    // stats, then exits 0 (success) or 1 (failure).
    void StartSmoke(int seconds, const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
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
    void ConnectSignals();

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
    void BrowseOutput();

    void SetConfigEnabled(bool enabled);
    void SetError(const QString& error);
    void UpdateStatsLabels(const hmmob::RecorderStats& stats);
    void UpdateAudioUi(bool microphone);
    void SetAdvancedExpanded(bool expanded);

    void UpdateStatusGeometry();
    void ToggleMaximize();
    void SyncMaximizedState();
    void SetIdleSuspended(bool suspended);
    void ApplyWindowEffects();

    hmmob::Recorder recorder_;
    hmmob::DesktopPreview preview_source_;
    std::vector<hmmob::DisplayInfo> displays_;

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
    StyledComboBox* display_combo_ = nullptr;
    StyledComboBox* fps_combo_ = nullptr;
    StyledComboBox* codec_combo_ = nullptr;
    StyledComboBox* bitrate_combo_ = nullptr;
    StyledComboBox* mic_device_combo_ = nullptr;
    StyledComboBox* system_device_combo_ = nullptr;
    QPushButton* mic_toggle_ = nullptr;
    QPushButton* system_toggle_ = nullptr;
    LevelMeter* mic_meter_ = nullptr;
    LevelMeter* system_meter_ = nullptr;
    ToggleCheckBox* hdr_check_ = nullptr;
    QWidget* advanced_content_ = nullptr;
    QWidget* display_row_ = nullptr;
    QPushButton* advanced_button_ = nullptr;
    QLabel* advanced_chevron_ = nullptr;

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
    QTimer* smoke_watchdog_ = nullptr;
};
