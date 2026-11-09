#include "app/MainWindow.h"

#include <QAbstractItemView>
#include <QAction>
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
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStringConverter>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

#include "app/Controls.h"
#include "app/CountdownOverlay.h"
#include "app/IconRenderer.h"
#include "app/LevelMeter.h"
#include "app/Logo.h"
#include "app/ModeTile.h"
#include "app/NavTabButton.h"
#include "app/PreviewWidget.h"
#include "app/RegionSelector.h"
#include "app/SettingsDialog.h"
#include "app/TitleBar.h"
#include "app/UiFont.h"
#include "app/WindowPickerDialog.h"
#include "core/audio/WasapiCapture.h"
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

const char* const kStyleSheetTemplate = R"(
QWidget#root { background: {window}; }
QWidget#navBar { background: {window}; }
QWidget#contentArea { background: transparent; }
QWidget#bottomBar { background: {bottomBar}; border-top: 1px solid {softBorder}; }
QFrame#card { background: {card}; border: 1px solid {cardBorder}; border-radius: 12px; }
QLabel { color: {text}; }
QLabel#cardTitle { font-size: 16px; font-weight: 600; color: {text}; }
QLabel#rowLabel { font-size: 14px; color: {text}; }
QLabel#caption { font-size: 13px; color: {textMuted}; }
QLabel#statValue { font-size: 16px; color: {text}; }
QLabel#statusLabel { font-size: 13px; color: {textMuted}; }
QLabel#secondaryLabel { font-size: 14px; color: {textSecondary}; }
QWidget#vDivider { background: {divider}; }
QPushButton#navAction { background: transparent; border: none; color: {textSecondary}; font-size: 15px; padding: 6px 8px; border-radius: 8px; }
QPushButton#navAction:hover { background: {navActionHover}; }
QPushButton#navAction:pressed { background: {navActionPressed}; }
QPushButton#navAction:disabled { color: {disabledText}; }
QPushButton#sourceToggle { background: transparent; border: none; border-radius: 8px; }
QPushButton#sourceToggle:hover { background: {hoverBg}; }
QPushButton#iconButton { background: transparent; border: none; border-radius: 8px; }
QPushButton#iconButton:hover { background: {hoverBg}; }
QComboBox { background: {inputBg}; border: 1px solid {inputBorder}; border-radius: 10px; padding: 0 10px 0 14px; font-size: 14px; color: {text}; }
QComboBox:hover { border-color: {iconMuted}; }
QComboBox:disabled { background: {disabledBg}; color: {disabledText}; border-color: {disabledBorder}; }
QComboBox::drop-down { border: none; background: transparent; width: 34px; }
QComboBox::down-arrow { image: none; width: 0px; height: 0px; }
QComboBox QAbstractItemView { background: {menuBg}; border: 1px solid {menuBorder}; border-radius: 8px; padding: 4px; outline: 0; color: {text}; selection-background-color: {selectionBg}; selection-color: {selectionText}; }
QComboBox QAbstractItemView::item { min-height: 30px; padding: 0 10px; }
QComboBox QAbstractItemView::item:hover { background: {hoverBg}; }
QFrame#pathBox { background: {pathBoxBg}; border: 1px solid {inputBorder}; border-radius: 10px; }
QLineEdit#pathEdit { background: transparent; border: none; padding: 0px; font-size: 14px; color: {text}; selection-background-color: {primary}; selection-color: #FFFFFF; }
QLineEdit#pathEdit:disabled { color: {disabledText}; }
QPushButton#browseButton { background: transparent; border: none; border-radius: 8px; }
QPushButton#browseButton:hover { background: {hoverBg}; }
QScrollArea#settingsScroll { background: transparent; border: none; }
QScrollArea#settingsScroll > QWidget > QWidget { background: transparent; }
QWidget#scrollContent { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0px; }
QScrollBar::handle:vertical { background: {scrollHandle}; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: {scrollHandleHover}; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: transparent; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QMenu { background: {menuBg}; border: 1px solid {menuBorder}; border-radius: 8px; padding: 4px; color: {text}; }
QMenu::item { padding: 6px 24px 6px 14px; border-radius: 6px; }
QMenu::item:selected { background: {selectionBg}; color: {selectionText}; }
QMenu::item:disabled { color: {disabledText}; }
QMenu::separator { height: 1px; background: {divider}; margin: 4px 8px; }
QToolTip { background: {menuBg}; color: {text}; border: 1px solid {menuBorder}; padding: 4px 8px; }
QMessageBox { background: {card}; }
QMessageBox QLabel { color: {text}; }
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

MainWindow::MainWindow(bool automation, QWidget* parent)
    : QWidget(parent), automation_(automation) {
    settings_ = LoadAppSettings();
    bitrate_ = settings_.video_bitrate;
    hdr_ = settings_.hdr;
    camera_index_ = settings_.camera_index;
    container_ = settings_.container_extension;
    audio_codec_ = settings_.audio_codec;
    audio_sample_rate_ = settings_.audio_sample_rate;
    audio_bitrate_ = settings_.audio_bitrate;
    countdown_seconds_ = settings_.countdown_seconds;

    setWindowTitle(QStringLiteral("Hmmob 录屏"));
    setObjectName(QStringLiteral("root"));
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowMinimizeButtonHint |
                   Qt::WindowMaximizeButtonHint | Qt::WindowSystemMenuHint);
    setStyleSheet(Theme::Instance().Expand(
        QString::fromUtf8(kStyleSheetTemplate)));
    resize(1400, 830);
    setMinimumSize(1024, 680);

    BuildUi();
    PopulateDisplays();
    PopulateCameras();
    PopulateAudioDevices();
    ApplyPersistedSettings();
    ConnectSignals();
    InstallShortcuts();
    connect(&Theme::Instance(), &Theme::changed, this, &MainWindow::ApplyTheme);
    if (!automation_) {
        CreateTray();
        // Keep the registry in step with the persisted preference; the user
        // may have removed the entry from outside the app.
        ApplyAutoStart(settings_.auto_start);
    }

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

    countdown_overlay_ = new CountdownOverlay(preview_);
    connect(countdown_overlay_, &CountdownOverlay::tick, this,
            [this](int remaining) {
                if (countdown_active_) {
                    ShowStatus(
                        QStringLiteral("%1 秒后开始录制…").arg(remaining),
                        false, 0);
                }
            });
    connect(countdown_overlay_, &CountdownOverlay::finished, this,
            &MainWindow::OnCountdownFinished);
    connect(countdown_overlay_, &CountdownOverlay::cancelled, this,
            &MainWindow::CancelCountdown);

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

    // Window-wide so Esc still cancels when the overlay lost focus (for
    // example after clicking the title bar).
    countdown_escape_shortcut_ =
        new QShortcut(QKeySequence(Qt::Key_Escape), this);
    countdown_escape_shortcut_->setContext(Qt::WindowShortcut);
    countdown_escape_shortcut_->setEnabled(false);
    connect(countdown_escape_shortcut_, &QShortcut::activated, this, [this] {
        if (countdown_active_) {
            countdown_overlay_->Cancel();
        }
    });
}

