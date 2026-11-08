#include "app/MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdint>
#include <utility>

#include "app/PreviewWidget.h"
#include "core/gfx/D3D11Device.h"

namespace {

QString IdleStatusText() {
    return QStringLiteral("已录 00:00.0 · 0 帧 · 0.0 fps");
}

QString PreviewUnavailableText(const std::string& error) {
    return QStringLiteral("预览不可用：%1").arg(QString::fromStdString(error));
}

QString FormatBytes(int64_t bytes) {
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
    BuildUi();
    PopulateDisplays();
    ConnectSignals();
}

MainWindow::~MainWindow() { preview_source_.Stop(); }

void MainWindow::BuildUi() {
    setWindowTitle(QStringLiteral("Hmmob EV 录制"));
    resize(1000, 800);
    setMinimumSize(900, 700);

    auto* root = new QVBoxLayout(this);

    preview_ = new PreviewWidget(this);
    root->addWidget(preview_, 1);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    display_combo_ = new QComboBox(this);
    form->addRow(QStringLiteral("显示器"), display_combo_);

    fps_combo_ = new QComboBox(this);
    fps_combo_->addItem(QStringLiteral("30"), 30);
    fps_combo_->addItem(QStringLiteral("60"), 60);
    fps_combo_->addItem(QStringLiteral("120"), 120);
    fps_combo_->setCurrentIndex(1);
    form->addRow(QStringLiteral("帧率"), fps_combo_);

    codec_combo_ = new QComboBox(this);
    codec_combo_->addItem(QStringLiteral("HEVC"), QStringLiteral("hevc"));
    codec_combo_->addItem(QStringLiteral("H.264"), QStringLiteral("h264"));
    form->addRow(QStringLiteral("编码"), codec_combo_);

    bitrate_combo_ = new QComboBox(this);
    bitrate_combo_->addItem(QStringLiteral("20 Mbps"),
                            QVariant::fromValue<qlonglong>(20000000));
    bitrate_combo_->addItem(QStringLiteral("50 Mbps"),
                            QVariant::fromValue<qlonglong>(50000000));
    bitrate_combo_->addItem(QStringLiteral("100 Mbps"),
                            QVariant::fromValue<qlonglong>(100000000));
    bitrate_combo_->setCurrentIndex(1);
    form->addRow(QStringLiteral("码率"), bitrate_combo_);

    hdr_check_ = new QCheckBox(QStringLiteral("HDR（色调映射为 SDR）"), this);
    form->addRow(QString(), hdr_check_);

    auto* system_row = new QHBoxLayout;
    system_audio_check_ = new QCheckBox(QStringLiteral("系统声音"), this);
    system_audio_check_->setChecked(true);
    system_gain_ = new QDoubleSpinBox(this);
    system_gain_->setRange(0.0, 4.0);
    system_gain_->setSingleStep(0.1);
    system_gain_->setDecimals(1);
    system_gain_->setValue(1.0);
    system_row->addWidget(system_audio_check_);
    system_row->addWidget(new QLabel(QStringLiteral("增益"), this));
    system_row->addWidget(system_gain_);
    system_row->addStretch(1);
    form->addRow(QStringLiteral("系统音频"), system_row);

    auto* mic_row = new QHBoxLayout;
    mic_check_ = new QCheckBox(QStringLiteral("麦克风"), this);
    mic_check_->setChecked(true);
    mic_gain_ = new QDoubleSpinBox(this);
    mic_gain_->setRange(0.0, 4.0);
    mic_gain_->setSingleStep(0.1);
    mic_gain_->setDecimals(1);
    mic_gain_->setValue(1.0);
    mic_row->addWidget(mic_check_);
    mic_row->addWidget(new QLabel(QStringLiteral("增益"), this));
    mic_row->addWidget(mic_gain_);
    mic_row->addStretch(1);
    form->addRow(QStringLiteral("麦克风"), mic_row);

    auto* output_row = new QHBoxLayout;
    auto_path_ = DefaultOutputPath();
    output_edit_ = new QLineEdit(auto_path_, this);
    output_edit_->setPlaceholderText(QStringLiteral("输出 MP4 路径"));
    browse_button_ = new QPushButton(QStringLiteral("浏览…"), this);
    output_row->addWidget(output_edit_, 1);
    output_row->addWidget(browse_button_);
    form->addRow(QStringLiteral("输出文件"), output_row);

    root->addLayout(form);

    auto* button_row = new QHBoxLayout;
    start_button_ = new QPushButton(QStringLiteral("开始录制"), this);
    stop_button_ = new QPushButton(QStringLiteral("停止"), this);
    stop_button_->setEnabled(false);
    button_row->addWidget(start_button_);
    button_row->addWidget(stop_button_);
    button_row->addStretch(1);
    root->addLayout(button_row);

    status_label_ = new QLabel(IdleStatusText(), this);
    audio_label_ = new QLabel(QStringLiteral("音频 0 包 · 0 B"), this);
    error_label_ = new QLabel(this);
    error_label_->setStyleSheet(QStringLiteral("color: #c00000;"));
    error_label_->setWordWrap(true);
    error_label_->setVisible(false);
    root->addWidget(status_label_);
    root->addWidget(audio_label_);
    root->addWidget(error_label_);

    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(33);
}

void MainWindow::ConnectSignals() {
    connect(browse_button_, &QPushButton::clicked, this,
            &MainWindow::BrowseOutput);
    connect(start_button_, &QPushButton::clicked, this,
            [this] { StartRecording(); });
    connect(stop_button_, &QPushButton::clicked, this,
            &MainWindow::StopRecording);
    connect(poll_timer_, &QTimer::timeout, this, &MainWindow::Tick);
    connect(system_audio_check_, &QCheckBox::toggled, system_gain_,
            &QWidget::setEnabled);
    connect(mic_check_, &QCheckBox::toggled, mic_gain_, &QWidget::setEnabled);
    connect(display_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (!recording_) {
                    StartIdlePreview();
                }
            });
}

void MainWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (preview_started_) {
        return;
    }
    preview_started_ = true;
    // The tick timer also drives the idle preview, so it must run before the
    // first recording starts.
    poll_timer_->start();
    StartIdlePreview();
}

void MainWindow::StartIdlePreview() {
    if (!has_display_ || recording_) {
        return;
    }
    std::string error;
    const int display_index = display_combo_->currentData().toInt();
    if (!preview_source_.Start(display_index, 640, 360, 30, error)) {
        preview_->SetFrame(nullptr);
        last_preview_.reset();
        status_label_->setText(PreviewUnavailableText(error));
        return;
    }
    if (status_label_->text().startsWith(QStringLiteral("预览不可用："))) {
        status_label_->setText(IdleStatusText());
    }
}

void MainWindow::PopulateDisplays() {
    hmmob::D3D11Device device;
    if (device.Create()) {
        const std::vector<hmmob::DisplayInfo> displays =
            device.EnumerateDisplays();
        for (const hmmob::DisplayInfo& info : displays) {
            display_combo_->addItem(
                QStringLiteral("%1: %2x%3")
                    .arg(info.index)
                    .arg(info.width)
                    .arg(info.height),
                info.index);
        }
    }

    if (display_combo_->count() == 0) {
        display_combo_->addItem(QStringLiteral("无可用显示器"));
        display_combo_->setEnabled(false);
        start_button_->setEnabled(false);
        has_display_ = false;
        return;
    }

    display_combo_->setCurrentIndex(0);
    has_display_ = true;
    start_button_->setEnabled(true);
}

QString MainWindow::DefaultOutputPath() const {
    QString directory =
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (directory.isEmpty()) {
        directory = QDir::tempPath();
    }
    const QString name = QStringLiteral("hmmob-%1.mp4")
                             .arg(QDateTime::currentDateTime().toString(
                                 QStringLiteral("yyyyMMdd-HHmmss")));
    return QDir(directory).filePath(name);
}

void MainWindow::BrowseOutput() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("选择输出文件"), output_edit_->text(),
        QStringLiteral("MP4 (*.mp4)"));
    if (!path.isEmpty()) {
        output_edit_->setText(path);
    }
}

