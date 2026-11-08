#include "app/SettingsDialog.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <vector>

#include "app/Controls.h"
#include "app/IconRenderer.h"
#include "app/UiFont.h"
#include "core/capture/CameraSource.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace {

constexpr int kDialogWidth = 980;
constexpr int kDialogHeight = 640;
constexpr int kNavWidth = 200;
constexpr int kLabelWidth = 88;

const QColor kPrimary(0x2F, 0x6B, 0xFF);
const QColor kText(0x1D, 0x21, 0x29);
const QColor kSecondary(0x4E, 0x59, 0x69);
const QColor kMuted(0x86, 0x90, 0x9C);

const char* const kSettingsStyle = R"(
QDialog { background: #FFFFFF; }
QWidget#settingsHeader { background: #FFFFFF; }
QWidget#settingsNavPane { background: #FFFFFF; }
QWidget#settingsContent { background: #FFFFFF; }
QWidget#settingsFooter { background: #FFFFFF; }
QLabel#pageTitle { font-size: 20px; font-weight: 600; color: #1D2129; }
QLabel#sectionTitle { font-size: 13px; font-weight: 600; color: #86909C; }
QLabel#fieldLabel { font-size: 14px; color: #1D2129; }
QLabel#fieldHint { font-size: 12px; color: #86909C; }
QLabel#fieldError { font-size: 12px; color: #E5484D; }
QLabel#themeHint { font-size: 12px; color: #86909C; }
QLabel#noteLabel { font-size: 12px; color: #86909C; }
QLabel#deferredLabel { font-size: 13px; color: #A9AEB8; }
QLabel#keyChip { background: #F7F8FA; border: 1px solid #EDF0F5; border-radius: 6px; padding: 3px 10px; font-size: 13px; color: #4E5969; }
QLabel#aboutText { font-size: 13px; color: #4E5969; }
QLabel#aboutVersion { font-size: 13px; color: #86909C; }
QFrame#rowSeparator { background: #F2F3F5; border: none; }
QComboBox { background: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 10px; padding: 0 10px 0 14px; font-size: 14px; color: #1D2129; }
QComboBox:hover { border-color: #C9CDD4; }
QComboBox:disabled { background: #F7F8FA; color: #A9AEB8; border-color: #EDF0F5; }
QComboBox::drop-down { border: none; background: transparent; width: 34px; }
QComboBox::down-arrow { image: none; width: 0px; height: 0px; }
QComboBox QAbstractItemView { background: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 8px; padding: 4px; outline: 0; selection-background-color: #EAF2FE; selection-color: #2F6BFF; }
QComboBox QAbstractItemView::item { min-height: 30px; padding: 0 10px; }
QLineEdit { background: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 10px; padding: 0 12px; font-size: 14px; color: #1D2129; selection-background-color: #2F6BFF; selection-color: #FFFFFF; }
QLineEdit:hover { border-color: #C9CDD4; }
QPushButton { min-height: 38px; padding: 0 20px; border-radius: 10px; font-size: 14px; }
QPushButton#outline { background: #FFFFFF; border: 1px solid #E5E6EB; color: #4E5969; }
QPushButton#outline:hover { background: #F7F8FA; }
QPushButton#saveButton { background: #2F6BFF; border: 1px solid #2F6BFF; color: #FFFFFF; }
QPushButton#saveButton:hover { background: #245BF0; }
QPushButton#saveButton:pressed { background: #1E52E8; }
QPushButton#saveButton:disabled { background: #B7C7F5; border-color: #B7C7F5; color: #FFFFFF; }
QPushButton#segment { background: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 10px; color: #4E5969; font-size: 14px; padding: 0 12px; }
QPushButton#segment:hover { border-color: #C9CDD4; }
QPushButton#segment:checked { background: #EEF5FF; border: 1px solid #2F6BFF; color: #2F6BFF; }
)";

// Left navigation row: 44 px tall, rounded highlight when active.
class SettingsNavButton : public QAbstractButton {
public:
    SettingsNavButton(Icon icon, const QString& text, QWidget* parent)
        : QAbstractButton(parent), icon_(icon), text_(text) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setFixedHeight(44);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override {
        return QSize(kNavWidth - 24, 44);
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        hover_ = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hover_ = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        (void)event;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds = rect();
        if (isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0xEE, 0xF4, 0xFF));
            painter.drawRoundedRect(bounds, 10.0, 10.0);
        } else if (hover_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0xF5, 0xF7, 0xFA));
            painter.drawRoundedRect(bounds, 10.0, 10.0);
        }

        const QColor color = isChecked() ? kPrimary : kSecondary;
        IconRenderer::Paint(painter, icon_,
                            QRectF(16.0, (height() - 20.0) / 2.0, 20, 20),
                            color, 1.7);

        QFont label = UiFont(15);
        label.setWeight(isChecked() ? QFont::DemiBold : QFont::Normal);
        painter.setFont(label);
        painter.setPen(color);
        painter.drawText(QRectF(48, 0, width() - 56, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, text_);
    }

private:
    Icon icon_;
    QString text_;
    bool hover_ = false;
};

// Caption bar of the frameless dialog. Dragging it moves the window through
// the native move loop, which keeps snapping/DPI behaviour consistent.
class SettingsHeader : public QWidget {
public:
    explicit SettingsHeader(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("settingsHeader"));
        setAttribute(Qt::WA_StyledBackground, true);
        setFixedHeight(56);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (QWindow* handle = window()->windowHandle()) {
                handle->startSystemMove();
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }
};

class SettingsCloseButton : public QAbstractButton {
public:
    explicit SettingsCloseButton(QWidget* parent) : QAbstractButton(parent) {
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(46, 56);
        setAttribute(Qt::WA_Hover, true);
        setToolTip(QStringLiteral("关闭"));
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        hover_ = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hover_ = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        (void)event;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (hover_) {
            painter.fillRect(rect(), QColor(0xE8, 0x11, 0x23));
        }
        IconRenderer::Paint(
            painter, Icon::Close,
            QRectF((width() - 16.0) / 2.0, (height() - 16.0) / 2.0, 16, 16),
            hover_ ? QColor(Qt::white) : QColor(0x3A, 0x42, 0x54), 1.5);
    }

private:
    bool hover_ = false;
};

QLabel* MakePageTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("pageTitle"));
    return label;
}

QLabel* MakeSectionTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

QLabel* MakeFieldLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("fieldLabel"));
    return label;
}