QLabel* MainWindow::MakeIconLabel(Icon icon, int size, IconTone tone,
                                  QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setPixmap(
        IconRenderer::Pixmap(icon, size, Theme::Instance().ToneColor(tone)));
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    icon_labels_.push_back({label, icon, size, tone});
    return label;
}

void MainWindow::ApplyTheme() {
    setStyleSheet(
        Theme::Instance().Expand(QString::fromUtf8(kStyleSheetTemplate)));
    RefreshThemedIcons();
    if (status_label_) {
        if (status_label_->text().isEmpty()) {
            status_label_->setStyleSheet(QString());
        } else {
            const ThemeColors& c = Theme::Instance().colors();
            status_label_->setStyleSheet(
                QStringLiteral("color:%1; font-size:13px;")
                    .arg((status_is_error_ ? c.danger : c.textMuted).name()));
        }
    }
    ApplyWindowEffects();
    update();
}

void MainWindow::RefreshThemedIcons() {
    const Theme& theme = Theme::Instance();
    for (const ThemedIconLabel& ref : icon_labels_) {
        if (ref.label) {
            ref.label->setPixmap(IconRenderer::Pixmap(
                ref.icon, ref.size, theme.ToneColor(ref.tone)));
        }
    }
    const QColor secondary = theme.ToneColor(IconTone::Secondary);
    if (settings_button_) {
        settings_button_->setIcon(
            IconRenderer::QIconFor(Icon::Gear, 22, secondary));
    }
    if (open_files_button_) {
        open_files_button_->setIcon(
            IconRenderer::QIconFor(Icon::Folder, 22, secondary));
    }
    if (browse_button_) {
        browse_button_->setIcon(
            IconRenderer::QIconFor(Icon::Folder, 20, secondary));
    }
    UpdateAudioUi(true);
    UpdateAudioUi(false);
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
        button->setIcon(IconRenderer::QIconFor(
            icon, 22, Theme::Instance().ToneColor(IconTone::Secondary)));
        button->setIconSize(QSize(22, 22));
        QFont font = button->font();
        font.setPixelSize(15);
        button->setFont(font);
        return button;
    };
    settings_button_ = make_action(Icon::Gear, QStringLiteral("设置"));
    open_files_button_ =
        make_action(Icon::Folder, QStringLiteral("打开文件"));
    auto* open_files = open_files_button_;
    layout->addWidget(settings_button_);
    layout->addSpacing(16);
    layout->addWidget(open_files);

    connect(settings_button_, &QPushButton::clicked, this,
            &MainWindow::OpenSettings);
    connect(open_files, &QPushButton::clicked, this, [this] {
        const QString directory = OutputDirectoryFromUi();
        if (directory.isEmpty() ||
            !QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
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
        MakeIconLabel(Icon::Monitor, 20, IconTone::Text, card));
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
        MakeIconLabel(Icon::Speaker, 20, IconTone::Text, card));
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

        // The icon/label/device/meter group is dimmed when the source is not
        // recorded; the toggle stays at full contrast and clickable.
        auto* body = new QWidget(row);
        auto* b = new QHBoxLayout(body);
        b->setContentsMargins(0, 0, 0, 0);
        b->setSpacing(0);
        b->addWidget(MakeIconLabel(microphone ? Icon::Mic : Icon::Speaker, 20,
                                   IconTone::Secondary, body));
        b->addSpacing(10);
        auto* label = new QLabel(
            microphone ? QStringLiteral("麦克风") : QStringLiteral("系统声音"),
            body);
        label->setObjectName(QStringLiteral("rowLabel"));
        label->setFixedWidth(64);
        b->addWidget(label);
        b->addSpacing(10);

        auto* device = new StyledComboBox(body);
        device->setFixedHeight(36);
        b->addWidget(device, 1);
        b->addSpacing(10);

        auto* meter = new LevelMeter(microphone ? 1 : 3, body);
        b->addWidget(meter, 0, Qt::AlignVCenter);
        b->addSpacing(8);
        h->addWidget(body, 1);

        auto* toggle = new QPushButton(row);
        toggle->setObjectName(QStringLiteral("sourceToggle"));
        toggle->setCheckable(true);
        toggle->setChecked(true);
        toggle->setCursor(Qt::PointingHandCursor);
        toggle->setFocusPolicy(Qt::NoFocus);
        toggle->setFixedSize(32, 32);
        toggle->setIconSize(QSize(22, 22));
        h->addWidget(toggle, 0, Qt::AlignVCenter);

        auto* dim = new QGraphicsOpacityEffect(body);
        dim->setOpacity(1.0);
        body->setGraphicsEffect(dim);

        if (microphone) {
            mic_device_combo_ = device;
            mic_meter_ = meter;
            mic_toggle_ = toggle;
            mic_mode_dim_ = dim;
        } else {
            system_device_combo_ = device;
            system_meter_ = meter;
            system_toggle_ = toggle;
            system_mode_dim_ = dim;
        }
        connect(toggle, &QPushButton::toggled, this,
                [this, microphone] {
                    UpdateAudioUi(microphone);
                    UpdateAudioModeFromToggles();
                    PersistSettings();
                });
        return row;
    };

    auto* rows = new QVBoxLayout;
    rows->setSpacing(6);
    rows->addWidget(build_row(true));
    rows->addWidget(build_row(false));
    layout->addStretch(1);
    layout->addLayout(rows);
    layout->addSpacing(8);

    // Compact explicit source choice. The toggles above remain the direct
    // mute controls; both stay in sync.
    auto* mode_row = new QWidget(card);
    auto* mode_layout = new QHBoxLayout(mode_row);
    mode_layout->setContentsMargins(0, 0, 0, 0);
    mode_layout->setSpacing(0);
    mode_layout->addWidget(
        MakeIconLabel(Icon::Speaker, 20, IconTone::Secondary, mode_row));
    mode_layout->addSpacing(10);
    auto* mode_label = new QLabel(QStringLiteral("录制声音"), mode_row);
    mode_label->setObjectName(QStringLiteral("rowLabel"));
    mode_label->setFixedWidth(64);
    mode_layout->addWidget(mode_label);
    mode_layout->addSpacing(10);

    audio_mode_combo_ = new StyledComboBox(mode_row);
    audio_mode_combo_->setFixedHeight(36);
    audio_mode_combo_->addItem(
        QStringLiteral("系统声音 + 麦克风"),
        static_cast<int>(AudioSourceMode::Both));
    audio_mode_combo_->addItem(QStringLiteral("仅麦克风"),
                               static_cast<int>(AudioSourceMode::MicOnly));
    audio_mode_combo_->addItem(QStringLiteral("仅系统声音"),
                               static_cast<int>(AudioSourceMode::SystemOnly));
    audio_mode_combo_->addItem(QStringLiteral("不录制声音"),
                               static_cast<int>(AudioSourceMode::None));
    mode_layout->addWidget(audio_mode_combo_, 1);
    // Match the device combos' right edge (toggle 32 px + 8 px gap).
    mode_layout->addSpacing(40);
    layout->addWidget(mode_row);
    layout->addStretch(1);

    connect(audio_mode_combo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (index < 0 || !audio_mode_combo_) {
                    return;
                }
                SetAudioMode(static_cast<AudioSourceMode>(
                    audio_mode_combo_->itemData(index).toInt()));
                PersistSettings();
            });

    UpdateAudioUi(true);
    UpdateAudioUi(false);
    UpdateAudioModeUi();
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
        MakeIconLabel(Icon::Gear, 20, IconTone::Text, body));
    auto* title = new QLabel(QStringLiteral("视频设置"), body);
    title->setObjectName(QStringLiteral("cardTitle"));
    header->addWidget(title);
    header->addStretch(1);
    layout->addLayout(header);
    layout->addSpacing(8);

    auto* row1 = new QHBoxLayout;
    row1->setSpacing(0);
    row1->addWidget(
        MakeIconLabel(Icon::Hd, 20, IconTone::Secondary, body));
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
    row2->addWidget(
        MakeIconLabel(Icon::Film, 20, IconTone::Secondary, body));
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
        MakeIconLabel(Icon::Folder, 22, IconTone::Secondary, bar));
    layout->addSpacing(10);
    auto* save_label = new QLabel(QStringLiteral("保存目录"), bar);
    save_label->setObjectName(QStringLiteral("secondaryLabel"));
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
    output_edit_->setPlaceholderText(QStringLiteral("保存目录"));
    const QString directory = settings_.output_directory.isEmpty()
                                  ? DefaultOutputDirectory()
                                  : settings_.output_directory;
    output_edit_->setText(QDir::toNativeSeparators(directory));
    output_edit_->setCursorPosition(0);
    path_layout->addWidget(output_edit_, 1);
    browse_button_ = new QPushButton(path_box);
    browse_button_->setObjectName(QStringLiteral("browseButton"));
    browse_button_->setCursor(Qt::PointingHandCursor);
    browse_button_->setFocusPolicy(Qt::NoFocus);
    browse_button_->setFixedSize(36, 36);
    browse_button_->setIconSize(QSize(20, 20));
    browse_button_->setIcon(
        IconRenderer::QIconFor(
            Icon::Folder, 20, Theme::Instance().ToneColor(IconTone::Secondary)));
    browse_button_->setToolTip(QStringLiteral("选择保存目录"));
    path_layout->addWidget(browse_button_);
    layout->addWidget(path_box, 2);
    layout->addSpacing(80);

    auto make_divider = [&] {
        auto* divider = new QWidget(bar);
        divider->setObjectName(QStringLiteral("vDivider"));
        divider->setFixedSize(1, 40);
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
            MakeIconLabel(icon, 22, IconTone::Muted, block),
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

void MainWindow::PopulateAudioDevices() {
    // The first item always selects the flow's default endpoint (empty id).
    // Enumeration failures leave just that item, which keeps recording with
    // shared-mode defaults working.
    auto fill = [](StyledComboBox* combo, bool loopback) {
        const QString previous = combo->currentData().toString();
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItem(QStringLiteral("默认设备"), QString());
        const std::vector<hmmob::WasapiCapture::AudioDeviceInfo> devices =
            hmmob::WasapiCapture::EnumerateDevices(loopback);
        for (const hmmob::WasapiCapture::AudioDeviceInfo& device : devices) {
            QString name = QString::fromStdString(device.name);
            if (device.is_default) {
                name += QStringLiteral(" (默认)");
            }
            combo->addItem(name, QString::fromStdString(device.id));
        }
        if (!previous.isEmpty()) {
            const int index = combo->findData(previous);
            combo->setCurrentIndex(index >= 0 ? index : 0);
        } else {
            combo->setCurrentIndex(0);
        }
    };
    fill(mic_device_combo_, false);
    fill(system_device_combo_, true);
}

void MainWindow::ApplyPersistedSettings() {
    if (fps_combo_) {
        const int index = fps_combo_->findData(settings_.fps);
        if (index >= 0) {
            fps_combo_->setCurrentIndex(index);
        }
    }
    if (codec_combo_) {
        const int index = codec_combo_->findData(settings_.video_codec);
        if (index >= 0) {
            codec_combo_->setCurrentIndex(index);
        }
    }

    // Restore the capture mode without touching the preview: the idle
    // duplication starts from showEvent, never from the constructor.
    selected_mode_ = settings_.capture_mode >= 0 && settings_.capture_mode <= 3
                         ? settings_.capture_mode
                         : 0;
    if (mode_group_) {
        if (QAbstractButton* tile = mode_group_->button(selected_mode_)) {
            const QSignalBlocker blocker(mode_group_);
            tile->setChecked(true);
        }
    }
    screen_tab_->SetActive(selected_mode_ == 0);
    region_tab_->SetActive(selected_mode_ == 1);
    window_tab_->SetActive(selected_mode_ == 2);
    camera_tab_->SetActive(selected_mode_ == 3);
    start_button_->setEnabled(CanStart());

    if (mic_device_combo_) {
        const int index = mic_device_combo_->findData(settings_.mic_device_id);
        mic_device_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (system_device_combo_) {
        const int index =
            system_device_combo_->findData(settings_.system_device_id);
        system_device_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    // The mode owns both toggles; the stored booleans are derived from it.
    SetAudioMode(settings_.audio_mode);
}

void MainWindow::SyncSettingsFromUi() {
    settings_.video_bitrate = bitrate_;
    settings_.hdr = hdr_;
    settings_.camera_index = camera_index_;
    settings_.container_extension = container_;
    settings_.audio_codec = audio_codec_;
    settings_.audio_sample_rate = audio_sample_rate_;
    settings_.audio_bitrate = audio_bitrate_;
    if (codec_combo_) {
        settings_.video_codec = codec_combo_->currentData().toString();
    }
    if (fps_combo_) {
        settings_.fps = fps_combo_->currentData().toInt();
    }
    settings_.capture_mode = selected_mode_;
    if (mic_device_combo_) {
        settings_.mic_device_id = mic_device_combo_->currentData().toString();
    }
    if (system_device_combo_) {
        settings_.system_device_id =
            system_device_combo_->currentData().toString();
    }
    if (output_edit_) {
        settings_.output_directory =
            QDir::fromNativeSeparators(output_edit_->text().trimmed());
    }
    if (mic_toggle_ && system_toggle_) {
        settings_.audio_mode = AudioModeFromFlags(
            mic_toggle_->isChecked(), system_toggle_->isChecked());
    }
    settings_.microphone_enabled = AudioModeHasMicrophone(settings_.audio_mode);
    settings_.system_audio_enabled =
        AudioModeHasSystemAudio(settings_.audio_mode);
}

void MainWindow::PersistSettings() {
    // Smoke runs pin their own options; never let them overwrite the user's
    // preferences.
    if (automation_ || smoke_) {
        return;
    }
    SyncSettingsFromUi();
    SaveAppSettings(settings_);
}

bool MainWindow::StartHiddenToTray() const {
    return !automation_ && tray_icon_ != nullptr &&
           settings_.minimize_to_tray_on_start;
}

void MainWindow::CreateTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    tray_icon_ = new QSystemTrayIcon(this);
    tray_icon_->setIcon(Logo::Icon());
    tray_icon_->setToolTip(QStringLiteral("Hmmob 录屏"));

    auto* menu = new QMenu(this);
    auto* show_action = menu->addAction(QStringLiteral("显示主窗口"));
    tray_start_action_ = menu->addAction(QStringLiteral("开始录制"));
    tray_stop_action_ = menu->addAction(QStringLiteral("停止录制"));
    menu->addSeparator();
    auto* quit_action = menu->addAction(QStringLiteral("退出"));
    tray_icon_->setContextMenu(menu);

    connect(show_action, &QAction::triggered, this, &MainWindow::ShowMainWindow);
    connect(tray_start_action_, &QAction::triggered, this, [this] {
        if (recording_ || countdown_active_ || !CanStart()) {
            ShowTrayMessage(QStringLiteral("Hmmob 录屏"),
                            QStringLiteral("当前无法开始录制"));
            return;
        }
        if (!StartRecording() && !last_error_.isEmpty()) {
            ShowTrayMessage(QStringLiteral("Hmmob 录屏"), last_error_);
        }
    });
    connect(tray_stop_action_, &QAction::triggered, this,
            [this] { StopRecording(); });
    connect(quit_action, &QAction::triggered, this, [this] {
        quitting_ = true;
        // Make sure the close confirmation is visible even when the window
        // was hidden in the tray.
        if (!isVisible()) {
            showNormal();
        }
        close();
        if (!isVisible()) {
            QCoreApplication::quit();
        } else {
            quitting_ = false;
        }
    });
    connect(menu, &QMenu::aboutToShow, this, &MainWindow::UpdateTrayMenu);
    connect(tray_icon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick) {
                    ShowMainWindow();
                }
            });

    UpdateTrayMenu();
    tray_icon_->show();
}