hmmob::RecorderConfig MainWindow::BuildConfig() const {
    hmmob::RecorderConfig config;
    config.display_index = display_combo_->currentData().toInt();
    config.fps = fps_combo_->currentData().toInt();
    config.codec = codec_combo_->currentData().toString().toStdString();
    config.bitrate = bitrate_combo_->currentData().toLongLong();
    config.hdr = hdr_check_->isChecked();
    config.system_audio = system_audio_check_->isChecked();
    config.microphone = mic_check_->isChecked();
    config.system_gain = static_cast<float>(system_gain_->value());
    config.mic_gain = static_cast<float>(mic_gain_->value());
    config.output_path = output_edit_->text().trimmed().toStdString();
    config.duration_seconds = 0;
    config.preview_width = 640;
    config.preview_height = 360;
    config.preview_fps = 30;
    return config;
}

bool MainWindow::StartRecording() {
    if (recording_) {
        return false;
    }
    if (!has_display_) {
        SetError(QStringLiteral("无可用显示器"));
        return false;
    }
    if (output_edit_->text().trimmed().isEmpty()) {
        SetError(QStringLiteral("请选择输出路径"));
        return false;
    }

    const QString path = output_edit_->text().trimmed();
    // Never destroy an existing file without asking. Smoke runs are scripted
    // and own their path, so they skip the prompt.
    if (!smoke_ && QFileInfo::exists(path)) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QStringLiteral("Hmmob EV 录制"),
            QStringLiteral("文件已存在，是否覆盖？\n%1")
                .arg(QDir::toNativeSeparators(path)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }

    const hmmob::RecorderConfig config = BuildConfig();
    output_path_ = path;

    // The idle preview owns a desktop duplication for this output. Release it
    // (Stop joins within one acquire timeout) before the recorder creates its
    // own, otherwise the second DuplicateOutput can fail.
    preview_source_.Stop();

    // A new attempt must never show the previous run's last frame, even if
    // initialization fails.
    preview_->SetFrame(nullptr);
    last_preview_.reset();
    save_watchdog_.invalidate();

    std::string error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool started = recorder_.Start(config, error);
    QApplication::restoreOverrideCursor();

    if (!started) {
        SetError(QString::fromStdString(error));
        SetConfigEnabled(true);
        StartIdlePreview();
        return false;
    }

    SetError(QString());
    recording_ = true;
    stop_requested_ = false;
    tick_count_ = 0;
    SetConfigEnabled(false);
    stop_button_->setEnabled(true);
    SetAudioLabel(hmmob::RecorderStats{});
    status_label_->setText(QStringLiteral("录制中…"));
    poll_timer_->start();
    return true;
}

void MainWindow::StopRecording() {
    if (!recording_) {
        return;
    }
    recorder_.RequestStop();
    stop_requested_ = true;
    save_watchdog_.start();
    stop_button_->setEnabled(false);
    status_label_->setText(QStringLiteral("正在保存…"));
}

void MainWindow::Tick() {
    if (!recording_) {
        // Idle preview: the desktop is captured by the preview thread until a
        // recording takes the capture path over.
        std::shared_ptr<const hmmob::PreviewFrame> idle_frame =
            preview_source_.LatestFrame();
        if (idle_frame && idle_frame != last_preview_) {
            last_preview_ = idle_frame;
            preview_->SetFrame(std::move(idle_frame));
        }
        const std::string preview_error = preview_source_.error();
        if (!preview_error.empty()) {
            const QString message = PreviewUnavailableText(preview_error);
            if (status_label_->text() != message) {
                status_label_->setText(message);
            }
        } else if (status_label_->text().startsWith(
                       QStringLiteral("预览不可用："))) {
            status_label_->setText(IdleStatusText());
        }
        // Stats stay idle until recording starts.
        return;
    }

    std::shared_ptr<const hmmob::PreviewFrame> frame =
        recorder_.LatestPreview();
    if (frame && frame != last_preview_) {
        last_preview_ = frame;
        preview_->SetFrame(std::move(frame));
    }

    // Stop() must not block the GUI, but a save that never completes must not
    // look like a hung window either. Keep the stopping state: only warn.
    if (stop_requested_ && !recorder_.Finished() &&
        save_watchdog_.isValid() && save_watchdog_.elapsed() >= 15000) {
        status_label_->setText(
            QStringLiteral("正在保存…（已超过 15 秒，仍在等待）"));
    }

    if (++tick_count_ % 8 == 0) {
        RefreshStats();
    }
}

