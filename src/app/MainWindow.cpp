#include "app/MainWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
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
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
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
#include "app/TitleBar.h"
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
QWidget#advancedBar { background: #F7F9FC; border-top: 1px solid #F0F2F7; border-bottom-left-radius: 12px; border-bottom-right-radius: 12px; }
QPushButton#advancedButton { background: transparent; border: none; border-radius: 0px; }
QPushButton#advancedButton:hover { background: #F0F3F8; }
QWidget#advancedContent { background: transparent; }
)";

QString PreviewUnavailableText(const std::string& error) {
    return QStringLiteral("预览不可用：%1").arg(QString::fromStdString(error));
}

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

QFont UiFont(int pixel_size, QFont::Weight weight = QFont::Normal) {
    QFont font;
    font.setFamilies({QStringLiteral("Microsoft YaHei UI"),
                      QStringLiteral("Microsoft YaHei")});
    font.setPixelSize(pixel_size);
    font.setWeight(weight);
    return font;
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
    setMinimumSize(1180, 720);

    BuildUi();
    PopulateDisplays();
    ConnectSignals();

    const QRect available = screen()->availableGeometry();
    move(available.center() -
         QPoint(width() / 2, height() / 2));
    normal_geometry_ = geometry();
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

    auto* column = new QWidget(content);
    column->setFixedWidth(kRightColumnWidth);
    auto* column_layout = new QVBoxLayout(column);
    column_layout->setContentsMargins(0, 0, 0, 0);
    column_layout->setSpacing(12);
    column_layout->addWidget(BuildContentCard(), 1);
    column_layout->addWidget(BuildAudioCard(), 1);
    column_layout->addWidget(BuildVideoCard(), 1);
    content_layout->addWidget(column, 0);

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
    auto* settings = make_action(Icon::Gear, QStringLiteral("设置"));
    auto* open_files = make_action(Icon::Folder, QStringLiteral("打开文件"));
    auto* menu = make_action(Icon::Menu, QString());
    layout->addWidget(settings);
    layout->addSpacing(16);
    layout->addWidget(open_files);
    layout->addSpacing(16);
    layout->addWidget(menu);

    connect(settings, &QPushButton::clicked, this, [this] {
        ShowStatus(QStringLiteral("该功能将在后续版本提供"), false, true);
    });
    connect(open_files, &QPushButton::clicked, this, [this] {
        ShowStatus(QStringLiteral("该功能将在后续版本提供"), false, true);
    });
    connect(menu, &QPushButton::clicked, this, [this] {
        ShowStatus(QStringLiteral("该功能将在后续版本提供"), false, true);
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
    layout->setContentsMargins(15, 15, 15, 6);
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
    row2->addSpacing(10);
    auto* info = MakeIconLabel(Icon::Info, 18, QColor(0xB4, 0xBA, 0xC6), body);
    info->setToolTip(QStringLiteral(
        "HEVC 压缩效率更高、文件更小；H.264 兼容性更好。"));
    row2->addWidget(info);
    row2->addStretch(1);
    layout->addLayout(row2);
    layout->addSpacing(6);

    advanced_content_ = new QWidget(body);
    advanced_content_->setObjectName(QStringLiteral("advancedContent"));
    auto* advanced_layout = new QVBoxLayout(advanced_content_);
    advanced_layout->setContentsMargins(0, 4, 0, 2);
    advanced_layout->setSpacing(6);

    auto* bitrate_row = new QHBoxLayout;
    bitrate_row->setSpacing(0);
    auto* bitrate_label = new QLabel(QStringLiteral("码率"), advanced_content_);
    bitrate_label->setObjectName(QStringLiteral("rowLabel"));
    bitrate_label->setFixedWidth(64);
    bitrate_row->addWidget(bitrate_label);
    bitrate_row->addSpacing(10);
    bitrate_combo_ = new StyledComboBox(advanced_content_);
    bitrate_combo_->setFixedHeight(32);
    bitrate_combo_->setFixedWidth(130);
    bitrate_combo_->addItem(QStringLiteral("20 Mbps"),
                            QVariant::fromValue<qlonglong>(20000000));
    bitrate_combo_->addItem(QStringLiteral("50 Mbps"),
                            QVariant::fromValue<qlonglong>(50000000));
    bitrate_combo_->addItem(QStringLiteral("100 Mbps"),
                            QVariant::fromValue<qlonglong>(100000000));
    bitrate_combo_->setCurrentIndex(1);
    bitrate_row->addWidget(bitrate_combo_);
    bitrate_row->addStretch(1);
    advanced_layout->addLayout(bitrate_row);

    hdr_check_ = new ToggleCheckBox(
        QStringLiteral("HDR（色调映射为 SDR）"), advanced_content_);
    hdr_check_->setChecked(false);
    advanced_layout->addWidget(hdr_check_);

    display_row_ = new QWidget(advanced_content_);
    auto* display_layout = new QHBoxLayout(display_row_);
    display_layout->setContentsMargins(0, 0, 0, 0);
    display_layout->setSpacing(0);
    auto* display_label = new QLabel(QStringLiteral("显示器"), display_row_);
    display_label->setObjectName(QStringLiteral("rowLabel"));
    display_label->setFixedWidth(64);
    display_layout->addWidget(display_label);
    display_layout->addSpacing(10);
    display_combo_ = new StyledComboBox(display_row_);
    display_combo_->setFixedHeight(32);
    display_combo_->setMinimumWidth(150);
    display_layout->addWidget(display_combo_, 1);
    display_row_->setVisible(false);
    advanced_layout->addWidget(display_row_);

    advanced_content_->setVisible(false);
    layout->addWidget(advanced_content_);
    layout->addStretch(1);

    card_layout->addWidget(body, 1);

    auto* footer = new QWidget(card);
    footer->setObjectName(QStringLiteral("advancedBar"));
    footer->setAttribute(Qt::WA_StyledBackground, true);
    footer->setFixedHeight(34);
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(0, 0, 0, 0);
    footer_layout->setSpacing(0);

    advanced_button_ = new QPushButton(footer);
    advanced_button_->setObjectName(QStringLiteral("advancedButton"));
    advanced_button_->setCheckable(true);
    advanced_button_->setCursor(Qt::PointingHandCursor);
    advanced_button_->setFocusPolicy(Qt::NoFocus);
    auto* button_layout = new QHBoxLayout(advanced_button_);
    button_layout->setContentsMargins(14, 0, 14, 0);
    button_layout->setSpacing(8);
    advanced_chevron_ = new QLabel(advanced_button_);
    advanced_chevron_->setPixmap(IconRenderer::Pixmap(
        Icon::ChevronDown, 16, QColor(0x86, 0x90, 0x9C)));
    advanced_chevron_->setFixedSize(16, 16);
    button_layout->addWidget(advanced_chevron_);
    auto* advanced_text = new QLabel(QStringLiteral("高级设置"),
                                     advanced_button_);
    advanced_text->setStyleSheet(
        QStringLiteral("color:#86909C; font-size:13px;"));
    button_layout->addWidget(advanced_text);
    button_layout->addStretch(1);
    footer_layout->addWidget(advanced_button_);
    card_layout->addWidget(footer);

    connect(advanced_button_, &QPushButton::toggled, this,
            &MainWindow::SetAdvancedExpanded);

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
    layout->addSpacing(24);

    auto make_divider = [&] {
        auto* divider = new QWidget(bar);
        divider->setFixedSize(1, 40);
        divider->setStyleSheet(QStringLiteral("background:#E5E6EB;"));
        return divider;
    };
    auto make_stat = [&](Icon icon, const QString& caption, QLabel** value) {
        auto* block = new QWidget(bar);
        auto* block_layout = new QHBoxLayout(block);
        block_layout->setContentsMargins(0, 0, 0, 0);
        block_layout->setSpacing(10);
        block_layout->addWidget(
            MakeIconLabel(icon, 22, QColor(0x86, 0x90, 0x9C), block),
            0, Qt::AlignVCenter);
        auto* text_layout = new QVBoxLayout;
        text_layout->setContentsMargins(0, 0, 0, 0);
        text_layout->setSpacing(1);
        auto* caption_label = new QLabel(caption, block);
        caption_label->setObjectName(QStringLiteral("caption"));
        auto* value_label = new QLabel(block);
        value_label->setObjectName(QStringLiteral("statValue"));
        text_layout->addWidget(caption_label);
        text_layout->addWidget(value_label);
        block_layout->addLayout(text_layout);
        *value = value_label;
        return block;
    };

    layout->addWidget(make_divider());
    layout->addSpacing(20);
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
    display_combo_->clear();

    hmmob::D3D11Device device;
    if (device.Create()) {
        displays_ = device.EnumerateDisplays();
    }
    for (const hmmob::DisplayInfo& info : displays_) {
        resolution_combo_->addItem(
            QStringLiteral("%1 × %2").arg(info.width).arg(info.height),
            info.index);
        display_combo_->addItem(
            QStringLiteral("显示器 %1 · %2 × %3")
                .arg(info.index + 1)
                .arg(info.width)
                .arg(info.height),
            info.index);
    }
    display_row_->setVisible(displays_.size() > 1);

    if (displays_.empty()) {
        resolution_combo_->addItem(QStringLiteral("无可用显示器"));
        resolution_combo_->setEnabled(false);
        start_button_->setEnabled(false);
        has_display_ = false;
        return;
    }

    resolution_combo_->setCurrentIndex(0);
    display_combo_->setCurrentIndex(0);
    has_display_ = true;
    start_button_->setEnabled(true);
    UpdateBadgeText();
}

void MainWindow::ConnectSignals() {
    connect(title_bar_, &TitleBar::MinimizeRequested, this,
            &QWidget::showMinimized);
    connect(title_bar_, &TitleBar::MaximizeRequested, this,
            &MainWindow::ToggleMaximize);
    connect(title_bar_, &TitleBar::CloseRequested, this, &QWidget::close);

    for (NavTabButton* tab : {window_tab_, region_tab_, camera_tab_}) {
        connect(tab, &QAbstractButton::clicked, this, [this] {
            ShowStatus(QStringLiteral("该功能将在后续版本提供"), false, true);
        });
    }

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
        if (!status_message_locked_) {
            status_label_->clear();
            status_label_->setStyleSheet(QString());
        }
    });
    connect(resolution_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { OnDisplaySelectionChanged(); });
    connect(display_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { OnDisplaySelectionChanged(); });
    connect(mode_group_, &QButtonGroup::idClicked, this, [this](int id) {
        selected_mode_ = id;
    });
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

void MainWindow::SetAdvancedExpanded(bool expanded) {
    if (advanced_content_) {
        advanced_content_->setVisible(expanded);
    }
    if (advanced_chevron_) {
        advanced_chevron_->setPixmap(IconRenderer::Pixmap(
            expanded ? Icon::ChevronUp : Icon::ChevronDown, 16,
            QColor(0x86, 0x90, 0x9C)));
    }
}

void MainWindow::ShowStatus(const QString& text, bool error, bool transient) {
    // A message the user just triggered supersedes a locked recording result;
    // otherwise the 4 s timer below could never clear it.
    if (transient) {
        status_message_locked_ = false;
    }
    status_label_->setText(text);
    status_label_->setStyleSheet(
        error ? QStringLiteral("color:#E5484D; font-size:13px;")
              : QStringLiteral("color:#86909C; font-size:13px;"));
    if (transient) {
        status_timer_->start();
    } else {
        status_timer_->stop();
    }
}

void MainWindow::OnDisplaySelectionChanged() {
    const int index = resolution_combo_->currentData().toInt();
    {
        const QSignalBlocker blocker(display_combo_);
        const int display_index = display_combo_->findData(index);
        if (display_index >= 0) {
            display_combo_->setCurrentIndex(display_index);
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
    const int index = resolution_combo_->currentData().toInt();
    for (const hmmob::DisplayInfo& info : displays_) {
        if (info.index == index) {
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
    if (status_label_) {
        status_label_->setGeometry(
            34, height() - kBottomBarHeight - 26, width() - 60, kStatusHeight);
        status_label_->raise();
    }
}

void MainWindow::StartIdlePreview() {
    if (!has_display_ || recording_) {
        return;
    }
    std::string error;
    const int display_index = resolution_combo_->currentData().toInt();
    if (!preview_source_.Start(display_index, 640, 360, 30, error)) {
        preview_->SetFrame(nullptr);
        last_preview_.reset();
        if (!status_message_locked_) {
            ShowStatus(PreviewUnavailableText(error), true, false);
        }
        return;
    }
    if (!status_message_locked_ &&
        status_label_->text().startsWith(QStringLiteral("预览不可用："))) {
        ShowStatus(QString(), false, false);
    }
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
    config.display_index = resolution_combo_->currentData().toInt();
    config.fps = fps_combo_->currentData().toInt();
    config.codec = codec_combo_->currentData().toString().toStdString();
    config.bitrate = bitrate_combo_->currentData().toLongLong();
    config.hdr = hdr_check_->isChecked();
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
    if (selected_mode_ != 0) {
        ShowStatus(QStringLiteral("该录制模式尚未实现"), false, true);
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
            this, QStringLiteral("Hmmob 录屏"),
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
        start_button_->setEnabled(has_display_);
        // Keep this failure visible; a preview-restart message must not
        // replace it.
        status_message_locked_ = true;
        StartIdlePreview();
        return false;
    }

    SetError(QString());
    status_message_locked_ = false;
    recording_ = true;
    stop_requested_ = false;
    tick_count_ = 0;
    SetConfigEnabled(false);
    start_button_->SetRecording(true);
    start_button_->SetBusy(false);
    start_button_->setEnabled(true);
    duration_label_->setText(QStringLiteral("00:00:00"));
    size_label_->setText(QStringLiteral("0 MB"));
    ShowStatus(QStringLiteral("录制中…"), false, false);
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
    start_button_->setEnabled(false);
    start_button_->SetBusy(true);
    ShowStatus(QStringLiteral("正在保存…"), false, false);
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
        if (!status_message_locked_) {
            const std::string preview_error = preview_source_.error();
            if (!preview_error.empty()) {
                const QString message = PreviewUnavailableText(preview_error);
                if (status_label_->text() != message) {
                    ShowStatus(message, true, false);
                }
            } else if (status_label_->text().startsWith(
                           QStringLiteral("预览不可用："))) {
                ShowStatus(QString(), false, false);
            }
        }
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
                   false);
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
    // The tick timer keeps running: it drives the idle preview from here on.

    const hmmob::RecorderStats stats = recorder_.stats();
    const QString error = QString::fromStdString(recorder_.error());

    SetConfigEnabled(true);
    start_button_->SetRecording(false);
    start_button_->SetBusy(false);
    start_button_->setEnabled(has_display_);
    UpdateStatsLabels(stats);

    if (!error.isEmpty()) {
        SetError(error);
        ShowStatus(QStringLiteral("保存失败：%1").arg(error), true, false);
    } else if (stats.encoded_frames == 0) {
        QFile::remove(output_path_);
        SetError(QString());
        ShowStatus(QStringLiteral("未录制到画面，未生成文件"), false, false);
    } else {
        SetError(QString());
        ShowStatus(QStringLiteral("已保存：%1").arg(output_path_), false, false);

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

    // The status area now reports the recording outcome; the idle preview
    // must not replace it with a transient error message.
    status_message_locked_ = true;

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
    resolution_combo_->setEnabled(enabled && has_display_);
    display_combo_->setEnabled(enabled && has_display_);
    fps_combo_->setEnabled(enabled);
    codec_combo_->setEnabled(enabled);
    bitrate_combo_->setEnabled(enabled);
    hdr_check_->setEnabled(enabled);
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
        ShowStatus(error, true, false);
    }
}

void MainWindow::UpdateStatsLabels(const hmmob::RecorderStats& stats) {
    duration_label_->setText(FormatDuration(stats.elapsed_us));
    size_label_->setText(FormatSize(stats.encoded_bytes));
}

void MainWindow::StartSmoke(int seconds, const QString& path) {
    smoke_ = true;
    smoke_finished_ = false;
    smoke_seconds_ = seconds;
    smoke_path_ = path;
    output_edit_->setText(path);

    resolution_combo_->setCurrentIndex(0);
    fps_combo_->setCurrentIndex(fps_combo_->findData(60));
    codec_combo_->setCurrentIndex(
        codec_combo_->findData(QStringLiteral("hevc")));
    bitrate_combo_->setCurrentIndex(
        bitrate_combo_->findData(QVariant::fromValue<qlonglong>(50000000)));
    hdr_check_->setChecked(false);
    system_toggle_->setChecked(true);
    mic_toggle_->setChecked(true);
    full_tile_->setChecked(true);
    selected_mode_ = 0;

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

void MainWindow::ToggleMaximize() {
    if (!maximized_) {
        normal_geometry_ = geometry();
        setGeometry(screen()->availableGeometry());
        maximized_ = true;
    } else {
        setGeometry(normal_geometry_);
        maximized_ = false;
    }
    title_bar_->SetMaximized(maximized_);
    ApplyWindowEffects();
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
        if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
            // WS_THICKFRAME is only present to enable the native resize loop;
            // the whole window rect must remain client area.
            *result = 0;
            return true;
        }
        if (msg->message == WM_NCHITTEST && !maximized_) {
            const HWND hwnd = reinterpret_cast<HWND>(winId());
            RECT window_rect{};
            GetWindowRect(hwnd, &window_rect);
            const int x = GET_X_LPARAM(msg->lParam);
            const int y = GET_Y_LPARAM(msg->lParam);
            const int border = kWindowResizeBorder;
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