void MainWindow::UpdateTrayMenu() {
    if (tray_start_action_) {
        tray_start_action_->setEnabled(!recording_ && !countdown_active_ &&
                                       CanStart());
    }
    if (tray_stop_action_) {
        tray_stop_action_->setEnabled(recording_);
    }
}

void MainWindow::ShowMainWindow() {
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::ShowTrayMessage(const QString& title, const QString& text) {
    if (!tray_icon_ || !QSystemTrayIcon::supportsMessages()) {
        return;
    }
    // 录制时隐藏桌面通知 also covers the "已最小化到托盘" balloon, which is
    // exactly the one that can fire in the middle of a recording.
    if (recording_ && settings_.hide_notifications_while_recording) {
        return;
    }
    tray_icon_->showMessage(title, text, QSystemTrayIcon::Information, 4000);
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
    // Editing the field directly picks a different destination folder.
    connect(output_edit_, &QLineEdit::editingFinished, this,
            [this] { PersistSettings(); });
    connect(start_button_, &QAbstractButton::clicked, this, [this] {
        if (recording_) {
            StopRecording();
        } else {
            StartRecording();
        }
    });
    connect(poll_timer_, &QTimer::timeout, this, &MainWindow::Tick);
    connect(status_timer_, &QTimer::timeout, this, [this] {
        status_is_error_ = false;
        status_label_->clear();
        status_label_->setStyleSheet(QString());
    });
    connect(resolution_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (index >= 0) {
                    SelectDisplay(resolution_combo_->itemData(index).toInt());
                }
            });
    connect(fps_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { PersistSettings(); });
    connect(codec_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (codec_combo_->currentData().toString() ==
                        QStringLiteral("hevc") &&
                    (container_ == QStringLiteral("avi") ||
                     container_ == QStringLiteral("flv") ||
                     container_ == QStringLiteral("wmv"))) {
                    ShowStatus(
                        QStringLiteral(
                            "HEVC 不支持该保存格式，请改用 H.264 或更换格式"),
                        true, 4000);
                }
                PersistSettings();
            });
    connect(mic_device_combo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { PersistSettings(); });
    connect(system_device_combo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { PersistSettings(); });
    connect(mode_group_, &QButtonGroup::idClicked, this,
            [this](int id) { SetMode(id); });
}

