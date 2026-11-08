#include "app/MainWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdint>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

#include "app/Controls.h"
#include "app/IconRenderer.h"
#include "app/LevelMeter.h"
#include "app/ModeTile.h"
#include "app/NavTabButton.h"
#include "app/PreviewWidget.h"
#include "app/RegionSelector.h"
#include "app/SettingsDialog.h"
#include "app/TitleBar.h"
#include "app/UiFont.h"
#include "app/WindowPickerDialog.h"
#include "core/capture/CameraSource.h"
#include "core/gfx/D3D11Device.h"

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace {

constexpr int kRightColumnWidth = 480;
constexpr int kBottomBarHeight = 100;
constexpr int kStatusHeight = 18;
constexpr int kWindowResizeBorder = 7;

const char* const kStyleSheet = R"(
QWidget#root { background: #F5F7FC; }
QWidget#navBar { background: #F5F7FC; }
QWidget#contentArea { background: transparent; }
QWidget#bottomBar { background: #FAFBFD; border-top: 1px solid #EEF1F5; }
QFrame#card { background: #FFFFFF; border: 1px solid #EDF0F5; border-radius: 12px; }
QLabel { color: #1D2129; }
QLabel#cardTitle { font-size: 16px; font-weight: 600; color: #1D2129; }
QLabel#rowLabel { font-size: 14px; color: #1D2129; }
QLabel#caption { font-size: 13px; color: #86909C; }
QLabel#statValue { font-size: 16px; color: #1D2129; }
QLabel#statusLabel { font-size: 13px; color: #86909C; }
QPushButton#navAction { background: transparent; border: none; color: #4E5969; font-size: 15px; padding: 6px 8px; border-radius: 8px; }
QPushButton#navAction:hover { background: #EDF2FA; }
QPushButton#navAction:pressed { background: #E3EAF5; }
QPushButton#sourceToggle { background: transparent; border: none; border-radius: 8px; }
QPushButton#sourceToggle:hover { background: #EDF0F5; }
QPushButton#iconButton { background: transparent; border: none; border-radius: 8px; }
QPushButton#iconButton:hover { background: #EDF0F5; }
QComboBox { background: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 10px; padding: 0 10px 0 14px; font-size: 14px; color: #1D2129; }
QComboBox:hover { border-color: #C9CDD4; }
QComboBox:disabled { background: #F7F8FA; color: #A9AEB8; border-color: #EDF0F5; }
QComboBox::drop-down { border: none; background: transparent; width: 34px; }
QComboBox::down-arrow { image: none; width: 0px; height: 0px; }
QComboBox QAbstractItemView { background: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 8px; padding: 4px; outline: 0; selection-background-color: #EAF2FE; selection-color: #2F6BFF; }
QComboBox QAbstractItemView::item { min-height: 30px; padding: 0 10px; }
QFrame#pathBox { background: #F7F8FA; border: 1px solid #E5E6EB; border-radius: 10px; }
QLineEdit#pathEdit { background: transparent; border: none; padding: 0px; font-size: 14px; color: #1D2129; selection-background-color: #2F6BFF; selection-color: #FFFFFF; }
QLineEdit#pathEdit:disabled { color: #A9AEB8; }
QPushButton#browseButton { background: transparent; border: none; border-radius: 8px; }
QPushButton#browseButton:hover { background: #EDF0F5; }
QScrollArea#settingsScroll { background: transparent; border: none; }
QScrollArea#settingsScroll > QWidget > QWidget { background: transparent; }
QWidget#scrollContent { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0px; }
QScrollBar::handle:vertical { background: #D5DBE5; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #C2C9D6; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: transparent; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)";

QString FormatDuration(int64_t elapsed_us) {
    const int64_t total_seconds = elapsed_us / 1000000;
    const int64_t hours = total_seconds / 3600;
    const int64_t minutes = (total_seconds / 60) % 60;
    const int64_t seconds = total_seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

QString FormatSize(int64_t bytes) {
    if (bytes <= 0) {
        return QStringLiteral("0 MB");
    }
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("Hmmob 录屏"));
    setObjectName(QStringLiteral("root"));
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowMinimizeButtonHint |
                   Qt::WindowMaximizeButtonHint | Qt::WindowSystemMenuHint);
    setStyleSheet(QString::fromUtf8(kStyleSheet));
    resize(1400, 830);
    setMinimumSize(1024, 680);

    BuildUi();
    PopulateDisplays();
    PopulateCameras();
    ConnectSignals();

    const QRect available = screen()->availableGeometry();
    move(available.center() - QPoint(width() / 2, height() / 2));
}

MainWindow::~MainWindow() { preview_source_.Stop(); }

void MainWindow::BuildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    title_bar_ = new TitleBar(this);
    root->addWidget(title_bar_);
    root->addWidget(BuildNavRow());

    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("contentArea"));
    auto* content_layout = new QHBoxLayout(content);
    content_layout->setContentsMargins(26, 8, 26, 30);
    content_layout->setSpacing(24);

    preview_ = new PreviewWidget(content);
    content_layout->addWidget(preview_, 1);

    auto* column = new QWidget;
    column->setObjectName(QStringLiteral("scrollContent"));
    auto* column_layout = new QVBoxLayout(column);
    column_layout->setContentsMargins(0, 0, 0, 0);
    column_layout->setSpacing(12);
    column_layout->addWidget(BuildContentCard(), 1);
    column_layout->addWidget(BuildAudioCard(), 1);
    column_layout->addWidget(BuildVideoCard(), 1);

    auto* settings_scroll = new QScrollArea(content);
    settings_scroll->setObjectName(QStringLiteral("settingsScroll"));
    settings_scroll->setWidgetResizable(true);
    settings_scroll->setFrameShape(QFrame::NoFrame);
    settings_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settings_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    settings_scroll->setWidget(column);
    settings_scroll->setFixedWidth(kRightColumnWidth);
    settings_scroll->viewport()->setAutoFillBackground(false);
    content_layout->addWidget(settings_scroll, 0);

    root->addWidget(content, 1);
    root->addWidget(BuildBottomBar());

    status_label_ = new QLabel(this);
    status_label_->setObjectName(QStringLiteral("statusLabel"));
    status_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    status_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    status_label_->setText(QString());
    status_label_->raise();

    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(33);

    status_timer_ = new QTimer(this);
    status_timer_->setSingleShot(true);
    status_timer_->setInterval(4000);
}

QLabel* MainWindow::MakeIconLabel(Icon icon, int size, const QColor& color,
                                  QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setPixmap(IconRenderer::Pixmap(icon, size, color));
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    return label;
}

QWidget* MainWindow::BuildNavRow() {
    auto* nav = new QWidget(this);
    nav->setObjectName(QStringLiteral("navBar"));
    nav->setAttribute(Qt::WA_StyledBackground, true);
    nav->setFixedHeight(72);

    auto* layout = new QHBoxLayout(nav);
    layout->setContentsMargins(34, 0, 26, 0);
    layout->setSpacing(12);

    screen_tab_ = new NavTabButton(Icon::Monitor, QStringLiteral("屏幕录制"), nav);
    window_tab_ = new NavTabButton(Icon::Window, QStringLiteral("窗口录制"), nav);
    region_tab_ = new NavTabButton(Icon::Region, QStringLiteral("区域录制"), nav);
    camera_tab_ = new NavTabButton(Icon::Camera, QStringLiteral("摄像头"), nav);
    screen_tab_->SetActive(true);
    layout->addWidget(screen_tab_);
    layout->addWidget(window_tab_);
    layout->addWidget(region_tab_);
    layout->addWidget(camera_tab_);
    layout->addStretch(1);

    auto make_action = [&](Icon icon, const QString& text) {
        auto* button = new QPushButton(text, nav);
        button->setObjectName(QStringLiteral("navAction"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setIcon(
            IconRenderer::QIconFor(icon, 22, QColor(0x4E, 0x59, 0x69)));
        button->setIconSize(QSize(22, 22));
        QFont font = button->font();
        font.setPixelSize(15);
        button->setFont(font);
        return button;
    };
    settings_button_ = make_action(Icon::Gear, QStringLiteral("设置"));
    auto* open_files = make_action(Icon::Folder, QStringLiteral("打开文件"));
    layout->addWidget(settings_button_);
    layout->addSpacing(16);
    layout->addWidget(open_files);

    connect(settings_button_, &QPushButton::clicked, this,
            &MainWindow::OpenSettings);
    connect(open_files, &QPushButton::clicked, this, [this] {
        const QString path =
            QFileInfo(output_edit_->text().trimmed()).absolutePath();
        if (path.isEmpty() ||
            !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            ShowStatus(QStringLiteral("无法打开保存文件夹"), true, 4000);
        }
    });
    return nav;
}

QWidget* MainWindow::BuildContentCard() {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(0);

    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    header->addWidget(
        MakeIconLabel(Icon::Monitor, 20, QColor(0x1D, 0x21, 0x29), card));
    auto* title = new QLabel(QStringLiteral("录制内容"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    header->addWidget(title);
    header->addStretch(1);
    layout->addLayout(header);
    layout->addSpacing(8);

    auto* row = new QHBoxLayout;
    row->setSpacing(10);
    mode_group_ = new QButtonGroup(this);
    mode_group_->setExclusive(true);

    full_tile_ = new ModeTile(Icon::Monitor, QStringLiteral("全屏"), card);
    auto* region_tile = new ModeTile(Icon::Region, QStringLiteral("区域"), card);
    auto* window_tile = new ModeTile(Icon::Window, QStringLiteral("窗口"), card);
    auto* camera_tile = new ModeTile(Icon::Webcam, QStringLiteral("摄像头"), card);
    mode_group_->addButton(full_tile_, 0);
    mode_group_->addButton(region_tile, 1);
    mode_group_->addButton(window_tile, 2);
    mode_group_->addButton(camera_tile, 3);
    full_tile_->setChecked(true);

    row->addWidget(full_tile_, 1, Qt::AlignVCenter);
    row->addWidget(region_tile, 1, Qt::AlignVCenter);
    row->addWidget(window_tile, 1, Qt::AlignVCenter);
    row->addWidget(camera_tile, 1, Qt::AlignVCenter);
    layout->addLayout(row, 1);

    return card;
}

QWidget* MainWindow::BuildAudioCard() {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(0);

    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    header->addWidget(
        MakeIconLabel(Icon::Speaker, 20, QColor(0x1D, 0x21, 0x29), card));
    auto* title = new QLabel(QStringLiteral("音频设置"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    header->addWidget(title);
    header->addStretch(1);
    layout->addLayout(header);
    layout->addSpacing(6);

    auto build_row = [&](bool microphone) {
        auto* row = new QWidget(card);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);
        h->addWidget(MakeIconLabel(microphone ? Icon::Mic : Icon::Speaker, 20,
                                   QColor(0x4E, 0x59, 0x69), row));
        h->addSpacing(10);
        auto* label = new QLabel(
            microphone ? QStringLiteral("麦克风") : QStringLiteral("系统声音"),
            row);
        label->setObjectName(QStringLiteral("rowLabel"));
        label->setFixedWidth(64);
        h->addWidget(label);
        h->addSpacing(10);

        auto* device = new StyledComboBox(row);
        device->addItem(QStringLiteral("默认设备"));
        device->setFixedHeight(36);
        h->addWidget(device, 1);
        h->addSpacing(10);

        auto* meter = new LevelMeter(microphone ? 1 : 3, row);
        h->addWidget(meter, 0, Qt::AlignVCenter);
        h->addSpacing(8);

        auto* toggle = new QPushButton(row);
        toggle->setObjectName(QStringLiteral("sourceToggle"));
        toggle->setCheckable(true);
        toggle->setChecked(true);
        toggle->setCursor(Qt::PointingHandCursor);
        toggle->setFocusPolicy(Qt::NoFocus);
        toggle->setFixedSize(32, 32);
        toggle->setIconSize(QSize(22, 22));
        h->addWidget(toggle, 0, Qt::AlignVCenter);

        if (microphone) {
            mic_device_combo_ = device;
            mic_meter_ = meter;
            mic_toggle_ = toggle;
        } else {
            system_device_combo_ = device;
            system_meter_ = meter;
            system_toggle_ = toggle;
        }
        connect(toggle, &QPushButton::toggled, this,
                [this, microphone] { UpdateAudioUi(microphone); });
        return row;
    };

    auto* rows = new QVBoxLayout;
    rows->setSpacing(6);
    rows->addWidget(build_row(true));
    rows->addWidget(build_row(false));
    layout->addStretch(1);
    layout->addLayout(rows);
    layout->addStretch(1);

    UpdateAudioUi(true);
    UpdateAudioUi(false);
    return card;
}

QWidget* MainWindow::BuildVideoCard() {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(1, 1, 1, 1);
    card_layout->setSpacing(0);

    auto* body = new QWidget(card);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(0);

    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    header->addWidget(
        MakeIconLabel(Icon::Gear, 20, QColor(0x1D, 0x21, 0x29), body));
    auto* title = new QLabel(QStringLiteral("视频设置"), body);
    title->setObjectName(QStringLiteral("cardTitle"));
    header->addWidget(title);
    header->addStretch(1);
    layout->addLayout(header);
    layout->addSpacing(8);

    auto* row1 = new QHBoxLayout;
    row1->setSpacing(0);
    row1->addWidget(MakeIconLabel(Icon::Hd, 20, QColor(0x4E, 0x59, 0x69), body));
    row1->addSpacing(8);
    auto* resolution_label = new QLabel(QStringLiteral("分辨率"), body);
    resolution_label->setObjectName(QStringLiteral("rowLabel"));
    resolution_label->setFixedWidth(64);
    row1->addWidget(resolution_label);
    row1->addSpacing(10);
    resolution_combo_ = new StyledComboBox(body);
    resolution_combo_->setFixedHeight(36);
    resolution_combo_->setFixedWidth(168);
    row1->addWidget(resolution_combo_);
    row1->addStretch(1);
    auto* fps_label = new QLabel(QStringLiteral("帧率"), body);
    fps_label->setObjectName(QStringLiteral("rowLabel"));
    row1->addWidget(fps_label);
    row1->addSpacing(8);
    fps_combo_ = new StyledComboBox(body);
    fps_combo_->setFixedHeight(36);
    fps_combo_->setFixedWidth(108);
    fps_combo_->addItem(QStringLiteral("30 fps"), 30);
    fps_combo_->addItem(QStringLiteral("60 fps"), 60);
    fps_combo_->addItem(QStringLiteral("120 fps"), 120);
    fps_combo_->setCurrentIndex(1);
    row1->addWidget(fps_combo_);
    layout->addLayout(row1);
    layout->addSpacing(6);

    auto* row2 = new QHBoxLayout;
    row2->setSpacing(0);
    row2->addWidget(MakeIconLabel(Icon::Film, 20, QColor(0x4E, 0x59, 0x69), body));
    row2->addSpacing(8);
    auto* codec_label = new QLabel(QStringLiteral("编码格式"), body);
    codec_label->setObjectName(QStringLiteral("rowLabel"));
    codec_label->setFixedWidth(64);
    row2->addWidget(codec_label);
    row2->addSpacing(10);
    codec_combo_ = new StyledComboBox(body);
    codec_combo_->setFixedHeight(36);
    codec_combo_->setFixedWidth(168);
    codec_combo_->addItem(QStringLiteral("HEVC"), QStringLiteral("hevc"));
    codec_combo_->addItem(QStringLiteral("H.264"), QStringLiteral("h264"));
    row2->addWidget(codec_combo_);
    row2->addStretch(1);
    layout->addLayout(row2);
    layout->addSpacing(6);

    layout->addStretch(1);

    card_layout->addWidget(body, 1);

    return card;
}

QWidget* MainWindow::BuildBottomBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("bottomBar"));
    bar->setAttribute(Qt::WA_StyledBackground, true);
    bar->setFixedHeight(kBottomBarHeight);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(34, 0, 26, 0);
    layout->setSpacing(0);

    layout->addWidget(
        MakeIconLabel(Icon::Folder, 22, QColor(0x4E, 0x59, 0x69), bar));
    layout->addSpacing(10);
    auto* save_label = new QLabel(QStringLiteral("保存路径"), bar);
    save_label->setStyleSheet(QStringLiteral("color:#4E5969; font-size:14px;"));
    layout->addWidget(save_label);
    layout->addSpacing(18);

    auto* path_box = new QFrame(bar);
    path_box->setObjectName(QStringLiteral("pathBox"));
    path_box->setFixedHeight(44);
    path_box->setMinimumWidth(280);
    path_box->setMaximumWidth(560);
    auto* path_layout = new QHBoxLayout(path_box);
    path_layout->setContentsMargins(14, 0, 4, 0);
    path_layout->setSpacing(0);
    output_edit_ = new QLineEdit(path_box);
    output_edit_->setObjectName(QStringLiteral("pathEdit"));
    output_edit_->setPlaceholderText(QStringLiteral("输出 MP4 路径"));
    auto_path_ = DefaultOutputPath();
    output_edit_->setText(auto_path_);
    output_edit_->setCursorPosition(0);
    path_layout->addWidget(output_edit_, 1);
    browse_button_ = new QPushButton(path_box);
    browse_button_->setObjectName(QStringLiteral("browseButton"));
    browse_button_->setCursor(Qt::PointingHandCursor);
    browse_button_->setFocusPolicy(Qt::NoFocus);
    browse_button_->setFixedSize(36, 36);
    browse_button_->setIconSize(QSize(20, 20));
    browse_button_->setIcon(
        IconRenderer::QIconFor(Icon::Folder, 20, QColor(0x4E, 0x59, 0x69)));
    browse_button_->setToolTip(QStringLiteral("选择保存位置"));
    path_layout->addWidget(browse_button_);
    layout->addWidget(path_box, 2);
    layout->addSpacing(80);

    auto make_divider = [&] {
        auto* divider = new QWidget(bar);
        divider->setFixedSize(1, 40);
        divider->setStyleSheet(QStringLiteral("background:#E5E6EB;"));
        return divider;
    };
    // One row per stat: icon + caption + value, all vertically centered. The
    // previous caption-over-value stack never lined up with the icon.
    auto make_stat = [&](Icon icon, const QString& caption, QLabel** value) {
        auto* block = new QWidget(bar);
        auto* block_layout = new QHBoxLayout(block);
        block_layout->setContentsMargins(0, 0, 0, 0);
        block_layout->setSpacing(0);
        block_layout->addWidget(
            MakeIconLabel(icon, 22, QColor(0x86, 0x90, 0x9C), block),
            0, Qt::AlignVCenter);
        block_layout->addSpacing(10);
        auto* caption_label = new QLabel(caption, block);
        caption_label->setObjectName(QStringLiteral("caption"));
        block_layout->addWidget(caption_label, 0, Qt::AlignVCenter);
        block_layout->addSpacing(10);
        auto* value_label = new QLabel(block);
        value_label->setObjectName(QStringLiteral("statValue"));
        block_layout->addWidget(value_label, 0, Qt::AlignVCenter);
        *value = value_label;
        return block;
    };

    QLabel* duration_value = nullptr;
    QLabel* size_value = nullptr;
    layout->addWidget(make_stat(Icon::Clock, QStringLiteral("预计时长"),
                                &duration_value));
    duration_label_ = duration_value;
    layout->addSpacing(20);
    layout->addWidget(make_divider());
    layout->addSpacing(20);
    layout->addWidget(make_stat(Icon::File, QStringLiteral("预计大小"),
                                &size_value));
    size_label_ = size_value;
    duration_label_->setText(QStringLiteral("00:00:00"));
    size_label_->setText(QStringLiteral("0 MB"));

    layout->addStretch(1);

    start_button_ = new RecordButton(bar);
    layout->addWidget(start_button_, 0, Qt::AlignVCenter);

    return bar;
}

void MainWindow::PopulateDisplays() {
    displays_.clear();
    resolution_combo_->clear();

    hmmob::D3D11Device device;
    if (device.Create()) {
        displays_ = device.EnumerateDisplays();
    }
    for (const hmmob::DisplayInfo& info : displays_) {
        resolution_combo_->addItem(
            QStringLiteral("%1 × %2").arg(info.width).arg(info.height),
            info.index);
    }

    if (displays_.empty()) {
        resolution_combo_->addItem(QStringLiteral("无可用显示器"));
        resolution_combo_->setEnabled(false);
        start_button_->setEnabled(CanStart());
        has_display_ = false;
        selected_display_index_ = -1;
        ShowPreviewError(QStringLiteral("无可用显示器"));
        return;
    }

    selected_display_index_ = displays_.front().index;
    resolution_combo_->setCurrentIndex(0);
    has_display_ = true;
    start_button_->setEnabled(CanStart());
    UpdateBadgeText();
}

void MainWindow::PopulateCameras() {
    const std::vector<hmmob::CameraSource::CameraDeviceInfo> devices =
        hmmob::CameraSource::EnumerateDevices();
    camera_device_count_ = static_cast<int>(devices.size());
    if (camera_device_count_ <= 0) {
        camera_index_ = 0;
        return;
    }
    if (camera_index_ < 0 || camera_index_ >= camera_device_count_) {
        camera_index_ = 0;
    }
}

bool MainWindow::CanStart() const {
    // Window and camera capture do not depend on an attached display.
    return has_display_ || selected_mode_ >= 2;
}

QRect MainWindow::SelectedDisplayRect() const {
    for (const hmmob::DisplayInfo& info : displays_) {
        if (info.index == selected_display_index_) {
            return QRect(info.left, info.top, info.width, info.height);
        }
    }
    return QRect();
}

void MainWindow::ConnectSignals() {
    connect(title_bar_, &TitleBar::MinimizeRequested, this,
            &QWidget::showMinimized);
    connect(title_bar_, &TitleBar::MaximizeRequested, this,
            &MainWindow::ToggleMaximize);
    connect(title_bar_, &TitleBar::CloseRequested, this, &QWidget::close);

    connect(screen_tab_, &QAbstractButton::clicked, this,
            [this] { SetMode(0); });
    connect(region_tab_, &QAbstractButton::clicked, this,
            [this] { SetMode(1); });
    connect(window_tab_, &QAbstractButton::clicked, this,
            [this] { SetMode(2); });
    connect(camera_tab_, &QAbstractButton::clicked, this,
            [this] { SetMode(3); });

    connect(browse_button_, &QPushButton::clicked, this,
            &MainWindow::BrowseOutput);
    connect(start_button_, &QAbstractButton::clicked, this, [this] {
        if (recording_) {
            StopRecording();
        } else {
            StartRecording();
        }
    });
    connect(poll_timer_, &QTimer::timeout, this, &MainWindow::Tick);
    connect(status_timer_, &QTimer::timeout, this, [this] {
        status_label_->clear();
        status_label_->setStyleSheet(QString());
    });
    connect(resolution_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (index >= 0) {
                    SelectDisplay(resolution_combo_->itemData(index).toInt());
                }
            });
    connect(mode_group_, &QButtonGroup::idClicked, this,
            [this](int id) { SetMode(id); });
}

void MainWindow::SetMode(int mode) {
    if (mode < 0 || mode > 3 || recording_) {
        return;
    }
    const bool changed = selected_mode_ != mode;
    selected_mode_ = mode;

    if (QAbstractButton* tile = mode_group_->button(mode)) {
        const QSignalBlocker blocker(mode_group_);
        tile->setChecked(true);
    }
    screen_tab_->SetActive(mode == 0);
    region_tab_->SetActive(mode == 1);
    window_tab_->SetActive(mode == 2);
    camera_tab_->SetActive(mode == 3);

    start_button_->setEnabled(CanStart());

    const QString region_hint =
        QStringLiteral("点击开始录制后框选区域");
    if (mode == 0 || mode == 1) {
        preview_->SetPlaceholderText(QStringLiteral("预览未开始"));
        // Screen and region share the same desktop preview; do not tear it
        // down when the user just switches between the two.
        if (!preview_source_.Running()) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            StartIdlePreview();
            QApplication::restoreOverrideCursor();
        }
        if (mode == 1) {
            ShowStatus(region_hint, false, 0);
        } else if (status_label_->text() == region_hint) {
            status_timer_->stop();
            status_label_->clear();
            status_label_->setStyleSheet(QString());
        }
    } else {
        // No live desktop preview for window/camera capture; the recorder's
        // preview takes over once recording starts.
        if (changed || preview_source_.Running()) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            preview_source_.Stop();
            QApplication::restoreOverrideCursor();
        }
        preview_->SetFrame(nullptr);
        last_preview_.reset();
        ShowPreviewError(QString());
        preview_->SetPlaceholderText(QStringLiteral("开始录制后显示画面"));
        if (status_label_->text() == region_hint) {
            status_timer_->stop();
            status_label_->clear();
            status_label_->setStyleSheet(QString());
        }
    }
    UpdateBadgeText();
}

void MainWindow::UpdateAudioUi(bool microphone) {
    QPushButton* toggle = microphone ? mic_toggle_ : system_toggle_;
    LevelMeter* meter = microphone ? mic_meter_ : system_meter_;
    if (!toggle || !meter) {
        return;
    }
    const bool enabled = toggle->isChecked();
    meter->SetActive(enabled);
    toggle->setIcon(IconRenderer::QIconFor(
        enabled ? Icon::Speaker : Icon::SpeakerMuted, 22,
        enabled ? QColor(0x3A, 0x42, 0x54) : QColor(0xB4, 0xBA, 0xC6)));
    toggle->setToolTip(enabled ? QStringLiteral("静音") : QStringLiteral("开启"));
}

void MainWindow::OpenSettings() {
    if (recording_) {
        return;
    }
    SettingsDialog dialog(bitrate_, hdr_, camera_index_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    bitrate_ = dialog.bitrate();
    hdr_ = dialog.hdr();
    camera_index_ = dialog.cameraIndex();
}

void MainWindow::ShowStatus(const QString& text, bool error, int timeout_ms) {
    status_label_->setText(text);
    status_label_->setStyleSheet(
        error ? QStringLiteral("color:#E5484D; font-size:13px;")
              : QStringLiteral("color:#86909C; font-size:13px;"));
    if (timeout_ms > 0) {
        status_timer_->start(timeout_ms);
    } else {
        status_timer_->stop();
    }
}

void MainWindow::ShowPreviewError(const QString& detail) {
    if (preview_error_detail_ == detail) {
        return;
    }
    preview_error_detail_ = detail;
    // The badge only carries a short marker; the full error is inspectable via
    // the tooltip. It lives on the preview card, so a save result in the
    // status line and a preview failure can be on screen at the same time.
    preview_->SetErrorBadge(detail.isEmpty()
                                ? QString()
                                : QStringLiteral("预览不可用 ×"));
    preview_->setToolTip(detail);
}

void MainWindow::SelectDisplay(int display_index) {
    if (display_index == selected_display_index_) {
        return;
    }
    const bool known = std::any_of(
        displays_.begin(), displays_.end(),
        [display_index](const hmmob::DisplayInfo& info) {
            return info.index == display_index;
        });
    if (!known) {
        return;
    }
    // The resolution combo is the only view on the selection, so keep its
    // current item in sync when the change came from somewhere else.
    selected_display_index_ = display_index;
    {
        const QSignalBlocker blocker(resolution_combo_);
        const int index = resolution_combo_->findData(display_index);
        if (index >= 0) {
            resolution_combo_->setCurrentIndex(index);
        }
    }
    UpdateBadgeText();
    if (!recording_) {
        // Stop() joins the preview thread, which can wait for one short
        // capture timeout.
        QApplication::setOverrideCursor(Qt::WaitCursor);
        StartIdlePreview();
        QApplication::restoreOverrideCursor();
    }
}

void MainWindow::UpdateBadgeText() {
    if (recording_) {
        switch (selected_mode_) {
            case 0:
                preview_->SetBadgeText(
                    QStringLiteral("预览区域 · %1 × %2")
                        .arg(capture_width_)
                        .arg(capture_height_));
                return;
            case 1:
                preview_->SetBadgeText(QStringLiteral("区域 · %1 × %2")
                                           .arg(capture_width_)
                                           .arg(capture_height_));
                return;
            default:
                preview_->SetBadgeText(QStringLiteral("录制中 · %1 × %2")
                                           .arg(capture_width_)
                                           .arg(capture_height_));
                return;
        }
    }
    if (selected_mode_ == 2) {
        preview_->SetBadgeText(QStringLiteral("窗口录制"));
        return;
    }
    if (selected_mode_ == 3) {
        preview_->SetBadgeText(QStringLiteral("摄像头"));
        return;
    }
    for (const hmmob::DisplayInfo& info : displays_) {
        if (info.index == selected_display_index_) {
            preview_->SetBadgeText(
                QStringLiteral("预览区域 · %1 × %2")
                    .arg(info.width)
                    .arg(info.height));
            return;
        }
    }
    preview_->SetBadgeText(QStringLiteral("预览区域"));
}

void MainWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    UpdateStatusGeometry();
    static bool effects_applied = false;
    if (!effects_applied) {
        effects_applied = true;
        ApplyWindowEffects();
    }
    if (preview_started_) {
        return;
    }
    preview_started_ = true;
    // The tick timer also drives the idle preview, so it must run before the
    // first recording starts.
    poll_timer_->start();
    StartIdlePreview();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateStatusGeometry();
}

void MainWindow::UpdateStatusGeometry() {
    if (!status_label_) {
        return;
    }
    status_label_->setGeometry(
        34, height() - kBottomBarHeight - 26, width() - 60, kStatusHeight);
    status_label_->raise();
}

void MainWindow::StartIdlePreview() {
    // Window and camera modes deliberately show a placeholder until recording
    // starts; only the desktop-backed modes own an idle preview.
    if (!has_display_ || recording_ || isMinimized() || selected_mode_ > 1) {
        return;
    }
    std::string error;
    if (!preview_source_.Start(selected_display_index_, 640, 360, 30, error)) {
        preview_->SetFrame(nullptr);
        last_preview_.reset();
        ShowPreviewError(QString::fromStdString(error));
        return;
    }
    ShowPreviewError(QString());
}

QString MainWindow::DefaultOutputPath() const {
    QString directory =
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (directory.isEmpty()) {
        directory = QDir::tempPath();
    }
    const QString name = QStringLiteral("Hmmob-%1.mp4")
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
    config.mode = static_cast<hmmob::CaptureMode>(selected_mode_);
    config.display_index = selected_display_index_;
    config.fps = fps_combo_->currentData().toInt();
    config.codec = codec_combo_->currentData().toString().toStdString();
    config.bitrate = bitrate_;
    // HDR capture only exists on the desktop-duplication path.
    config.hdr = selected_mode_ == 0 && hdr_;
    config.camera_index = camera_index_;
    config.system_audio = system_toggle_->isChecked();
    config.microphone = mic_toggle_->isChecked();
    // Source levels are intentionally fixed at unity in this build; the
    // meters are decorative and never feed back into the recorder.
    config.system_gain = 1.0f;
    config.mic_gain = 1.0f;
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
    const int mode = selected_mode_;

    if ((mode == 0 || mode == 1) && !has_display_) {
        SetError(QStringLiteral("无可用显示器"));
        return false;
    }
    if (mode == 3 && camera_device_count_ <= 0) {
        SetError(QStringLiteral("未检测到摄像头"));
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
            this, QStringLiteral("Hmmob 录屏"),
            QStringLiteral("文件已存在，是否覆盖？\n%1")
                .arg(QDir::toNativeSeparators(path)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }

    hmmob::RecorderConfig config = BuildConfig();
    QRect selected_region;

    if (mode == 1) {
        // Region: the overlay sits over the selected display and returns a
        // display-local rectangle.
        if (smoke_) {
            selected_region = smoke_region_;
        } else {
            preview_source_.Stop();
            preview_->SetFrame(nullptr);
            last_preview_.reset();
            if (!RegionSelector::Select(this, SelectedDisplayRect(),
                                        selected_region)) {
                StartIdlePreview();
                return false;
            }
        }
        const QRect display = SelectedDisplayRect();
        if (display.width() > 0 && display.height() > 0) {
            selected_region = selected_region.intersected(
                QRect(0, 0, display.width(), display.height()));
        }
        if (selected_region.width() <= 0 || selected_region.height() <= 0) {
            SetError(QStringLiteral("无效的录制区域"));
            StartIdlePreview();
            return false;
        }
        config.region_x = selected_region.x();
        config.region_y = selected_region.y();
        config.region_width = selected_region.width();
        config.region_height = selected_region.height();
    } else if (mode == 2) {
        HWND window = nullptr;
        if (smoke_) {
            const std::vector<HWND> windows =
                WindowPickerDialog::EnumerateCapturableWindows();
            if (smoke_window_ < 1 ||
                smoke_window_ > static_cast<int>(windows.size())) {
                SetError(QStringLiteral("未找到指定的窗口（共 %1 个）")
                             .arg(windows.size()));
                return false;
            }
            window = windows[static_cast<size_t>(smoke_window_ - 1)];
            wchar_t title[512] = {};
            GetWindowTextW(window, title, 511);
            smoke_window_title_ = QString::fromWCharArray(title);
        } else {
            preview_source_.Stop();
            preview_->SetFrame(nullptr);
            last_preview_.reset();
            if (!WindowPickerDialog::Pick(this, window)) {
                StartIdlePreview();
                return false;
            }
            // Picking can take a while; keep the idle duplication released
            // until the recorder owns the capture path.
            preview_source_.Stop();
        }
        if (!window) {
            SetError(QStringLiteral("未选择窗口"));
            return false;
        }
        config.window = window;
    } else if (mode == 3) {
        if (smoke_) {
            config.camera_index = smoke_camera_;
        }
        if (config.camera_index < 0 ||
            config.camera_index >= camera_device_count_) {
            SetError(QStringLiteral("摄像头不可用"));
            return false;
        }
    }

    output_path_ = path;

    // The idle preview owns a desktop duplication for this output. Release it
    // (Stop joins within one capture timeout) before the recorder creates its
    // own, otherwise the second DuplicateOutput can fail.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    preview_source_.Stop();

    // A new attempt must never show the previous run's last frame, even if
    // initialization fails.
    preview_->SetFrame(nullptr);
    last_preview_.reset();
    save_watchdog_.invalidate();

    std::string error;
    const bool started = recorder_.Start(config, error);
    QApplication::restoreOverrideCursor();

    if (!started) {
        SetError(QString::fromStdString(error));
        SetConfigEnabled(true);
        start_button_->SetRecording(false);
        start_button_->setEnabled(CanStart());
        StartIdlePreview();
        return false;
    }

    SetError(QString());
    ShowPreviewError(QString());
    recording_ = true;
    stop_requested_ = false;
    tick_count_ = 0;
    SetConfigEnabled(false);
    record_started_.start();
    start_button_->SetRecording(true);
    start_button_->SetBusy(false);
    start_button_->setEnabled(true);
    duration_label_->setText(QStringLiteral("00:00:00"));
    size_label_->setText(QStringLiteral("0 MB"));

    const hmmob::RecorderStats started_stats = recorder_.stats();
    capture_width_ = started_stats.display_width;
    capture_height_ = started_stats.display_height;
    if (capture_width_ <= 0 || capture_height_ <= 0) {
        if (mode == 1) {
            capture_width_ = selected_region.width();
            capture_height_ = selected_region.height();
        }
    }
    UpdateBadgeText();
    ShowStatus(QStringLiteral("录制中…"), false, 0);
    poll_timer_->start();
    return true;
}

void MainWindow::StopRecording() {
    if (!recording_) {
        return;
    }
    // A double-click on the start button produces a second click right after
    // the recording began; ignore it instead of starting and instantly
    // stopping. Real users cannot react faster than this window either.
    if (record_started_.isValid() && record_started_.elapsed() < 800) {
        return;
    }
    recorder_.RequestStop();
    stop_requested_ = true;
    save_watchdog_.start();
    start_button_->setEnabled(false);
    start_button_->SetBusy(true);
    ShowStatus(QStringLiteral("正在保存…"), false, 0);
}

void MainWindow::Tick() {
    if (!recording_) {
        if (selected_mode_ > 1) {
            // Window/camera modes show a placeholder, not a desktop preview.
            return;
        }
        // Idle preview: the desktop is captured by the preview thread until a
        // recording takes the capture path over.
        std::shared_ptr<const hmmob::PreviewFrame> idle_frame =
            preview_source_.LatestFrame();
        if (idle_frame && idle_frame != last_preview_) {
            last_preview_ = idle_frame;
            preview_->SetFrame(std::move(idle_frame));
        }
        // Preview failures live in their own badge on the preview so that a
        // save/failure result in the status line is never overwritten.
        const std::string preview_error = preview_source_.error();
        ShowPreviewError(preview_error.empty()
                             ? QString()
                             : QString::fromStdString(preview_error));
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
        ShowStatus(QStringLiteral("正在保存…（已超过 15 秒，仍在等待）"), false,
                   0);
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
    UpdateStatsLabels(stats);
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
    record_started_.invalidate();
    // The tick timer keeps running: it drives the idle preview from here on.

    const hmmob::RecorderStats stats = recorder_.stats();
    const QString error = QString::fromStdString(recorder_.error());

    SetConfigEnabled(true);
    start_button_->SetRecording(false);
    start_button_->SetBusy(false);
    start_button_->setEnabled(CanStart());
    UpdateStatsLabels(stats);

    if (!error.isEmpty()) {
        SetError(error);
        ShowStatus(QStringLiteral("保存失败：%1").arg(error), true, 8000);
    } else if (stats.encoded_frames == 0) {
        QFile::remove(output_path_);
        SetError(QString());
        ShowStatus(QStringLiteral("未录制到画面，未生成文件"), false, 8000);
    } else {
        SetError(QString());
        ShowStatus(QStringLiteral("已保存：%1").arg(output_path_), false, 8000);

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

    // Resume the idle preview for the desktop-backed modes only; window and
    // camera keep the recorder's last frame. If the window is minimized the
    // preview is restarted on restore instead, and the poll timer must not
    // keep ticking invisibly.
    if (selected_mode_ == 0 || selected_mode_ == 1) {
        StartIdlePreview();
    }
    UpdateBadgeText();
    if (isMinimized()) {
        poll_timer_->stop();
    }

    if (smoke_) {
        FinishSmoke(stats, error);
    }
}

void MainWindow::SetConfigEnabled(bool enabled) {
    resolution_combo_->setEnabled(enabled && has_display_);
    fps_combo_->setEnabled(enabled);
    codec_combo_->setEnabled(enabled);
    // The dialog edits options for the next run; it must not be reachable
    // while this run is recording or saving.
    settings_button_->setEnabled(enabled);
    mic_toggle_->setEnabled(enabled);
    system_toggle_->setEnabled(enabled);
    mic_device_combo_->setEnabled(enabled);
    system_device_combo_->setEnabled(enabled);
    output_edit_->setEnabled(enabled);
    browse_button_->setEnabled(enabled);
    for (QAbstractButton* button : mode_group_->buttons()) {
        button->setEnabled(enabled);
    }
}

void MainWindow::SetError(const QString& error) {
    last_error_ = error;
    if (!error.isEmpty()) {
        ShowStatus(error, true, 8000);
    }
}

void MainWindow::UpdateStatsLabels(const hmmob::RecorderStats& stats) {
    duration_label_->setText(FormatDuration(stats.elapsed_us));
    size_label_->setText(FormatSize(stats.encoded_bytes));
}

void MainWindow::StartSmoke(int seconds, const QString& path,
                            const SmokeOptions& options) {
    smoke_ = true;
    smoke_finished_ = false;
    smoke_seconds_ = seconds;
    smoke_path_ = path;
    smoke_mode_ = options.mode;
    smoke_region_ = options.region;
    smoke_window_ = options.window;
    smoke_camera_ = options.camera;
    output_edit_->setText(path);

    if (!displays_.empty()) {
        SelectDisplay(displays_.front().index);
    }
    fps_combo_->setCurrentIndex(fps_combo_->findData(60));
    codec_combo_->setCurrentIndex(
        codec_combo_->findData(QStringLiteral("hevc")));
    bitrate_ = 50000000;
    hdr_ = false;
    system_toggle_->setChecked(true);
    mic_toggle_->setChecked(true);

    int mode = 0;
    if (smoke_mode_ == QStringLiteral("region")) {
        mode = 1;
    } else if (smoke_mode_ == QStringLiteral("window")) {
        mode = 2;
    } else if (smoke_mode_ == QStringLiteral("camera")) {
        mode = 3;
    } else {
        smoke_mode_ = QStringLiteral("screen");
    }
    SetMode(mode);

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
        out << "mode=" << smoke_mode_;
        if (smoke_mode_ == QStringLiteral("region")) {
            const int width = stats.display_width > 0
                                  ? stats.display_width
                                  : smoke_region_.width();
            const int height = stats.display_height > 0
                                   ? stats.display_height
                                   : smoke_region_.height();
            out << " w=" << width << " h=" << height;
        } else if (smoke_mode_ == QStringLiteral("window")) {
            out << " window=" << smoke_window_ << " w=" << stats.display_width
                << " h=" << stats.display_height;
            if (!smoke_window_title_.isEmpty()) {
                QString title = smoke_window_title_;
                title.replace(QLatin1Char('\r'), QLatin1Char(' '));
                title.replace(QLatin1Char('\n'), QLatin1Char(' '));
                out << " title=\"" << title << '"';
            }
        } else if (smoke_mode_ == QStringLiteral("camera")) {
            out << " camera=" << smoke_camera_;
        } else {
            out << " w=" << stats.display_width << " h=" << stats.display_height;
        }
        out << '\n';
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

void MainWindow::ToggleMaximize() {
    // Native maximize/restore, so Win+Up, the system menu and the taskbar all
    // agree with our caption button. Windows keeps the normal placement.
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
    SyncMaximizedState();
}

void MainWindow::SyncMaximizedState() {
    const bool maximized = isMaximized();
    if (maximized == maximized_) {
        return;
    }
    maximized_ = maximized;
    title_bar_->SetMaximized(maximized_);
    ApplyWindowEffects();
}

void MainWindow::SetIdleSuspended(bool suspended) {
    if (mic_meter_) {
        mic_meter_->SetPaused(suspended);
    }
    if (system_meter_) {
        system_meter_->SetPaused(suspended);
    }
    if (suspended) {
        if (!recording_) {
            poll_timer_->stop();
            preview_source_.Stop();
        }
        return;
    }
    if (!recording_) {
        poll_timer_->start();
        StartIdlePreview();
    }
}

void MainWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) {
        return;
    }
    SyncMaximizedState();
    const bool minimized = isMinimized();
    if (minimized == minimized_) {
        return;
    }
    minimized_ = minimized;
    SetIdleSuspended(minimized_);
}

void MainWindow::ApplyWindowEffects() {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        return;
    }
    // A frameless QWidget is a WS_POPUP window, which Windows will not resize
    // from WM_NCHITTEST alone. Give it a sizing frame but collapse the
    // non-client area to zero (WM_NCCALCSIZE below), so the custom caption
    // still covers the whole window while the native resize loop works.
    static bool frame_prepared = false;
    if (!frame_prepared) {
        frame_prepared = true;
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        style &= ~static_cast<LONG_PTR>(WS_CAPTION);
        style |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                         SWP_FRAMECHANGED);
    }
    const int preference = maximized_ ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                          sizeof(preference));
    const BOOL dark = FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                          sizeof(dark));
#endif
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message,
                             qintptr* result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        auto* msg = static_cast<MSG*>(message);
        const HWND hwnd = msg->hwnd;
        if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
            // WS_THICKFRAME is only present to enable the native resize loop.
            // Normally the whole window rect is client area; when Windows
            // maximizes us, clamp the client to the monitor work area instead
            // so the taskbar stays visible.
            if (IsZoomed(hwnd)) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
                MONITORINFO info{};
                info.cbSize = sizeof(info);
                if (GetMonitorInfo(
                        MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                        &info)) {
                    params->rgrc[0] = info.rcWork;
                }
            }
            *result = 0;
            return true;
        }
        if (msg->message == WM_SYSCOMMAND) {
            const WPARAM command = msg->wParam & 0xFFF0;
            if (command == SC_MAXIMIZE || command == SC_RESTORE) {
                QTimer::singleShot(0, this, [this] { SyncMaximizedState(); });
            }
        }
        if (msg->message == WM_NCHITTEST && !IsZoomed(hwnd)) {
            RECT window_rect{};
            GetWindowRect(hwnd, &window_rect);
            const int x = GET_X_LPARAM(msg->lParam);
            const int y = GET_Y_LPARAM(msg->lParam);
            const UINT dpi = GetDpiForWindow(hwnd);
            const int border =
                MulDiv(kWindowResizeBorder, dpi > 0 ? static_cast<int>(dpi) : 96,
                       96);
            const bool left = x >= window_rect.left &&
                              x < window_rect.left + border;
            const bool right = x <= window_rect.right &&
                               x > window_rect.right - border;
            const bool top =
                y >= window_rect.top && y < window_rect.top + border;
            const bool bottom = y <= window_rect.bottom &&
                                y > window_rect.bottom - border;
            if (left || right || top || bottom) {
                if (top && left) {
                    *result = HTTOPLEFT;
                } else if (top && right) {
                    *result = HTTOPRIGHT;
                } else if (bottom && left) {
                    *result = HTBOTTOMLEFT;
                } else if (bottom && right) {
                    *result = HTBOTTOMRIGHT;
                } else if (left) {
                    *result = HTLEFT;
                } else if (right) {
                    *result = HTRIGHT;
                } else if (top) {
                    *result = HTTOP;
                } else {
                    *result = HTBOTTOM;
                }
                return true;
            }
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
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
        this, QStringLiteral("Hmmob 录屏"),
        QStringLiteral("正在录制，确定停止并退出？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        closing_ = false;
        if (recording_) {
            start_button_->setEnabled(!stop_requested_);
            if (!poll_timer_->isActive()) {
                poll_timer_->start();
            }
        }
        event->ignore();
        return;
    }

    poll_timer_->stop();
    start_button_->setEnabled(false);
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