QLabel* MakeHint(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("fieldHint"));
    return label;
}

QLabel* MakeNote(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("noteLabel"));
    return label;
}

QWidget* MakeLabeledRow(const QString& label_text, QWidget* control,
                        QWidget* parent, int label_width = kLabelWidth) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto* label = MakeFieldLabel(label_text, row);
    label->setFixedWidth(label_width);
    layout->addWidget(label, 0, Qt::AlignVCenter);
    layout->addWidget(control, 0, Qt::AlignVCenter);
    layout->addStretch(1);
    return row;
}

QFrame* MakeSeparator(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setObjectName(QStringLiteral("rowSeparator"));
    line->setFixedHeight(1);
    return line;
}

QComboBox* MakeCombo(QWidget* parent, int width) {
    auto* combo = new StyledComboBox(parent);
    combo->setFixedHeight(38);
    if (width > 0) {
        combo->setFixedWidth(width);
    }
    return combo;
}

QPushButton* MakeSegment(Icon icon, const QString& text, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("segment"));
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(118, 38);
    QIcon qicon;
    qicon.addPixmap(IconRenderer::Pixmap(icon, 18, kSecondary), QIcon::Normal,
                    QIcon::Off);
    qicon.addPixmap(IconRenderer::Pixmap(icon, 18, kPrimary), QIcon::Normal,
                    QIcon::On);
    button->setIcon(qicon);
    button->setIconSize(QSize(18, 18));
    return button;
}

}  // namespace

