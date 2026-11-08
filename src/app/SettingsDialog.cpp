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
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
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
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

constexpr int kDialogWidth = 980;
constexpr int kDialogHeight = 640;
constexpr int kDialogMinWidth = 900;
constexpr int kDialogMinHeight = 560;
constexpr int kNavWidth = 200;
constexpr int kLabelWidth = 88;

// Three equal segments: 118 px each with 8 px gaps.
constexpr int kSegmentWidth = 118;
constexpr int kSegmentGap = 8;
constexpr int kSegmentRowWidth = kSegmentWidth * 3 + kSegmentGap * 2;

const char* const kSettingsStyleTemplate = R"(
QDialog { background: {card}; }
QWidget#settingsHeader { background: {card}; }
QWidget#settingsNavPane { background: {card}; }
QWidget#settingsContent { background: {card}; }
QWidget#settingsFooter { background: {card}; }
QLabel#pageTitle { font-size: 20px; font-weight: 600; color: {text}; }
QLabel#sectionTitle { font-size: 13px; font-weight: 600; color: {textMuted}; }
QLabel#fieldLabel { font-size: 14px; color: {text}; }
QLabel#fieldHint { font-size: 12px; color: {textMuted}; }
QLabel#fieldError { font-size: 12px; color: {danger}; }
QLabel#noteLabel { font-size: 12px; color: {textMuted}; }
QLabel#deferredLabel { font-size: 13px; color: {disabledText}; }
QLabel#appTitle, QLabel#aboutName { color: {text}; }
QLabel#keyChip { background: {keyChipBg}; border: 1px solid {keyChipBorder}; border-radius: 6px; padding: 3px 10px; font-size: 13px; color: {textSecondary}; }
QLabel#aboutText { font-size: 13px; color: {textSecondary}; }
QLabel#aboutVersion { font-size: 13px; color: {textMuted}; }
QFrame#rowSeparator { background: {separator}; border: none; }
QComboBox { background: {inputBg}; border: 1px solid {inputBorder}; border-radius: 10px; padding: 0 10px 0 14px; font-size: 14px; color: {text}; }
QComboBox:hover { border-color: {iconMuted}; }
QComboBox:disabled { background: {disabledBg}; color: {disabledText}; border-color: {disabledBorder}; }
QComboBox::drop-down { border: none; background: transparent; width: 34px; }
QComboBox::down-arrow { image: none; width: 0px; height: 0px; }
QComboBox QAbstractItemView { background: {menuBg}; border: 1px solid {menuBorder}; border-radius: 8px; padding: 4px; outline: 0; color: {text}; selection-background-color: {selectionBg}; selection-color: {selectionText}; }
QComboBox QAbstractItemView::item { min-height: 30px; padding: 0 10px; }
QComboBox QAbstractItemView::item:hover { background: {hoverBg}; }
QLineEdit { background: {inputBg}; border: 1px solid {inputBorder}; border-radius: 10px; padding: 0 12px; font-size: 14px; color: {text}; selection-background-color: {primary}; selection-color: #FFFFFF; }
QLineEdit:hover { border-color: {iconMuted}; }
QLineEdit:disabled { background: {disabledBg}; color: {disabledText}; border-color: {disabledBorder}; }
QPushButton { min-height: 38px; padding: 0 20px; border-radius: 10px; font-size: 14px; }
QPushButton#outline { background: {inputBg}; border: 1px solid {inputBorder}; color: {textSecondary}; }
QPushButton#outline:hover { background: {hoverBg}; }
QPushButton#outline:disabled { color: {disabledText}; border-color: {disabledBorder}; }
QPushButton#saveButton { background: {primary}; border: 1px solid {primary}; color: #FFFFFF; }
QPushButton#saveButton:hover { background: {primaryHover}; }
QPushButton#saveButton:pressed { background: {primaryPressed}; }
QPushButton#saveButton:disabled { background: {primaryDisabled}; border-color: {primaryDisabled}; color: {primaryDisabledText}; }
QPushButton#segment { background: {card}; border: 1px solid {inputBorder}; border-radius: 10px; color: {textSecondary}; font-size: 14px; padding: 0 10px; text-align: center; }
QPushButton#segment:hover { border-color: {iconMuted}; }
QPushButton#segment:checked { background: {primarySoft}; border: 1px solid {primary}; color: {primary}; }
QPushButton#segment:disabled { color: {disabledText}; }
QScrollArea#settingsScroll { background: transparent; border: none; }
QScrollArea#settingsScroll > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0px; }
QScrollBar::handle:vertical { background: {scrollHandle}; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: {scrollHandleHover}; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: transparent; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QToolTip { background: {menuBg}; color: {text}; border: 1px solid {menuBorder}; padding: 4px 8px; }
QMenu { background: {menuBg}; border: 1px solid {menuBorder}; border-radius: 8px; padding: 4px; color: {text}; }
QMenu::item { padding: 6px 24px 6px 14px; border-radius: 6px; }
QMenu::item:selected { background: {selectionBg}; color: {selectionText}; }
QMenu::item:disabled { color: {disabledText}; }
QMenu::separator { height: 1px; background: {divider}; margin: 4px 8px; }
QMessageBox { background: {card}; }
QMessageBox QLabel { color: {text}; }
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
        connect(&Theme::Instance(), &Theme::changed, this,
                [this] { update(); });
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
        const ThemeColors& colors = Theme::Instance().colors();
        const QRectF bounds = rect();
        if (isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.navActiveBg);
            painter.drawRoundedRect(bounds, 10.0, 10.0);
        } else if (hover_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.hoverBg);
            painter.drawRoundedRect(bounds, 10.0, 10.0);
        }

        const QColor color =
            isChecked() ? colors.primary : colors.textSecondary;
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
        connect(&Theme::Instance(), &Theme::changed, this,
                [this] { update(); });
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
        const ThemeColors& colors = Theme::Instance().colors();
        if (hover_) {
            painter.fillRect(rect(), colors.dangerStrong);
        }
        IconRenderer::Paint(
            painter, Icon::Close,
            QRectF((width() - 16.0) / 2.0, (height() - 16.0) / 2.0, 16, 16),
            hover_ ? QColor(Qt::white) : colors.icon, 1.5);
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
    button->setMinimumSize(kSegmentWidth, 38);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const ThemeColors& colors = Theme::Instance().colors();
    QIcon qicon;
    qicon.addPixmap(IconRenderer::Pixmap(icon, 18, colors.textSecondary),
                    QIcon::Normal, QIcon::Off);
    qicon.addPixmap(IconRenderer::Pixmap(icon, 18, colors.primary),
                    QIcon::Normal, QIcon::On);
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
    setStyleSheet(Theme::Instance().Expand(
        QString::fromUtf8(kSettingsStyleTemplate)));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumSize(kDialogMinWidth, kDialogMinHeight);
    resize(kDialogWidth, kDialogHeight);

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

    // The pages are taller than the minimum dialog height on some DPI/font
    // combinations; the scroll area keeps every control reachable.
    auto* scroll = new QScrollArea(content);
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->viewport()->setAutoFillBackground(false);

    stack_ = new QStackedWidget(scroll);
    stack_->addWidget(BuildGeneralPage());
    stack_->addWidget(BuildRecordingPage());
    stack_->addWidget(BuildShortcutsPage());
    stack_->addWidget(BuildOutputPage());
    stack_->addWidget(BuildAboutPage());
    scroll->setWidget(stack_);
    content_layout->addWidget(scroll, 1);
    content_layout->addWidget(BuildFooter(), 0);

    body_layout->addWidget(content, 1);
    root->addWidget(body, 1);

    for (int i = 0; i < nav_buttons_.size(); ++i) {
        connect(nav_buttons_.at(i), &QAbstractButton::clicked, this,
                [this, i] { SelectPage(i); });
    }
    nav_buttons_.front()->setChecked(true);
    stack_->setCurrentIndex(0);

    initial_theme_ = Theme::Instance().mode();
    SetThemeMode(static_cast<int>(initial_theme_), false);
    connect(&Theme::Instance(), &Theme::changed, this,
            &SettingsDialog::ApplyTheme);

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

    header_mark_ = new QLabel(header);
    header_mark_->setPixmap(IconRenderer::Pixmap(
        Icon::Play, 26, Theme::Instance().colors().primary));
    header_mark_->setFixedSize(26, 26);
    layout->addWidget(header_mark_, 0, Qt::AlignVCenter);
    layout->addSpacing(12);

    auto* title = new QLabel(QStringLiteral("Hmmob 录屏"), header);
    title->setObjectName(QStringLiteral("appTitle"));
    title->setFont(UiFont(16, QFont::DemiBold));
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

    // A fixed-width row with three equal segments. The width is smaller than
    // the narrowest content column (900 px dialog at any DPI), so the third
    // segment and its border always stay inside the page.
    auto* theme_control = new QWidget(page);
    theme_control->setFixedWidth(kSegmentRowWidth);
    theme_control->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* theme_layout = new QHBoxLayout(theme_control);
    theme_layout->setContentsMargins(0, 0, 0, 0);
    theme_layout->setSpacing(kSegmentGap);
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
    theme_layout->addWidget(light_button_, 1);
    theme_layout->addWidget(dark_button_, 1);
    theme_layout->addWidget(system_button_, 1);
    layout->addWidget(
        MakeLabeledRow(QStringLiteral("主题模式"), theme_control, page));
    connect(theme_group_, &QButtonGroup::idClicked, this, [this](int id) {
        SetThemeMode(id, true);
    });
    layout->addSpacing(10);

    language_combo_ = MakeCombo(page, 220);
    language_combo_->setObjectName(QStringLiteral("languageCombo"));
    language_combo_->addItem(QStringLiteral("简体中文"),
                             QStringLiteral("zh-CN"));
    {
        const int index = language_combo_->findData(base_.language);
        language_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
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

    about_mark_ = new QLabel(page);
    about_mark_->setPixmap(IconRenderer::Pixmap(
        Icon::Play, 48, Theme::Instance().colors().primary));
    about_mark_->setFixedSize(48, 48);
    layout->addWidget(about_mark_);
    layout->addSpacing(12);

    auto* name = new QLabel(QStringLiteral("Hmmob 录屏"), page);
    name->setObjectName(QStringLiteral("aboutName"));
    name->setFont(UiFont(20, QFont::DemiBold));
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
    (void)announce;
    if (mode < 0 || mode > 2) {
        mode = 0;
    }
    {
        const QSignalBlocker blocker(theme_group_);
        light_button_->setChecked(mode == 0);
        dark_button_->setChecked(mode == 1);
        system_button_->setChecked(mode == 2);
    }
    // Live preview; Theme persists the choice. 取消 restores initial_theme_.
    Theme::Instance().SetMode(static_cast<ThemeMode>(mode));
}