void MainWindow::RefreshStats() {
    if (!recording_) {
        return;
    }
    const hmmob::RecorderStats stats = recorder_.stats();
    UpdateStatsLabels(stats, !stop_requested_);
    if (recorder_.Finished()) {
        FinalizeRecording();
    }
}

void MainWindow::FinalizeRecording() {
    if (!recording_) {
        return;
    }
    recording_ = false;
    stop_requested_ = false;
    save_watchdog_.invalidate();
    // The tick timer keeps running: it drives the idle preview from here on.

    const hmmob::RecorderStats stats = recorder_.stats();
    const QString error = QString::fromStdString(recorder_.error());

    SetConfigEnabled(true);
    stop_button_->setEnabled(false);
    UpdateStatsLabels(stats, true);

    if (!error.isEmpty()) {
        SetError(error);
        status_label_->setText(QStringLiteral("保存失败"));
    } else if (stats.encoded_frames == 0) {
        QFile::remove(output_path_);
        SetError(QString());
        status_label_->setText(
            QStringLiteral("未录制到画面，未生成文件"));
    } else {
        SetError(QString());
        const double mib = static_cast<double>(stats.encoded_bytes) /
                           (1024.0 * 1024.0);
        const double seconds =
            static_cast<double>(stats.elapsed_us) / 1000000.0;
        status_label_->setText(
            QStringLiteral("已保存：%1（%2 MiB，%3 s）")
                .arg(output_path_)
                .arg(mib, 0, 'f', 1)
                .arg(seconds, 0, 'f', 2));

        // The next run must not reuse the file that was just written. Only
        // rotate the name while the field still holds our own suggestion;
        // a path the user typed or picked is kept.
        if (output_edit_->text().trimmed() == auto_path_) {
            QString next = DefaultOutputPath();
            if (next == output_path_) {
                next = next.chopped(4) + QStringLiteral("-2.mp4");
            }
            auto_path_ = next;
            output_edit_->setText(auto_path_);
        }
    }

    if (std::shared_ptr<const hmmob::PreviewFrame> frame =
            recorder_.LatestPreview()) {
        last_preview_ = frame;
        preview_->SetFrame(std::move(frame));
    }

    // Resume the idle preview now that the recorder has released the capture
    // path and the controls are usable again.
    StartIdlePreview();

    if (smoke_) {
        FinishSmoke(stats, error);
    }
}

void MainWindow::SetConfigEnabled(bool enabled) {
    display_combo_->setEnabled(enabled && has_display_);
    fps_combo_->setEnabled(enabled);
    codec_combo_->setEnabled(enabled);
    bitrate_combo_->setEnabled(enabled);
    hdr_check_->setEnabled(enabled);
    system_audio_check_->setEnabled(enabled);
    mic_check_->setEnabled(enabled);
    system_gain_->setEnabled(enabled && system_audio_check_->isChecked());
    mic_gain_->setEnabled(enabled && mic_check_->isChecked());
    output_edit_->setEnabled(enabled);
    browse_button_->setEnabled(enabled);
    start_button_->setEnabled(enabled && has_display_);
}

void MainWindow::SetError(const QString& error) {
    last_error_ = error;
    error_label_->setText(error);
    error_label_->setVisible(!error.isEmpty());
}

void MainWindow::SetAudioLabel(const hmmob::RecorderStats& stats) {
    audio_label_->setText(QStringLiteral("音频 %1 包 · %2")
                              .arg(stats.audio_packets)
                              .arg(FormatBytes(stats.audio_bytes)));
}