SettingsDialog::SettingsDialog(const AppSettings& settings, QWidget* parent)
    : QDialog(parent),
      base_(settings),
      hevc_(settings.video_codec == QStringLiteral("hevc")) {
    setWindowTitle(QStringLiteral("设置"));
    setModal(true);
    setFont(UiFont(14));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString::fromUtf8(kSettingsStyle));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setFixedSize(kDialogWidth, kDialogHeight);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(BuildHeader());

    auto* body = new QWidget(this);
    auto* body_layout = new QHBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(0);
    body_layout->addWidget(BuildNav());

    auto* content = new QWidget(body);
    content->setObjectName(QStringLiteral("settingsContent"));
    content->setAttribute(Qt::WA_StyledBackground, true);
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(32, 18, 32, 0);
    content_layout->setSpacing(0);

    stack_ = new QStackedWidget(content);
    stack_->addWidget(BuildGeneralPage());
    stack_->addWidget(BuildRecordingPage());
    stack_->addWidget(BuildShortcutsPage());
    stack_->addWidget(BuildOutputPage());
    stack_->addWidget(BuildAboutPage());
    content_layout->addWidget(stack_, 1);
    content_layout->addWidget(BuildFooter(), 0);

    body_layout->addWidget(content, 1);
    root->addWidget(body, 1);

    for (int i = 0; i < nav_buttons_.size(); ++i) {
        connect(nav_buttons_.at(i), &QAbstractButton::clicked, this,
                [this, i] { SelectPage(i); });
    }
    nav_buttons_.front()->setChecked(true);
    stack_->setCurrentIndex(0);

    connect(container_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { UpdateCompatibility(); });
    connect(audio_codec_combo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { UpdateCompatibility(); });
    UpdateCompatibility();
}