void SettingsDialog::ApplyTheme() {
    const ThemeColors& colors = Theme::Instance().colors();
    setStyleSheet(Theme::Instance().Expand(
        QString::fromUtf8(kSettingsStyleTemplate)));
    UpdateSegmentIcons();
    if (header_mark_) {
        header_mark_->setPixmap(
            IconRenderer::Pixmap(Icon::Play, 26, colors.primary));
    }
    if (about_mark_) {
        about_mark_->setPixmap(
            IconRenderer::Pixmap(Icon::Play, 48, colors.primary));
    }
#ifdef Q_OS_WIN
    if (windowHandle()) {
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        const BOOL dark =
            Theme::Instance().effectiveMode() == ThemeMode::Dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                              sizeof(dark));
    }
#endif
    update();
}

void SettingsDialog::UpdateSegmentIcons() {
    const ThemeColors& colors = Theme::Instance().colors();
    auto update = [&colors](QPushButton* button, Icon icon) {
        if (!button) {
            return;
        }
        QIcon qicon;
        qicon.addPixmap(IconRenderer::Pixmap(icon, 18, colors.textSecondary),
                        QIcon::Normal, QIcon::Off);
        qicon.addPixmap(IconRenderer::Pixmap(icon, 18, colors.primary),
                        QIcon::Normal, QIcon::On);
        button->setIcon(qicon);
    };
    update(light_button_, Icon::Sun);
    update(dark_button_, Icon::Moon);
    update(system_button_, Icon::Monitor);
}

void SettingsDialog::reject() {
    // The dialog previews the theme as soon as a segment is clicked; closing
    // without 保存设置 must put the saved mode back.
    Theme::Instance().SetMode(initial_theme_);
    QDialog::reject();
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
    if (language_combo_) {
        language_combo_->setCurrentIndex(0);
    }

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
    if (language_combo_) {
        result.language = language_combo_->currentData().toString();
    }
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
        const BOOL dark =
            Theme::Instance().effectiveMode() == ThemeMode::Dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                              sizeof(dark));
    }
#endif
}