void MainWindow::UpdateStatsLabels(const hmmob::RecorderStats& stats,
                                   bool update_status) {
    if (update_status) {
        const int64_t total_ms = stats.elapsed_us / 1000;
        const int64_t minutes = total_ms / 60000;
        const int64_t seconds = (total_ms % 60000) / 1000;
        const int64_t tenths = (total_ms % 1000) / 100;
        const double fps = stats.elapsed_us > 0
                               ? static_cast<double>(stats.encoded_frames) *
                                     1000000.0 / static_cast<double>(stats.elapsed_us)
                               : 0.0;
        status_label_->setText(
            QStringLiteral("已录 %1:%2.%3 · %4 帧 · %5 fps")
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'))
                .arg(tenths)
                .arg(stats.encoded_frames)
                .arg(fps, 0, 'f', 1));
    }
    SetAudioLabel(stats);
}

void MainWindow::StartSmoke(int seconds, const QString& path) {
    smoke_ = true;
    smoke_finished_ = false;
    smoke_seconds_ = seconds;
    smoke_path_ = path;
    output_edit_->setText(path);

    display_combo_->setCurrentIndex(0);
    fps_combo_->setCurrentIndex(fps_combo_->findData(60));
    codec_combo_->setCurrentIndex(
        codec_combo_->findData(QStringLiteral("hevc")));
    bitrate_combo_->setCurrentIndex(
        bitrate_combo_->findData(QVariant::fromValue<qlonglong>(50000000)));
    hdr_check_->setChecked(false);
    system_audio_check_->setChecked(true);
    mic_check_->setChecked(true);
    system_gain_->setValue(1.0);
    mic_gain_->setValue(1.0);

    // A hung smoke run must still terminate with a diagnosable result instead
    // of leaving a window behind for the automation to time out on. The
    // watchdog outlives the requested duration: 300 ms start delay plus the
    // recording itself plus a 30 s grace period (startup, muxer flush).
    smoke_watchdog_ = new QTimer(this);
    smoke_watchdog_->setSingleShot(true);
    smoke_watchdog_->setInterval(300 + smoke_seconds_ * 1000 + 30000);
    connect(smoke_watchdog_, &QTimer::timeout, this, [this] {
        FinishSmoke(recorder_.stats(), QStringLiteral("smoke_timeout"));
    });
    smoke_watchdog_->start();

    QTimer::singleShot(300, this, [this] {
        if (!StartRecording()) {
            FinishSmoke(recorder_.stats(), last_error_);
            return;
        }
        QTimer::singleShot(smoke_seconds_ * 1000, this,
                           [this] { StopRecording(); });
    });
}

void MainWindow::FinishSmoke(const hmmob::RecorderStats& stats,
                             const QString& error) {
    if (smoke_finished_) {
        return;
    }
    smoke_finished_ = true;
    if (smoke_watchdog_) {
        smoke_watchdog_->stop();
    }

    QFile file(smoke_path_ + QStringLiteral(".smoke.txt"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text |
                  QIODevice::Truncate)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "elapsed_us=" << stats.elapsed_us << '\n';
        out << "encoded=" << stats.encoded_frames << '\n';
        out << "packets=" << stats.packets_written << '\n';
        out << "bytes=" << stats.encoded_bytes << '\n';
        out << "audio_packets=" << stats.audio_packets << '\n';
        out << "audio_bytes=" << stats.audio_bytes << '\n';
        out << "error=" << error << '\n';
        out.flush();
        file.close();
    }

    const bool success = error.isEmpty() && stats.encoded_frames > 0;
    QCoreApplication::exit(success ? 0 : 1);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (closing_) {
        event->ignore();
        return;
    }
    if (!recorder_.Running()) {
        preview_source_.Stop();
        event->accept();
        return;
    }

    // Set before the modal question so a second close request cannot open a
    // second confirmation dialog.
    closing_ = true;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Hmmob EV 录制"),
        QStringLiteral("正在录制，确定停止并退出？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        closing_ = false;
        if (recording_) {
            stop_button_->setEnabled(!stop_requested_);
            if (!poll_timer_->isActive()) {
                poll_timer_->start();
            }
        }
        event->ignore();
        return;
    }

    poll_timer_->stop();
    start_button_->setEnabled(false);
    stop_button_->setEnabled(false);
    recorder_.RequestStop();
    QElapsedTimer timer;
    timer.start();
    while (!recorder_.Finished() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::msleep(50);
    }
    preview_source_.Stop();
    event->accept();
}