QWidget* SettingsDialog::BuildHeader() {
    auto* header = new SettingsHeader(this);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(28, 0, 0, 0);
    layout->setSpacing(0);

    auto* mark = new QLabel(header);
    mark->setPixmap(IconRenderer::Pixmap(Icon::Play, 26, kPrimary));
    mark->setFixedSize(26, 26);
    layout->addWidget(mark, 0, Qt::AlignVCenter);
    layout->addSpacing(12);

    auto* title = new QLabel(QStringLiteral("Hmmob 录屏"), header);
    title->setFont(UiFont(16, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color:#1D2129;"));
    layout->addWidget(title, 0, Qt::AlignVCenter);
    layout->addStretch(1);

    auto* close = new SettingsCloseButton(header);
    layout->addWidget(close, 0, Qt::AlignTop);
    connect(close, &QAbstractButton::clicked, this, &QDialog::reject);
    return header;
}

QWidget* SettingsDialog::BuildNav() {
    auto* nav = new QWidget(this);
    nav->setObjectName(QStringLiteral("settingsNavPane"));
    nav->setAttribute(Qt::WA_StyledBackground, true);
    nav->setFixedWidth(kNavWidth);

    auto* layout = new QVBoxLayout(nav);
    layout->setContentsMargins(12, 18, 12, 0);
    layout->setSpacing(4);

    struct Item {
        Icon icon;
        QString text;
    };
    const Item items[] = {
        {Icon::Gear, QStringLiteral("通用设置")},
        {Icon::Camera, QStringLiteral("录制设置")},
        {Icon::Keyboard, QStringLiteral("快捷键")},
        {Icon::Folder, QStringLiteral("输出设置")},
        {Icon::Info, QStringLiteral("关于我们")},
    };
    for (const Item& item : items) {
        auto* button = new SettingsNavButton(item.icon, item.text, nav);
        nav_buttons_.push_back(button);
        layout->addWidget(button);
    }
    layout->addStretch(1);
    return nav;
}

QWidget* SettingsDialog::BuildGeneralPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(MakePageTitle(QStringLiteral("通用设置"), page));
    layout->addSpacing(18);

    layout->addWidget(MakeSectionTitle(QStringLiteral("启动设置"), page));
    layout->addSpacing(8);
    auto_start_check_ =
        new ToggleCheckBox(QStringLiteral("开机自动启动"), page);
    auto_start_check_->setObjectName(QStringLiteral("autoStartCheck"));
    auto_start_check_->setChecked(base_.auto_start);
    layout->addWidget(auto_start_check_);
    layout->addSpacing(10);
    start_hidden_check_ =
        new ToggleCheckBox(QStringLiteral("启动后最小化到托盘"), page);
    start_hidden_check_->setObjectName(QStringLiteral("startHiddenCheck"));
    start_hidden_check_->setChecked(base_.minimize_to_tray_on_start);
    layout->addWidget(start_hidden_check_);
    layout->addSpacing(10);
    close_to_tray_check_ =
        new ToggleCheckBox(QStringLiteral("关闭窗口时最小化到托盘"), page);
    close_to_tray_check_->setObjectName(QStringLiteral("closeToTrayCheck"));
    close_to_tray_check_->setChecked(base_.close_to_tray);
    layout->addWidget(close_to_tray_check_);
    layout->addSpacing(20);

    layout->addWidget(MakeSectionTitle(QStringLiteral("界面设置"), page));
    layout->addSpacing(8);

    auto* theme_control = new QWidget(page);
    auto* theme_layout = new QHBoxLayout(theme_control);
    theme_layout->setContentsMargins(0, 0, 0, 0);
    theme_layout->setSpacing(8);
    theme_group_ = new QButtonGroup(this);
    theme_group_->setExclusive(true);
    light_button_ = MakeSegment(Icon::Sun, QStringLiteral("浅色"), theme_control);
    dark_button_ = MakeSegment(Icon::Moon, QStringLiteral("深色"), theme_control);
    system_button_ =
        MakeSegment(Icon::Monitor, QStringLiteral("跟随系统"), theme_control);
    light_button_->setChecked(true);
    theme_group_->addButton(light_button_, 0);
    theme_group_->addButton(dark_button_, 1);
    theme_group_->addButton(system_button_, 2);
    theme_layout->addWidget(light_button_);
    theme_layout->addWidget(dark_button_);
    theme_layout->addWidget(system_button_);
    theme_layout->addStretch(1);
    layout->addWidget(
        MakeLabeledRow(QStringLiteral("主题模式"), theme_control, page));
    connect(theme_group_, &QButtonGroup::idClicked, this, [this](int id) {
        SetThemeMode(id, true);
    });

    theme_hint_ = new QLabel(page);
    theme_hint_->setObjectName(QStringLiteral("themeHint"));
    theme_hint_->setContentsMargins(kLabelWidth + 16, 0, 0, 0);
    theme_hint_->setFixedHeight(18);
    layout->addWidget(theme_hint_);
    theme_hint_timer_ = new QTimer(this);
    theme_hint_timer_->setSingleShot(true);
    theme_hint_timer_->setInterval(4000);
    connect(theme_hint_timer_, &QTimer::timeout, this,
            [this] { theme_hint_->clear(); });
    layout->addSpacing(6);

    language_combo_ = MakeCombo(page, 220);
    language_combo_->setObjectName(QStringLiteral("languageCombo"));
    language_combo_->addItem(QStringLiteral("简体中文"));
    layout->addWidget(
        MakeLabeledRow(QStringLiteral("语言设置"), language_combo_, page));
    layout->addSpacing(20);

    layout->addWidget(MakeSectionTitle(QStringLiteral("其他设置"), page));
    layout->addSpacing(8);
    hide_notifications_check_ =
        new ToggleCheckBox(QStringLiteral("录制时隐藏桌面通知"), page);
    hide_notifications_check_->setObjectName(
        QStringLiteral("hideNotificationsCheck"));
    hide_notifications_check_->setChecked(
        base_.hide_notifications_while_recording);
    layout->addWidget(hide_notifications_check_);
    layout->addSpacing(10);
    show_complete_check_ =
        new ToggleCheckBox(QStringLiteral("显示录制完成提示"), page);
    show_complete_check_->setObjectName(QStringLiteral("showCompleteCheck"));
    show_complete_check_->setChecked(
        base_.show_recording_complete_notification);
    layout->addWidget(show_complete_check_);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::BuildRecordingPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(MakePageTitle(QStringLiteral("录制设置"), page));
    layout->addSpacing(18);

    layout->addWidget(MakeSectionTitle(QStringLiteral("视频设置"), page));
    layout->addSpacing(8);

    container_combo_ = MakeCombo(page, 220);
    container_combo_->setObjectName(QStringLiteral("formatCombo"));
    container_combo_->addItem(QStringLiteral("MP4"), QStringLiteral("mp4"));
    container_combo_->addItem(QStringLiteral("MOV"), QStringLiteral("mov"));
    container_combo_->addItem(QStringLiteral("MKV"), QStringLiteral("mkv"));
    container_combo_->addItem(QStringLiteral("AVI"), QStringLiteral("avi"));
    container_combo_->addItem(QStringLiteral("FLV"), QStringLiteral("flv"));
    container_combo_->addItem(QStringLiteral("WMV"), QStringLiteral("wmv"));
    {
        const int index =
            container_combo_->findData(base_.container_extension);
        container_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    layout->addWidget(
        MakeLabeledRow(QStringLiteral("保存格式"), container_combo_, page));

    compatibility_hint_ = new QLabel(page);
    compatibility_hint_->setObjectName(QStringLiteral("fieldError"));
    compatibility_hint_->setContentsMargins(kLabelWidth + 16, 0, 0, 0);
    compatibility_hint_->setFixedHeight(18);
    layout->addWidget(compatibility_hint_);

    bitrate_combo_ = MakeCombo(page, 220);
    bitrate_combo_->setObjectName(QStringLiteral("bitrateCombo"));
    bitrate_combo_->addItem(QStringLiteral("20 Mbps"),
                            QVariant::fromValue<qlonglong>(20000000));
    bitrate_combo_->addItem(QStringLiteral("50 Mbps"),
                            QVariant::fromValue<qlonglong>(50000000));
    bitrate_combo_->addItem(QStringLiteral("100 Mbps"),
                            QVariant::fromValue<qlonglong>(100000000));
    {
        const int index = bitrate_combo_->findData(QVariant::fromValue<qlonglong>(
            static_cast<qlonglong>(base_.video_bitrate)));
        bitrate_combo_->setCurrentIndex(index >= 0 ? index : 1);
    }
    layout->addWidget(
        MakeLabeledRow(QStringLiteral("码率"), bitrate_combo_, page));
    layout->addSpacing(10);

    hdr_check_ = new ToggleCheckBox(QStringLiteral("HDR（色调映射为 SDR）"), page);
    hdr_check_->setChecked(base_.hdr);
    layout->addWidget(hdr_check_);
    auto* hdr_hint = MakeHint(QStringLiteral("仅全屏录制生效"), page);
    hdr_hint->setContentsMargins(26, 0, 0, 0);
    layout->addWidget(hdr_hint);
    layout->addSpacing(18);

    layout->addWidget(MakeSectionTitle(QStringLiteral("音频设置"), page));
    layout->addSpacing(8);

    audio_codec_combo_ = MakeCombo(page, 220);
    audio_codec_combo_->setObjectName(QStringLiteral("audioCodecCombo"));
    audio_codec_combo_->addItem(QStringLiteral("AAC"),
                                static_cast<int>(hmmob::AudioCodec::Aac));
    audio_codec_combo_->addItem(
        QStringLiteral("PCM"),
        static_cast<int>(hmmob::AudioCodec::PcmS16Le));
    {
        const int index = audio_codec_combo_->findData(
            static_cast<int>(base_.audio_codec));
        audio_codec_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    layout->addWidget(
        MakeLabeledRow(QStringLiteral("音频格式"), audio_codec_combo_, page));
    layout->addSpacing(10);

    sample_rate_combo_ = MakeCombo(page, 220);
    sample_rate_combo_->setObjectName(QStringLiteral("sampleRateCombo"));
    sample_rate_combo_->addItem(QStringLiteral("44.1 kHz"), 44100);
    sample_rate_combo_->addItem(QStringLiteral("48 kHz"), 48000);
    {
        const int index = sample_rate_combo_->findData(base_.audio_sample_rate);
        sample_rate_combo_->setCurrentIndex(index >= 0 ? index : 1);
    }
    layout->addWidget(
        MakeLabeledRow(QStringLiteral("采样率"), sample_rate_combo_, page));
    layout->addSpacing(10);

    audio_bitrate_combo_ = MakeCombo(page, 220);
    audio_bitrate_combo_->setObjectName(QStringLiteral("audioBitrateCombo"));
    audio_bitrate_combo_->addItem(QStringLiteral("96 kbps"), 96000);
    audio_bitrate_combo_->addItem(QStringLiteral("128 kbps"), 128000);
    audio_bitrate_combo_->addItem(QStringLiteral("192 kbps"), 192000);
    audio_bitrate_combo_->addItem(QStringLiteral("320 kbps"), 320000);
    {
        const int index = audio_bitrate_combo_->findData(base_.audio_bitrate);
        audio_bitrate_combo_->setCurrentIndex(index >= 0 ? index : 2);
    }
    layout->addWidget(
        MakeLabeledRow(QStringLiteral("音频码率"), audio_bitrate_combo_, page));
    layout->addSpacing(18);

    layout->addWidget(MakeSectionTitle(QStringLiteral("摄像头"), page));
    layout->addSpacing(8);
    camera_combo_ = MakeCombo(page, 240);
    camera_combo_->setObjectName(QStringLiteral("cameraCombo"));
    const std::vector<hmmob::CameraSource::CameraDeviceInfo> devices =
        hmmob::CameraSource::EnumerateDevices();
    if (devices.empty()) {
        camera_combo_->addItem(QStringLiteral("未检测到摄像头"));
        camera_combo_->setEnabled(false);
    } else {
        for (size_t i = 0; i < devices.size(); ++i) {
            camera_combo_->addItem(QString::fromStdString(devices[i].name),
                                   static_cast<int>(i));
        }
        const int index = camera_combo_->findData(base_.camera_index);
        camera_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    layout->addWidget(
        MakeLabeledRow(QStringLiteral("设备"), camera_combo_, page));
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::BuildShortcutsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(MakePageTitle(QStringLiteral("快捷键"), page));
    layout->addSpacing(18);
    layout->addWidget(MakeSectionTitle(QStringLiteral("录制快捷键"), page));
    layout->addSpacing(6);

    auto add_shortcut = [&](const QString& label, const QString& key,
                            bool available) {
        auto* control = new QWidget(page);
        auto* control_layout = new QHBoxLayout(control);
        control_layout->setContentsMargins(0, 0, 0, 0);
        control_layout->setSpacing(0);
        if (available) {
            auto* chip = new QLabel(key, control);
            chip->setObjectName(QStringLiteral("keyChip"));
            control_layout->addWidget(chip);
        } else {
            auto* deferred = new QLabel(QStringLiteral("后续版本"), control);
            deferred->setObjectName(QStringLiteral("deferredLabel"));
            control_layout->addWidget(deferred);
        }
        control_layout->addStretch(1);
        auto* row = MakeLabeledRow(label, control, page, 160);
        row->setFixedHeight(44);
        layout->addWidget(row);
    };

    add_shortcut(QStringLiteral("开始 / 停止录制"), QStringLiteral("Ctrl+F9"),
                 true);
    layout->addWidget(MakeSeparator(page));
    add_shortcut(QStringLiteral("暂停 / 继续"), QStringLiteral("Ctrl+F10"),
                 false);
    layout->addWidget(MakeSeparator(page));
    add_shortcut(QStringLiteral("截图"), QString(), false);

    layout->addSpacing(20);
    layout->addWidget(
        MakeNote(QStringLiteral("快捷键自定义将在后续版本提供"), page));
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::BuildOutputPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(MakePageTitle(QStringLiteral("输出设置"), page));
    layout->addSpacing(18);
    layout->addWidget(MakeSectionTitle(QStringLiteral("保存目录"), page));
    layout->addSpacing(8);

    auto* row = new QWidget(page);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(16);
    auto* label = MakeFieldLabel(QStringLiteral("保存目录"), row);
    label->setFixedWidth(kLabelWidth);
    row_layout->addWidget(label, 0, Qt::AlignVCenter);

    directory_edit_ = new QLineEdit(row);
    directory_edit_->setObjectName(QStringLiteral("directoryEdit"));
    directory_edit_->setFixedHeight(38);
    directory_edit_->setMinimumWidth(360);
    const QString directory = base_.output_directory.isEmpty()
                                  ? DefaultOutputDirectory()
                                  : base_.output_directory;
    directory_edit_->setText(QDir::toNativeSeparators(directory));
    directory_edit_->setCursorPosition(0);
    row_layout->addWidget(directory_edit_, 1);

    auto* browse = new QPushButton(QStringLiteral("浏览…"), row);
    browse->setObjectName(QStringLiteral("outline"));
    browse->setCursor(Qt::PointingHandCursor);
    browse->setFocusPolicy(Qt::NoFocus);
    browse->setFixedSize(88, 38);
    row_layout->addWidget(browse, 0, Qt::AlignVCenter);
    layout->addWidget(row);
    connect(browse, &QPushButton::clicked, this,
            &SettingsDialog::BrowseOutputDirectory);

    layout->addSpacing(16);
    layout->addWidget(MakeSectionTitle(QStringLiteral("文件命名"), page));
    layout->addSpacing(8);
    auto_timestamp_check_ =
        new ToggleCheckBox(QStringLiteral("自动使用时间戳文件名"), page);
    auto_timestamp_check_->setObjectName(
        QStringLiteral("autoTimestampCheck"));
    auto_timestamp_check_->setChecked(base_.auto_timestamp_filename);
    layout->addWidget(auto_timestamp_check_);
    auto* timestamp_hint = MakeHint(
        QStringLiteral("文件名格式：Hmmob-yyyyMMdd-HHmmss.<保存格式>"), page);
    timestamp_hint->setContentsMargins(26, 0, 0, 0);
    layout->addWidget(timestamp_hint);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::BuildAboutPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(MakePageTitle(QStringLiteral("关于我们"), page));
    layout->addSpacing(24);

    auto* mark = new QLabel(page);
    mark->setPixmap(IconRenderer::Pixmap(Icon::Play, 48, kPrimary));
    mark->setFixedSize(48, 48);
    layout->addWidget(mark);
    layout->addSpacing(12);

    auto* name = new QLabel(QStringLiteral("Hmmob 录屏"), page);
    name->setFont(UiFont(20, QFont::DemiBold));
    name->setStyleSheet(QStringLiteral("color:#1D2129;"));
    layout->addWidget(name);
    layout->addSpacing(4);

    QString version = QCoreApplication::applicationVersion();
    if (version.isEmpty()) {
        version = QStringLiteral("0.1.0");
    }
    auto* version_label =
        new QLabel(QStringLiteral("版本 %1").arg(version), page);
    version_label->setObjectName(QStringLiteral("aboutVersion"));
    layout->addWidget(version_label);
    layout->addSpacing(18);

    auto add_paragraph = [&](const QString& text) {
        auto* label = new QLabel(text, page);
        label->setObjectName(QStringLiteral("aboutText"));
        label->setWordWrap(true);
        label->setMaximumWidth(620);
        layout->addWidget(label);
        layout->addSpacing(10);
    };
    add_paragraph(QStringLiteral(
        "一款面向 Windows 的屏幕录制工具，支持全屏、区域、窗口与摄像头录制。"));
    add_paragraph(QStringLiteral(
        "采集使用 DXGI Desktop Duplication 与 Windows Graphics Capture，硬件"
        "编码基于 AMD AMF，封装与音频处理使用 FFmpeg 与 WASAPI。"));
    add_paragraph(QStringLiteral(
        "本项目为净室（clean-room）实现，界面与代码均由本项目独立完成，不"
        "包含第三方专有代码。"));
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::BuildFooter() {
    auto* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("settingsFooter"));
    footer->setAttribute(Qt::WA_StyledBackground, true);
    footer->setFixedHeight(76);

    auto* layout = new QHBoxLayout(footer);
    layout->setContentsMargins(0, 0, 0, 20);
    layout->setSpacing(10);
    layout->addStretch(1);

    auto* restore = new QPushButton(QStringLiteral("恢复默认"), footer);
    restore->setObjectName(QStringLiteral("outline"));
    restore->setCursor(Qt::PointingHandCursor);
    restore->setFocusPolicy(Qt::NoFocus);
    restore->setFixedSize(96, 38);
    layout->addWidget(restore);

    save_button_ = new QPushButton(QStringLiteral("保存设置"), footer);
    save_button_->setObjectName(QStringLiteral("saveButton"));
    save_button_->setCursor(Qt::PointingHandCursor);
    save_button_->setFocusPolicy(Qt::NoFocus);
    save_button_->setDefault(true);
    save_button_->setFixedSize(96, 38);
    layout->addWidget(save_button_);

    connect(restore, &QPushButton::clicked, this, [this] {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QStringLiteral("Hmmob 录屏"),
            QStringLiteral("确定要恢复所有设置的默认值吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes) {
            ResetToDefaults();
        }
    });
    connect(save_button_, &QPushButton::clicked, this, [this] {
        if (save_button_->isEnabled()) {
            accept();
        }
    });
    footer->raise();
    return footer;
}

void SettingsDialog::UpdateCompatibility() {
    const QString extension = container_combo_->currentData().toString();
    const auto audio_codec = static_cast<hmmob::AudioCodec>(
        audio_codec_combo_->currentData().toInt());

    QString hint;
    if (hevc_ && (extension == QStringLiteral("flv") ||
                  extension == QStringLiteral("avi") ||
                  extension == QStringLiteral("wmv"))) {
        hint = QStringLiteral(
            "HEVC 不支持该保存格式，请改用 H.264 或更换格式");
    } else if (extension == QStringLiteral("flv") &&
               audio_codec == hmmob::AudioCodec::PcmS16Le) {
        hint = QStringLiteral(
            "FLV 保存格式不支持 PCM 音频，请改用 AAC 或更换格式");
    }

    compatibility_hint_->setText(hint);
    save_button_->setEnabled(hint.isEmpty());
    audio_bitrate_combo_->setEnabled(audio_codec == hmmob::AudioCodec::Aac);
}

void SettingsDialog::SetThemeMode(int mode, bool announce) {
    // Only the light theme exists in this build. Selecting another option
    // explains that and snaps the selection back.
    {
        const QSignalBlocker blocker(theme_group_);
        light_button_->setChecked(true);
        dark_button_->setChecked(false);
        system_button_->setChecked(false);
    }
    if (announce && mode != 0) {
        theme_hint_->setText(QStringLiteral("深色主题将在后续版本提供"));
        theme_hint_timer_->start();
    } else {
        theme_hint_timer_->stop();
        theme_hint_->clear();
    }
}

void SettingsDialog::ResetToDefaults() {
    const AppSettings defaults;

    auto_start_check_->setChecked(defaults.auto_start);
    start_hidden_check_->setChecked(defaults.minimize_to_tray_on_start);
    close_to_tray_check_->setChecked(defaults.close_to_tray);
    hide_notifications_check_->setChecked(
        defaults.hide_notifications_while_recording);
    show_complete_check_->setChecked(
        defaults.show_recording_complete_notification);
    SetThemeMode(0, false);
    language_combo_->setCurrentIndex(0);

    {
        const QSignalBlocker container_blocker(container_combo_);
        const QSignalBlocker codec_blocker(audio_codec_combo_);
        container_combo_->setCurrentIndex(
            container_combo_->findData(defaults.container_extension));
        bitrate_combo_->setCurrentIndex(bitrate_combo_->findData(
            QVariant::fromValue<qlonglong>(
                static_cast<qlonglong>(defaults.video_bitrate))));
        hdr_check_->setChecked(defaults.hdr);
        audio_codec_combo_->setCurrentIndex(audio_codec_combo_->findData(
            static_cast<int>(defaults.audio_codec)));
        sample_rate_combo_->setCurrentIndex(
            sample_rate_combo_->findData(defaults.audio_sample_rate));
        audio_bitrate_combo_->setCurrentIndex(
            audio_bitrate_combo_->findData(defaults.audio_bitrate));
        if (camera_combo_->isEnabled()) {
            camera_combo_->setCurrentIndex(0);
        }
    }
    UpdateCompatibility();

    directory_edit_->setText(
        QDir::toNativeSeparators(DefaultOutputDirectory()));
    directory_edit_->setCursorPosition(0);
    auto_timestamp_check_->setChecked(defaults.auto_timestamp_filename);
}

void SettingsDialog::BrowseOutputDirectory() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择保存目录"), directory_edit_->text());
    if (!directory.isEmpty()) {
        directory_edit_->setText(QDir::toNativeSeparators(directory));
        directory_edit_->setCursorPosition(0);
    }
}

AppSettings SettingsDialog::Collect() const {
    AppSettings result = base_;
    result.container_extension = container_combo_->currentData().toString();
    result.video_bitrate = bitrate_combo_->currentData().toLongLong();
    result.hdr = hdr_check_->isChecked();
    result.audio_codec = static_cast<hmmob::AudioCodec>(
        audio_codec_combo_->currentData().toInt());
    result.audio_sample_rate = sample_rate_combo_->currentData().toInt();
    result.audio_bitrate = audio_bitrate_combo_->currentData().toInt();
    if (camera_combo_->isEnabled()) {
        result.camera_index = camera_combo_->currentData().toInt();
    }
    result.output_directory =
        QDir::fromNativeSeparators(directory_edit_->text().trimmed());
    result.auto_timestamp_filename = auto_timestamp_check_->isChecked();
    result.auto_start = auto_start_check_->isChecked();
    result.minimize_to_tray_on_start = start_hidden_check_->isChecked();
    result.close_to_tray = close_to_tray_check_->isChecked();
    result.hide_notifications_while_recording =
        hide_notifications_check_->isChecked();
    result.show_recording_complete_notification =
        show_complete_check_->isChecked();
    return result;
}

AppSettings SettingsDialog::settings() const { return Collect(); }

void SettingsDialog::SelectPage(int page) {
    if (!stack_ || page < 0 || page >= stack_->count()) {
        return;
    }
    stack_->setCurrentIndex(page);
    for (int i = 0; i < nav_buttons_.size(); ++i) {
        nav_buttons_.at(i)->setChecked(i == page);
    }
}

void SettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        const int preference = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &preference, sizeof(preference));
    }
#endif
}