void MainWindow::SetMode(int mode) {
    if (mode < 0 || mode > 3 || recording_ || countdown_active_) {
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
    PersistSettings();
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
        Theme::Instance().ToneColor(enabled ? IconTone::Icon
                                             : IconTone::IconDisabled)));
    toggle->setToolTip(enabled ? QStringLiteral("静音") : QStringLiteral("开启"));
}

void MainWindow::SetAudioMode(AudioSourceMode mode) {
    const int value = static_cast<int>(mode);
    if (value < 0 || value > static_cast<int>(AudioSourceMode::None)) {
        mode = AudioSourceMode::Both;
    }
    settings_.audio_mode = mode;
    const bool microphone = AudioModeHasMicrophone(mode);
    const bool system_audio = AudioModeHasSystemAudio(mode);

    if (mic_toggle_) {
        const QSignalBlocker blocker(mic_toggle_);
        mic_toggle_->setChecked(microphone);
    }
    if (system_toggle_) {
        const QSignalBlocker blocker(system_toggle_);
        system_toggle_->setChecked(system_audio);
    }
    if (audio_mode_combo_) {
        const int index = audio_mode_combo_->findData(static_cast<int>(mode));
        if (index >= 0) {
            const QSignalBlocker blocker(audio_mode_combo_);
            audio_mode_combo_->setCurrentIndex(index);
        }
    }

    UpdateAudioUi(true);
    UpdateAudioUi(false);
    UpdateAudioModeUi();
}

void MainWindow::UpdateAudioModeFromToggles() {
    if (!mic_toggle_ || !system_toggle_) {
        return;
    }
    const AudioSourceMode mode = AudioModeFromFlags(
        mic_toggle_->isChecked(), system_toggle_->isChecked());
    settings_.audio_mode = mode;
    if (audio_mode_combo_) {
        const int index = audio_mode_combo_->findData(static_cast<int>(mode));
        if (index >= 0 && index != audio_mode_combo_->currentIndex()) {
            const QSignalBlocker blocker(audio_mode_combo_);
            audio_mode_combo_->setCurrentIndex(index);
        }
    }
    UpdateAudioModeUi();
}

void MainWindow::UpdateAudioModeUi() {
    if (!mic_mode_dim_ || !system_mode_dim_) {
        return;
    }
    AudioSourceMode mode = settings_.audio_mode;
    if (audio_mode_combo_) {
        mode = static_cast<AudioSourceMode>(
            audio_mode_combo_->currentData().toInt());
    }
    const int value = static_cast<int>(mode);
    if (value < 0 || value > static_cast<int>(AudioSourceMode::None)) {
        mode = AudioSourceMode::Both;
    }
    mic_mode_dim_->setOpacity(AudioModeHasMicrophone(mode) ? 1.0 : 0.45);
    system_mode_dim_->setOpacity(AudioModeHasSystemAudio(mode) ? 1.0 : 0.45);
}

void MainWindow::InstallShortcuts() {
    auto install = [this](QShortcut*& slot, const QString& text,
                          Qt::ShortcutContext context, auto handler) {
        delete slot;
        slot = nullptr;
        if (text.trimmed().isEmpty()) {
            return;
        }
        const QKeySequence sequence(text, QKeySequence::PortableText);
        if (sequence.isEmpty()) {
            return;
        }
        slot = new QShortcut(sequence, this);
        slot->setContext(context);
        connect(slot, &QShortcut::activated, this, handler);
    };

    install(start_stop_shortcut_, settings_.shortcut_start_stop,
            Qt::WindowShortcut, [this] {
                if (recording_) {
                    StopRecording();
                } else {
                    StartRecording();
                }
            });
    // Application-wide so it also restores a window that is hidden in the
    // tray; hide() suspends the idle preview and showEvent resumes it.
    install(show_hide_shortcut_, settings_.shortcut_show_hide,
            Qt::ApplicationShortcut, [this] {
                if (isVisible() && !isMinimized()) {
                    hide();
                } else {
                    ShowMainWindow();
                }
            });
    install(open_folder_shortcut_, settings_.shortcut_open_folder,
            Qt::WindowShortcut, [this] { OpenOutputFolder(); });
}

