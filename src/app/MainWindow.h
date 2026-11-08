#pragma once

#include <QString>
#include <QWidget>
#include <memory>

#include "core/recorder/Recorder.h"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class PreviewWidget;

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

private:
    void BuildUi();
    void PopulateDisplays();
    void ConnectSignals();

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
    void SetAudioLabel(const hmmob::RecorderStats& stats);
    void UpdateStatsLabels(const hmmob::RecorderStats& stats, bool update_status);

    hmmob::Recorder recorder_;

    PreviewWidget* preview_ = nullptr;
    QComboBox* display_combo_ = nullptr;
    QComboBox* fps_combo_ = nullptr;
    QComboBox* codec_combo_ = nullptr;
    QComboBox* bitrate_combo_ = nullptr;
    QCheckBox* hdr_check_ = nullptr;
    QCheckBox* system_audio_check_ = nullptr;
    QDoubleSpinBox* system_gain_ = nullptr;
    QCheckBox* mic_check_ = nullptr;
    QDoubleSpinBox* mic_gain_ = nullptr;
    QLineEdit* output_edit_ = nullptr;
    QPushButton* browse_button_ = nullptr;
    QPushButton* start_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* audio_label_ = nullptr;
    QLabel* error_label_ = nullptr;
    QTimer* poll_timer_ = nullptr;

    bool has_display_ = false;
    bool recording_ = false;
    bool stop_requested_ = false;
    int tick_count_ = 0;
    QString output_path_;
    QString last_error_;
    std::shared_ptr<const hmmob::PreviewFrame> last_preview_;

    bool smoke_ = false;
    bool smoke_finished_ = false;
    int smoke_seconds_ = 0;
    QString smoke_path_;
};