void MainWindow::OpenOutputFolder() {
    QString directory = OutputDirectoryFromUi();
    if (directory.isEmpty()) {
        directory = settings_.output_directory.isEmpty()
                        ? DefaultOutputDirectory()
                        : settings_.output_directory;
    }
    if (directory.isEmpty() ||
        !QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
        ShowStatus(QStringLiteral("无法打开保存文件夹"), true, 4000);
    }
}

void MainWindow::OpenSettings() {
    if (recording_ || countdown_active_) {
        return;
    }
    // Pick up choices made directly on the main window (codec, fps, mode,
    // devices, folder) so the dialog edits and carries the current state.
    SyncSettingsFromUi();

    SettingsDialog dialog(settings_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    settings_ = dialog.settings();

    bitrate_ = settings_.video_bitrate;
    hdr_ = settings_.hdr;
    camera_index_ = settings_.camera_index;
    if (camera_device_count_ > 0 &&
        (camera_index_ < 0 || camera_index_ >= camera_device_count_)) {
        camera_index_ = 0;
    }
    settings_.camera_index = camera_index_;
    container_ = settings_.container_extension;
    audio_codec_ = settings_.audio_codec;
    audio_sample_rate_ = settings_.audio_sample_rate;
    audio_bitrate_ = settings_.audio_bitrate;
    countdown_seconds_ = settings_.countdown_seconds;

    // One source of truth for the destination folder: the bottom bar mirrors
    // the value accepted in 输出设置.
    if (settings_.output_directory.isEmpty()) {
        settings_.output_directory = DefaultOutputDirectory();
    }
    output_edit_->setText(
        QDir::toNativeSeparators(settings_.output_directory));
    output_edit_->setCursorPosition(0);

    SaveAppSettings(settings_);
    ApplyAutoStart(settings_.auto_start);
    // New sequences take effect immediately after 保存设置.
    InstallShortcuts();
}

void MainWindow::ShowStatus(const QString& text, bool error, int timeout_ms) {
    status_is_error_ = error;
    status_label_->setText(text);
    const ThemeColors& c = Theme::Instance().colors();
    status_label_->setStyleSheet(
        QStringLiteral("color:%1; font-size:13px;")
            .arg((error ? c.danger : c.textMuted).name()));
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
    preview_started_ = true;
    // Also covers restoring from the tray: the idle preview and meters were
    // paused while the window was hidden.
    if (!recording_) {
        SetIdleSuspended(false);
    }
}

void MainWindow::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    // Minimization is handled by changeEvent(); this path is the tray hide,
    // where the preview must not keep duplicating the desktop invisibly. An
    // active recording keeps its pipeline alive.
    if (!recording_ && !isMinimized()) {
        SetIdleSuspended(true);
    }
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
    // starts; only the desktop-backed modes own an idle preview. A window
    // hidden in the tray must not keep duplicating the desktop either.
    if (!has_display_ || recording_ || isMinimized() || !isVisible() ||
        selected_mode_ > 1) {
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

QString MainWindow::OutputDirectoryFromUi() const {
    return output_edit_
               ? QDir::fromNativeSeparators(output_edit_->text().trimmed())
               : QString();
}

QString MainWindow::GenerateOutputPath() const {
    const QString directory = OutputDirectoryFromUi();
    if (directory.isEmpty()) {
        return QString();
    }
    const QString base = QStringLiteral("Hmmob-%1").arg(
        QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd-HHmmss")));
    QString path = QDir(directory).filePath(
        QStringLiteral("%1.%2").arg(base, container_));
    // Never overwrite: two starts within one second get -2, -3, ...
    for (int suffix = 2; QFileInfo::exists(path); ++suffix) {
        path = QDir(directory).filePath(
            QStringLiteral("%1-%2.%3").arg(base).arg(suffix).arg(container_));
    }
    return path;
}

void MainWindow::BrowseOutput() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择保存目录"), OutputDirectoryFromUi());
    if (!directory.isEmpty()) {
        output_edit_->setText(QDir::toNativeSeparators(directory));
        output_edit_->setCursorPosition(0);
        settings_.output_directory = QDir::fromNativeSeparators(directory);
        PersistSettings();
    }
}

QString MainWindow::RecordingCompatibilityError() const {
    const QString extension = container_.toLower();
    const bool known = extension == QStringLiteral("mp4") ||
                       extension == QStringLiteral("mov") ||
                       extension == QStringLiteral("mkv") ||
                       extension == QStringLiteral("avi") ||
                       extension == QStringLiteral("flv") ||
                       extension == QStringLiteral("wmv");
    if (!known) {
        return QStringLiteral(
            "不支持的保存格式，请使用 mp4/mov/mkv/avi/flv/wmv 扩展名");
    }

    const QString codec = codec_combo_->currentData().toString();
    if (codec == QStringLiteral("hevc") &&
        (extension == QStringLiteral("avi") ||
         extension == QStringLiteral("flv") ||
         extension == QStringLiteral("wmv"))) {
        return QStringLiteral(
                   "HEVC 不支持 %1 保存格式，请改用 H.264 或更换保存格式")
            .arg(extension.toUpper());
    }
    if (extension == QStringLiteral("flv") &&
        audio_codec_ == hmmob::AudioCodec::PcmS16Le) {
        return QStringLiteral(
            "FLV 保存格式不支持 PCM 音频，请改用 AAC 或更换保存格式");
    }
    return QString();
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
    AudioSourceMode audio_mode = settings_.audio_mode;
    if (audio_mode_combo_) {
        audio_mode = static_cast<AudioSourceMode>(
            audio_mode_combo_->currentData().toInt());
    }
    const int audio_mode_value = static_cast<int>(audio_mode);
    if (audio_mode_value < 0 ||
        audio_mode_value > static_cast<int>(AudioSourceMode::None)) {
        audio_mode = AudioSourceMode::Both;
    }
    // Neither source selected also disables the audio track entirely.
    config.system_audio = AudioModeHasSystemAudio(audio_mode);
    config.microphone = AudioModeHasMicrophone(audio_mode);
    config.system_device_id = system_device_combo_->currentData()
                                  .toString()
                                  .toStdString();
    config.mic_device_id =
        mic_device_combo_->currentData().toString().toStdString();
    config.audio_codec = audio_codec_;
    config.audio_sample_rate = audio_sample_rate_;
    config.audio_bitrate = audio_bitrate_;
    // Source levels are intentionally fixed at unity in this build; the
    // meters are decorative and never feed back into the recorder.
    config.system_gain = 1.0f;
    config.mic_gain = 1.0f;
    // Filled in by StartRecordingNow() once the countdown (if any) is over.
    config.output_path.clear();
    config.duration_seconds = 0;
    config.preview_width = 640;
    config.preview_height = 360;
    config.preview_fps = 30;
    return config;
}

bool MainWindow::StartRecording() {
    if (recording_ || countdown_active_) {
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

    // The destination is a folder. The file name is generated at the moment
    // the recorder actually starts, so cancelling a countdown never creates
    // one. Validate that the folder exists or can be created.
    const QString directory = smoke_
                                  ? QFileInfo(smoke_path_).absolutePath()
                                  : OutputDirectoryFromUi();
    if (directory.isEmpty()) {
        SetError(QStringLiteral("请选择保存目录"));
        return false;
    }
    QDir folder(directory);
    if (!folder.exists() && !QDir().mkpath(directory)) {
        SetError(QStringLiteral("无法创建保存目录：%1")
                     .arg(QDir::toNativeSeparators(directory)));
        return false;
    }

    const QString compatibility = RecordingCompatibilityError();
    if (!compatibility.isEmpty()) {
        SetError(compatibility);
        return false;
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

    pending_config_ = config;

    // Smoke runs start immediately; a user-facing start honours 倒计时.
    if (!smoke_ && countdown_seconds_ > 0) {
        BeginCountdown();
        return true;
    }
    return StartRecordingNow();
}

bool MainWindow::StartRecordingNow() {
    if (recording_) {
        return false;
    }
    countdown_active_ = false;
    countdown_escape_shortcut_->setEnabled(false);

    QString path;
    if (smoke_) {
        path = smoke_path_;
    } else {
        path = GenerateOutputPath();
    }
    if (path.isEmpty()) {
        SetError(QStringLiteral("请选择保存目录"));
        SetConfigEnabled(true);
        start_button_->SetBusy(false);
        start_button_->setEnabled(CanStart());
        StartIdlePreview();
        return false;
    }
    output_path_ = path;

    hmmob::RecorderConfig config = pending_config_;
    config.output_path = QDir::toNativeSeparators(path).toStdString();

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
        start_button_->SetBusy(false);
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
        if (selected_mode_ == 1) {
            capture_width_ = pending_config_.region_width;
            capture_height_ = pending_config_.region_height;
        }
    }
    UpdateBadgeText();
    ShowStatus(QStringLiteral("录制中…"), false, 0);
    poll_timer_->start();
    return true;
}

void MainWindow::BeginCountdown() {
    countdown_active_ = true;
    countdown_escape_shortcut_->setEnabled(true);
    SetConfigEnabled(false);
    start_button_->SetBusy(false);
    start_button_->setEnabled(false);
    // Region selection released the idle preview; keep the desktop visible
    // underneath the overlay while the countdown runs.
    if (selected_mode_ <= 1) {
        StartIdlePreview();
    }
    UpdateBadgeText();
    ShowStatus(QStringLiteral("%1 秒后开始录制…").arg(countdown_seconds_),
               false, 0);
    countdown_overlay_->Start(countdown_seconds_);
}

void MainWindow::CancelCountdown() {
    if (!countdown_active_) {
        return;
    }
    countdown_active_ = false;
    countdown_escape_shortcut_->setEnabled(false);
    countdown_overlay_->Stop();
    SetConfigEnabled(true);
    start_button_->SetBusy(false);
    start_button_->setEnabled(CanStart());
    ShowStatus(QString(), false, 0);
    if (selected_mode_ <= 1) {
        StartIdlePreview();
    }
    UpdateBadgeText();
}

void MainWindow::OnCountdownFinished() {
    if (!countdown_active_) {
        return;
    }
    countdown_active_ = false;
    countdown_escape_shortcut_->setEnabled(false);
    countdown_overlay_->Stop();
    StartRecordingNow();
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
        if (settings_.show_recording_complete_notification) {
            ShowTrayMessage(
                QStringLiteral("Hmmob 录屏"),
                QStringLiteral("录制完成: %1")
                    .arg(QDir::toNativeSeparators(output_path_)));
        }
    }

    if (std::shared_ptr<const hmmob::PreviewFrame> frame =
            recorder_.LatestPreview()) {
        last_preview_ = frame;
        preview_->SetFrame(std::move(frame));
    }

    // Resume the idle preview for the desktop-backed modes only; window and
    // camera keep the recorder's last frame. If the window is minimized or
    // hidden in the tray the preview is restarted on restore instead, and the
    // poll timer must not keep ticking invisibly.
    if (selected_mode_ == 0 || selected_mode_ == 1) {
        StartIdlePreview();
    }
    UpdateBadgeText();
    if (isMinimized() || !isVisible()) {
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
    if (audio_mode_combo_) {
        audio_mode_combo_->setEnabled(enabled);
    }
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

void MainWindow::CaptureSettingsScreenshots(const QString& directory) {
    SyncSettingsFromUi();
    QDir().mkpath(directory);
    auto* dialog = new SettingsDialog(settings_, this);
    // Verification hook: render at a specific size, e.g. the minimum
    // 900x560, so DPI/resize regressions can be inspected in CI.
    const QString requested_size =
        QString::fromLocal8Bit(qgetenv("HMMOB_SETTINGS_SCREENSHOT_SIZE"));
    if (!requested_size.isEmpty()) {
        const QStringList parts = requested_size.split(QLatin1Char('x'));
        if (parts.size() == 2) {
            bool width_ok = false;
            bool height_ok = false;
            const int width = parts.at(0).toInt(&width_ok);
            const int height = parts.at(1).toInt(&height_ok);
            if (width_ok && height_ok && width > 0 && height > 0) {
                dialog->resize(width, height);
            }
        }
    }
    dialog->setModal(false);
    dialog->show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    const QStringList names = {
        QStringLiteral("general"), QStringLiteral("recording"),
        QStringLiteral("shortcuts"), QStringLiteral("output"),
        QStringLiteral("about")};
    for (int i = 0; i < names.size(); ++i) {
        dialog->SelectPage(i);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        dialog->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        const QString path = QDir(directory).filePath(
            QStringLiteral("settings-%1-%2.png").arg(i + 1).arg(names.at(i)));
        QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Theme::Instance().colors().window);
        dialog->render(&image);
        if (!image.save(path)) {
            // This Qt build ships no PNG image plugin; BMP is built in, so
            // the hook still produces inspectable output.
            const QString bmp_path = path.left(path.size() - 4) +
                                     QStringLiteral(".bmp");
            if (!image.save(bmp_path)) {
                std::fprintf(stderr, "failed to save settings screenshot: %s\n",
                             bmp_path.toLocal8Bit().constData());
            }
        }
    }

    dialog->close();
    delete dialog;
    QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(0); });
}

void MainWindow::RunUiHarness(const QString& directory) {
    const AppSettings original_settings = settings_;
    const ThemeMode original_theme = Theme::Instance().mode();

    QDir().mkpath(directory);
    const QString recordings =
        QDir(directory).filePath(QStringLiteral("recordings"));
    QDir().mkpath(recordings);

    QStringList report;
    bool passed = true;
    const auto check = [&](const QString& name, bool value,
                           const QString& detail = QString()) {
        if (!value) {
            passed = false;
        }
        report << QStringLiteral("%1=%2%3")
                      .arg(name, value ? QStringLiteral("pass")
                                       : QStringLiteral("fail"),
                           detail.isEmpty()
                               ? QString()
                               : QStringLiteral(" (%1)").arg(detail));
    };
    const auto pump_ms = [](int milliseconds) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(5);
        }
    };
    const auto pump_until = [](const std::function<bool()>& done,
                               int timeout_ms) {
        QElapsedTimer timer;
        timer.start();
        while (!done() && timer.elapsed() < timeout_ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(5);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return done();
    };
    const auto save_shot = [](QWidget* widget, const QString& base) {
        QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Theme::Instance().colors().window);
        widget->render(&image);
        if (image.save(base + QStringLiteral(".png"))) {
            return base + QStringLiteral(".png");
        }
        if (image.save(base + QStringLiteral(".bmp"))) {
            return base + QStringLiteral(".bmp");
        }
        return QString();
    };
    const auto native_clean = [](const QString& path) {
        return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    };

    // -- Configure the run: screen mode, MP4, 3 s countdown, harness folder.
    container_ = QStringLiteral("mp4");
    countdown_seconds_ = 3;
    SetMode(0);
    output_edit_->setText(QDir::toNativeSeparators(recordings));
    output_edit_->setCursorPosition(0);
    settings_.output_directory = recordings;
    Theme::Instance().SetMode(ThemeMode::Light);
    pump_ms(500);

    // -- Folder-only bottom bar.
    const QString field_text = output_edit_->text();
    check(QStringLiteral("folder_only"),
          native_clean(field_text) == QDir::cleanPath(recordings) &&
              QFileInfo(field_text).isDir(),
          field_text);
    const QString light_main =
        save_shot(this, QDir(directory).filePath(QStringLiteral("main-light")));
    check(QStringLiteral("screenshot_main_light"), !light_main.isEmpty(),
          light_main);

    // -- Settings page: directory sync, countdown combo and wheel handling.
    {
        SettingsDialog dialog(settings_, this);
        dialog.SelectPage(1);
        dialog.setModal(false);
        dialog.show();
        pump_ms(300);

        const QString light_settings = save_shot(
            &dialog,
            QDir(directory).filePath(
                QStringLiteral("settings-recording-light")));
        check(QStringLiteral("screenshot_settings_light"),
              !light_settings.isEmpty(), light_settings);

        auto* format = dialog.findChild<QComboBox*>(
            QStringLiteral("formatCombo"));
        auto* countdown = dialog.findChild<QComboBox*>(
            QStringLiteral("countdownCombo"));
        auto* directory_edit = dialog.findChild<QLineEdit*>(
            QStringLiteral("directoryEdit"));
        check(QStringLiteral("countdown_combo_present"),
              countdown != nullptr && countdown->count() == 4 &&
                  countdown->currentData().toInt() == 3);
        check(QStringLiteral("settings_directory_sync"),
              directory_edit != nullptr &&
                  native_clean(directory_edit->text()) ==
                      QDir::cleanPath(recordings),
              directory_edit ? directory_edit->text() : QString());

        const auto post_wheel = [](QComboBox* combo) {
            const QPoint center(combo->width() / 2, combo->height() / 2);
            QWheelEvent wheel(QPointF(center),
                              QPointF(combo->mapToGlobal(center)), QPoint(),
                              QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QApplication::sendEvent(combo, &wheel);
        };
        int index_before = format->currentIndex();
        post_wheel(format);
        check(QStringLiteral("wheel_format_closed_unchanged"),
              format->currentIndex() == index_before,
              QString::number(format->currentIndex()));
        index_before = countdown->currentIndex();
        post_wheel(countdown);
        check(QStringLiteral("wheel_countdown_closed_unchanged"),
              countdown->currentIndex() == index_before,
              QString::number(countdown->currentIndex()));

        // The dropdown still opens and the keys still select. Windows can
        // refuse to show a popup while the dialog is not foreground yet, so
        // activate it and give the popup a few frames.
        dialog.raise();
        dialog.activateWindow();
        pump_ms(250);
        format->showPopup();
        bool popup_open = false;
        for (int attempt = 0; attempt < 8 && !popup_open; ++attempt) {
            pump_ms(100);
            popup_open =
                format->view() != nullptr && format->view()->isVisible();
        }
        const int before_key = format->currentIndex();
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(format, &down);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(format, &enter);
        pump_ms(150);
        format->hidePopup();
        check(QStringLiteral("dropdown_opens"), popup_open);
        check(QStringLiteral("keys_change_value"),
              format->currentIndex() != before_key,
              QStringLiteral("%1 -> %2")
                  .arg(before_key)
                  .arg(format->currentIndex()));

        const int before_programmatic = format->currentIndex();
        format->setCurrentIndex(
            (before_programmatic + 1) % format->count());
        check(QStringLiteral("programmatic_change"),
              format->currentIndex() != before_programmatic);

        Theme::Instance().SetMode(ThemeMode::Dark);
        pump_ms(350);
        const QString dark_settings = save_shot(
            &dialog,
            QDir(directory).filePath(
                QStringLiteral("settings-recording-dark")));
        check(QStringLiteral("screenshot_settings_dark"),
              !dark_settings.isEmpty(), dark_settings);
        dialog.close();
        pump_ms(150);
    }

    const QString dark_main =
        save_shot(this, QDir(directory).filePath(QStringLiteral("main-dark")));
    check(QStringLiteral("screenshot_main_dark"), !dark_main.isEmpty(),
          dark_main);

    // -- Countdown: screenshots at 3/1 and Esc cancellation, per theme.
    const auto countdown_cancel = [&](const QString& theme_name) {
        const int files_before =
            QDir(recordings).entryList(QDir::Files).size();
        bool shot_three = false;
        bool shot_one = false;
        bool reached_one = false;
        const QMetaObject::Connection connection = connect(
            countdown_overlay_, &CountdownOverlay::tick, this,
            [&](int remaining) {
                if (remaining == 3 && !shot_three) {
                    shot_three = !save_shot(
                                      this, QDir(directory).filePath(
                                                QStringLiteral(
                                                    "countdown-%1-3s")
                                                    .arg(theme_name)))
                                      .isEmpty();
                }
                if (remaining == 1) {
                    reached_one = true;
                }
            });
        const bool began = StartRecording();
        const bool reached =
            pump_until([&] { return reached_one; }, 5000);
        if (reached) {
            shot_one = !save_shot(
                              this, QDir(directory).filePath(
                                        QStringLiteral("countdown-%1-1s")
                                            .arg(theme_name)))
                              .isEmpty();
        }
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(countdown_overlay_, &escape);
        pump_ms(350);
        disconnect(connection);

        check(QStringLiteral("countdown_%1_began").arg(theme_name), began);
        check(QStringLiteral("countdown_%1_reached_1s").arg(theme_name),
              reached);
        check(QStringLiteral("countdown_%1_esc_cancels").arg(theme_name),
              !recording_ && !countdown_active_ &&
                  !countdown_overlay_->IsActive());
        check(QStringLiteral("countdown_%1_restores_start").arg(theme_name),
              start_button_->isEnabled() && !status_is_error_);
        check(QStringLiteral("countdown_%1_no_file").arg(theme_name),
              QDir(recordings).entryList(QDir::Files).size() == files_before);
        check(QStringLiteral("screenshot_countdown_%1_3s").arg(theme_name),
              shot_three);
        check(QStringLiteral("screenshot_countdown_%1_1s").arg(theme_name),
              shot_one);
    };
    Theme::Instance().SetMode(ThemeMode::Light);
    pump_ms(350);
    countdown_cancel(QStringLiteral("light"));

    Theme::Instance().SetMode(ThemeMode::Dark);
    pump_ms(350);
    countdown_cancel(QStringLiteral("dark"));
    Theme::Instance().SetMode(ThemeMode::Light);
    pump_ms(350);

    // -- The overlay's 取消 button takes the same cancellation path.
    {
        const int files_before =
            QDir(recordings).entryList(QDir::Files).size();
        const bool began = StartRecording();
        pump_ms(200);
        QPushButton* cancel = countdown_overlay_->findChild<QPushButton*>();
        const bool clicked = cancel != nullptr && cancel->isVisible();
        if (clicked) {
            cancel->click();
        }
        pump_ms(350);
        check(QStringLiteral("countdown_cancel_button"),
              began && clicked && !recording_ && !countdown_active_ &&
                  QDir(recordings).entryList(QDir::Files).size() ==
                      files_before);
    }

    // -- A real countdown recording lands in the folder with a timestamp name.
    {
        // Rotation: with the generated name occupied, the next attempt must
        // pick a different file instead of overwriting it.
        const QString probe = GenerateOutputPath();
        QFile probe_file(probe);
        const bool probe_created = probe_file.open(QIODevice::WriteOnly);
        if (probe_created) {
            probe_file.close();
        }
        const QString rotated = GenerateOutputPath();
        check(QStringLiteral("rotation_never_overwrites"),
              probe_created && QFileInfo::exists(probe) && rotated != probe &&
                  !QFileInfo::exists(rotated),
              QFileInfo(rotated).fileName());
        QFile::remove(rotated);
        QFile::remove(probe);

        const QStringList before = QDir(recordings).entryList(QDir::Files);
        QElapsedTimer timer;
        timer.start();
        const bool began = StartRecording();
        const bool started =
            pump_until([&] { return recording_; }, 8000);
        const qint64 start_ms = timer.elapsed();
        check(QStringLiteral("record_started"), began && started,
              QStringLiteral("%1 ms").arg(start_ms));
        check(QStringLiteral("countdown_about_3s"),
              started && start_ms >= 2800 && start_ms <= 5000,
              QStringLiteral("%1 ms").arg(start_ms));

        pump_ms(2200);
        StopRecording();
        const bool finished =
            pump_until([&] { return !recording_; }, 15000);
        check(QStringLiteral("record_finished"), finished);

        QString recorded;
        const QStringList after = QDir(recordings).entryList(QDir::Files);
        const QRegularExpression name_pattern(
            QStringLiteral("^Hmmob-\\d{8}-\\d{6}(-\\d+)?\\.mp4$"));
        for (const QString& name : after) {
            if (!before.contains(name) &&
                name_pattern.match(name).hasMatch()) {
                recorded = QDir(recordings).filePath(name);
                break;
            }
        }
        const QFileInfo info(recorded);
        check(QStringLiteral("recording_in_folder"),
              !recorded.isEmpty() && info.absolutePath() ==
                                          QDir(recordings).absolutePath(),
              QDir::toNativeSeparators(recorded));
        check(QStringLiteral("recording_nonempty"),
              info.exists() && info.size() > 0,
              QStringLiteral("%1 bytes").arg(info.size()));
        report << QStringLiteral("recording_path=%1").arg(recorded);
        report << QStringLiteral("countdown_start_ms=%1").arg(start_ms);
    }

    // -- Both folder entry points persist through QSettings (the same code
    // path the next launch's LoadAppSettings() uses).
    {
        output_edit_->setText(QDir::toNativeSeparators(recordings));
        SyncSettingsFromUi();
        check(QStringLiteral("folder_from_bottom_bar"),
              QDir::cleanPath(settings_.output_directory) ==
                  QDir::cleanPath(recordings));
        SaveAppSettings(settings_);
        const AppSettings reloaded = LoadAppSettings();
        check(QStringLiteral("persists_bottom_bar"),
              native_clean(reloaded.output_directory) ==
                  QDir::cleanPath(recordings),
              reloaded.output_directory);

        SettingsDialog dialog(settings_, this);
        auto* directory_edit = dialog.findChild<QLineEdit*>(
            QStringLiteral("directoryEdit"));
        const QString alternate =
            QDir(directory).filePath(QStringLiteral("alternate"));
        QDir().mkpath(alternate);
        directory_edit->setText(QDir::toNativeSeparators(alternate));
        const AppSettings collected = dialog.settings();
        check(QStringLiteral("folder_from_settings_page"),
              native_clean(collected.output_directory) ==
                  QDir::cleanPath(alternate));
        SaveAppSettings(collected);
        const AppSettings reloaded_settings = LoadAppSettings();
        check(QStringLiteral("persists_settings_page"),
              native_clean(reloaded_settings.output_directory) ==
                  QDir::cleanPath(alternate),
              reloaded_settings.output_directory);
    }

    // Leave the user's preferences exactly as they were.
    settings_ = original_settings;
    SaveAppSettings(original_settings);
    Theme::Instance().SetMode(original_theme);

    QFile file(QDir(directory).filePath(QStringLiteral("ui-harness.txt")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text |
                  QIODevice::Truncate)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "result=" << (passed ? "pass" : "fail") << '\n';
        for (const QString& line : report) {
            out << line << '\n';
        }
        out.flush();
    }

    QTimer::singleShot(0, qApp, [passed] {
        QCoreApplication::exit(passed ? 0 : 1);
    });
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
    // Smoke keeps its explicit file path; the bottom bar still shows a folder.
    output_edit_->setText(QDir::toNativeSeparators(
        QFileInfo(path).absolutePath()));

    if (!displays_.empty()) {
        SelectDisplay(displays_.front().index);
    }
    fps_combo_->setCurrentIndex(fps_combo_->findData(60));
    codec_combo_->setCurrentIndex(
        codec_combo_->findData(QStringLiteral("hevc")));
    // Smoke runs are unattended and start before the settings dialog can be
    // reached, so pin the documented defaults instead of inheriting anything.
    bitrate_ = 50000000;
    hdr_ = false;
    container_ = QStringLiteral("mp4");
    audio_codec_ = hmmob::AudioCodec::Aac;
    audio_sample_rate_ = 48000;
    audio_bitrate_ = 192000;
    SetAudioMode(AudioSourceMode::Both);
    {
        const QSignalBlocker mic_blocker(mic_device_combo_);
        const QSignalBlocker system_blocker(system_device_combo_);
        mic_device_combo_->setCurrentIndex(0);
        system_device_combo_->setCurrentIndex(0);
    }

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
    const BOOL dark =
        Theme::Instance().effectiveMode() == ThemeMode::Dark ? TRUE : FALSE;
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
    // 关闭窗口时最小化到托盘: hide instead of quitting. The tray's 退出
    // action sets quitting_ and falls through to the normal shutdown path.
    if (settings_.close_to_tray && !quitting_ && tray_icon_) {
        hide();
        ShowTrayMessage(QStringLiteral("Hmmob 录屏"),
                        QStringLiteral("已最小化到托盘"));
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
