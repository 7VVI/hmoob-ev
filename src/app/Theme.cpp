#include "app/Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>
#include <QTimer>

namespace {

QColor Rgb(int r, int g, int b) { return QColor(r, g, b); }

ThemeColors LightColors() {
    ThemeColors c;
    c.window = Rgb(0xF5, 0xF7, 0xFC);
    c.card = Rgb(0xFF, 0xFF, 0xFF);
    c.cardBorder = Rgb(0xED, 0xF0, 0xF5);
    c.border = Rgb(0xE5, 0xE6, 0xEB);
    c.text = Rgb(0x1D, 0x21, 0x29);
    c.textSecondary = Rgb(0x4E, 0x59, 0x69);
    c.textMuted = Rgb(0x86, 0x90, 0x9C);
    c.primary = Rgb(0x2F, 0x6B, 0xFF);
    c.primaryHover = Rgb(0x24, 0x5B, 0xF0);
    c.primaryPressed = Rgb(0x1E, 0x52, 0xE8);
    c.primaryDisabled = Rgb(0xB7, 0xC7, 0xF5);
    c.primaryDisabledText = Rgb(0xFF, 0xFF, 0xFF);
    c.primarySoft = Rgb(0xEE, 0xF5, 0xFF);
    c.inputBg = Rgb(0xFF, 0xFF, 0xFF);
    c.inputBorder = Rgb(0xE5, 0xE6, 0xEB);
    c.divider = Rgb(0xE5, 0xE6, 0xEB);
    c.softBorder = Rgb(0xEE, 0xF1, 0xF5);
    c.separator = Rgb(0xF2, 0xF3, 0xF5);
    c.danger = Rgb(0xE5, 0x48, 0x4D);
    c.dangerHover = Rgb(0xD9, 0x3A, 0x40);
    c.dangerStrong = Rgb(0xE8, 0x11, 0x23);
    c.selectionBg = Rgb(0xEA, 0xF2, 0xFE);
    c.selectionText = Rgb(0x2F, 0x6B, 0xFF);
    c.icon = Rgb(0x3A, 0x42, 0x54);
    c.iconMuted = Rgb(0x86, 0x90, 0x9C);
    c.iconDisabled = Rgb(0xB4, 0xBA, 0xC6);
    c.hoverBg = Rgb(0xED, 0xF0, 0xF5);
    c.pressedBg = Rgb(0xE3, 0xEA, 0xF5);
    c.disabledText = Rgb(0xA9, 0xAE, 0xB8);
    c.disabledBg = Rgb(0xF7, 0xF8, 0xFA);
    c.disabledBorder = Rgb(0xED, 0xF0, 0xF5);
    c.scrollHandle = Rgb(0xD5, 0xDB, 0xE5);
    c.scrollHandleHover = Rgb(0xC2, 0xC9, 0xD6);
    c.previewBg = Rgb(0xEE, 0xF2, 0xF8);
    c.previewBorder = Rgb(0xE7, 0xEB, 0xF2);
    c.placeholder = Rgb(0x9A, 0xA3, 0xB2);
    c.titleBar = Rgb(0xF5, 0xF7, 0xFC);
    c.navBar = Rgb(0xF5, 0xF7, 0xFC);
    c.bottomBar = Rgb(0xFA, 0xFB, 0xFD);
    c.navActiveBg = Rgb(0xEA, 0xF2, 0xFE);
    c.navHoverBg = Rgb(0xF0, 0xF4, 0xFB);
    c.navActionHover = Rgb(0xED, 0xF2, 0xFA);
    c.navActionPressed = Rgb(0xE3, 0xEA, 0xF5);
    c.tileBg = Rgb(0xFF, 0xFF, 0xFF);
    c.tileBorder = Rgb(0xE5, 0xE6, 0xEB);
    c.tileHoverBg = Rgb(0xF7, 0xF9, 0xFD);
    c.tileHoverBorder = Rgb(0xD5, 0xDB, 0xE5);
    c.tileSelectedBg = Rgb(0xEE, 0xF5, 0xFF);
    c.tileSelectedBorder = Rgb(0x2F, 0x6B, 0xFF);
    c.keyChipBg = Rgb(0xF7, 0xF8, 0xFA);
    c.keyChipBorder = Rgb(0xED, 0xF0, 0xF5);
    c.pathBoxBg = Rgb(0xF7, 0xF8, 0xFA);
    c.menuBg = Rgb(0xFF, 0xFF, 0xFF);
    c.menuBorder = Rgb(0xE5, 0xE6, 0xEB);
    c.errorBadgeBg = Rgb(0xE5, 0x48, 0x4D);
    return c;
}

ThemeColors DarkColors() {
    ThemeColors c;
    c.window = Rgb(0x15, 0x17, 0x1C);
    c.card = Rgb(0x1D, 0x20, 0x27);
    c.cardBorder = Rgb(0x2A, 0x2E, 0x37);
    c.border = Rgb(0x2A, 0x2E, 0x37);
    c.text = Rgb(0xE6, 0xE8, 0xEC);
    c.textSecondary = Rgb(0xA7, 0xAD, 0xB8);
    c.textMuted = Rgb(0x7C, 0x83, 0x90);
    c.primary = Rgb(0x3B, 0x82, 0xF6);
    c.primaryHover = Rgb(0x4F, 0x92, 0xF8);
    c.primaryPressed = Rgb(0x2F, 0x6F, 0xE0);
    c.primaryDisabled = Rgb(0x2E, 0x4A, 0x7A);
    c.primaryDisabledText = Rgb(0xC7, 0xD2, 0xE8);
    c.primarySoft = Rgb(0x24, 0x30, 0x4A);
    c.inputBg = Rgb(0x23, 0x27, 0x30);
    c.inputBorder = Rgb(0x33, 0x38, 0x43);
    c.divider = Rgb(0x28, 0x2C, 0x35);
    c.softBorder = Rgb(0x28, 0x2C, 0x35);
    c.separator = Rgb(0x24, 0x28, 0x32);
    c.danger = Rgb(0xF8, 0x71, 0x71);
    c.dangerHover = Rgb(0xF9, 0x8D, 0x8D);
    c.dangerStrong = Rgb(0xB9, 0x1C, 0x1C);
    c.selectionBg = Rgb(0x2A, 0x3A, 0x5C);
    c.selectionText = Rgb(0x9C, 0xC0, 0xFF);
    c.icon = Rgb(0xB9, 0xBF, 0xC9);
    c.iconMuted = Rgb(0x7C, 0x83, 0x90);
    c.iconDisabled = Rgb(0x5E, 0x65, 0x72);
    c.hoverBg = Rgb(0x26, 0x2A, 0x33);
    c.pressedBg = Rgb(0x2C, 0x31, 0x3B);
    c.disabledText = Rgb(0x7C, 0x83, 0x90);
    c.disabledBg = Rgb(0x1B, 0x1E, 0x25);
    c.disabledBorder = Rgb(0x2A, 0x2E, 0x37);
    c.scrollHandle = Rgb(0x3A, 0x3F, 0x4B);
    c.scrollHandleHover = Rgb(0x4A, 0x50, 0x5D);
    c.previewBg = Rgb(0x16, 0x19, 0x20);
    c.previewBorder = Rgb(0x2A, 0x2E, 0x37);
    c.placeholder = Rgb(0x8B, 0x92, 0x9F);
    c.titleBar = Rgb(0x15, 0x17, 0x1C);
    c.navBar = Rgb(0x15, 0x17, 0x1C);
    c.bottomBar = Rgb(0x1A, 0x1D, 0x23);
    c.navActiveBg = Rgb(0x24, 0x30, 0x4A);
    c.navHoverBg = Rgb(0x22, 0x26, 0x2F);
    c.navActionHover = Rgb(0x22, 0x26, 0x2F);
    c.navActionPressed = Rgb(0x2A, 0x2F, 0x39);
    c.tileBg = Rgb(0x20, 0x24, 0x2C);
    c.tileBorder = Rgb(0x2E, 0x32, 0x3C);
    c.tileHoverBg = Rgb(0x24, 0x28, 0x33);
    c.tileHoverBorder = Rgb(0x3A, 0x3F, 0x4B);
    c.tileSelectedBg = Rgb(0x24, 0x30, 0x4A);
    c.tileSelectedBorder = Rgb(0x3B, 0x82, 0xF6);
    c.keyChipBg = Rgb(0x23, 0x27, 0x30);
    c.keyChipBorder = Rgb(0x2E, 0x32, 0x3C);
    c.pathBoxBg = Rgb(0x23, 0x27, 0x30);
    c.menuBg = Rgb(0x1D, 0x20, 0x27);
    c.menuBorder = Rgb(0x2A, 0x2E, 0x37);
    c.errorBadgeBg = Rgb(0xB9, 0x1C, 0x1C);
    return c;
}

constexpr auto kThemeKey = "appearance/theme";

}  // namespace

Theme& Theme::Instance() {
    static Theme instance;
    return instance;
}

Theme::Theme() {
    QSettings store;
    mode_ = ModeFromKey(store.value(QString::fromLatin1(kThemeKey),
                                    QStringLiteral("light"))
                            .toString());
    effective_ = mode_ == ThemeMode::Dark ? ThemeMode::Dark : ThemeMode::Light;
    if (mode_ == ThemeMode::System && SystemIsDark()) {
        effective_ = ThemeMode::Dark;
    }
    colors_ = effective_ == ThemeMode::Dark ? DarkColors() : LightColors();
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        app->setPalette(palette());
    }

    if (QStyleHints* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) {
                    if (mode_ == ThemeMode::System) {
                        Refresh();
                    }
                });
    }

    // The registry fallback (and manual registry edits used by tests) do not
    // necessarily broadcast a message Qt listens to, so poll while the user
    // asked us to follow the system.
    system_timer_ = new QTimer(this);
    system_timer_->setInterval(1500);
    connect(system_timer_, &QTimer::timeout, this, [this] {
        if (mode_ != ThemeMode::System) {
            return;
        }
        const ThemeMode wanted =
            SystemIsDark() ? ThemeMode::Dark : ThemeMode::Light;
        if (wanted != effective_) {
            Refresh();
        }
    });
    if (mode_ == ThemeMode::System) {
        system_timer_->start();
    }
}

QString Theme::ModeKey(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Dark:
            return QStringLiteral("dark");
        case ThemeMode::System:
            return QStringLiteral("system");
        case ThemeMode::Light:
        default:
            return QStringLiteral("light");
    }
}

ThemeMode Theme::ModeFromKey(const QString& key) {
    if (key.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::Dark;
    }
    if (key.compare(QStringLiteral("system"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::System;
    }
    return ThemeMode::Light;
}

void Theme::SetMode(ThemeMode mode) {
    mode_ = mode;
    {
        QSettings store;
        store.setValue(QString::fromLatin1(kThemeKey), ModeKey(mode_));
        store.sync();
    }
    if (system_timer_) {
        if (mode_ == ThemeMode::System) {
            system_timer_->start();
        } else {
            system_timer_->stop();
        }
    }
    Refresh();
}

void Theme::Refresh() {
    effective_ = mode_;
    if (mode_ == ThemeMode::System) {
        effective_ = SystemIsDark() ? ThemeMode::Dark : ThemeMode::Light;
    }
    colors_ = effective_ == ThemeMode::Dark ? DarkColors() : LightColors();
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        app->setPalette(palette());
    }
    emit changed();
}

bool Theme::SystemIsDark() const {
    if (QStyleHints* hints = QGuiApplication::styleHints()) {
        switch (hints->colorScheme()) {
            case Qt::ColorScheme::Dark:
                return true;
            case Qt::ColorScheme::Light:
                return false;
            default:
                break;
        }
    }
    QSettings personalize(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\"
            "Themes\\Personalize"),
        QSettings::NativeFormat);
    return personalize.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() ==
           0;
}

QPalette Theme::palette() const {
    const ThemeColors& c = colors_;
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.inputBg);
    p.setColor(QPalette::AlternateBase, c.card);
    p.setColor(QPalette::ToolTipBase, c.menuBg);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::PlaceholderText, c.placeholder);
    p.setColor(QPalette::Button, c.card);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, c.danger);
    p.setColor(QPalette::Link, c.primary);
    p.setColor(QPalette::LinkVisited, c.primaryHover);
    p.setColor(QPalette::Highlight, c.primary);
    p.setColor(QPalette::HighlightedText, QColor(Qt::white));
    p.setColor(QPalette::Light, c.card);
    p.setColor(QPalette::Midlight, c.border);
    p.setColor(QPalette::Mid, c.divider);
    p.setColor(QPalette::Dark, c.disabledBorder);
    p.setColor(QPalette::Shadow, QColor(0, 0, 0));

    p.setColor(QPalette::Disabled, QPalette::WindowText, c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text, c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::Base, c.disabledBg);
    p.setColor(QPalette::Disabled, QPalette::Button, c.disabledBg);
    p.setColor(QPalette::Disabled, QPalette::Highlight, c.disabledBorder);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, c.disabledText);
    return p;
}

QColor Theme::ToneColor(IconTone tone) const {
    switch (tone) {
        case IconTone::Primary:
            return colors_.primary;
        case IconTone::Text:
            return colors_.text;
        case IconTone::Muted:
            return colors_.textMuted;
        case IconTone::Icon:
            return colors_.icon;
        case IconTone::IconDisabled:
            return colors_.iconDisabled;
        case IconTone::Secondary:
        default:
            return colors_.textSecondary;
    }
}

QString Theme::Expand(const QString& css) const {
    const ThemeColors& c = colors_;
    QString result = css;
    result.replace(QStringLiteral("{window}"), c.window.name());
    result.replace(QStringLiteral("{card}"), c.card.name());
    result.replace(QStringLiteral("{cardBorder}"), c.cardBorder.name());
    result.replace(QStringLiteral("{border}"), c.border.name());
    result.replace(QStringLiteral("{textSecondary}"), c.textSecondary.name());
    result.replace(QStringLiteral("{textMuted}"), c.textMuted.name());
    result.replace(QStringLiteral("{text}"), c.text.name());
    result.replace(QStringLiteral("{primaryHover}"), c.primaryHover.name());
    result.replace(QStringLiteral("{primaryPressed}"), c.primaryPressed.name());
    result.replace(QStringLiteral("{primaryDisabledText}"),
                   c.primaryDisabledText.name());
    result.replace(QStringLiteral("{primaryDisabled}"),
                   c.primaryDisabled.name());
    result.replace(QStringLiteral("{primarySoft}"), c.primarySoft.name());
    result.replace(QStringLiteral("{primary}"), c.primary.name());
    result.replace(QStringLiteral("{inputBg}"), c.inputBg.name());
    result.replace(QStringLiteral("{inputBorder}"), c.inputBorder.name());
    result.replace(QStringLiteral("{divider}"), c.divider.name());
    result.replace(QStringLiteral("{softBorder}"), c.softBorder.name());
    result.replace(QStringLiteral("{separator}"), c.separator.name());
    result.replace(QStringLiteral("{dangerHover}"), c.dangerHover.name());
    result.replace(QStringLiteral("{danger}"), c.danger.name());
    result.replace(QStringLiteral("{selectionBg}"), c.selectionBg.name());
    result.replace(QStringLiteral("{selectionText}"), c.selectionText.name());
    result.replace(QStringLiteral("{iconMuted}"), c.iconMuted.name());
    result.replace(QStringLiteral("{hoverBg}"), c.hoverBg.name());
    result.replace(QStringLiteral("{pressedBg}"), c.pressedBg.name());
    result.replace(QStringLiteral("{disabledText}"), c.disabledText.name());
    result.replace(QStringLiteral("{disabledBg}"), c.disabledBg.name());
    result.replace(QStringLiteral("{disabledBorder}"), c.disabledBorder.name());
    result.replace(QStringLiteral("{scrollHandleHover}"),
                   c.scrollHandleHover.name());
    result.replace(QStringLiteral("{scrollHandle}"), c.scrollHandle.name());
    result.replace(QStringLiteral("{previewBg}"), c.previewBg.name());
    result.replace(QStringLiteral("{previewBorder}"), c.previewBorder.name());
    result.replace(QStringLiteral("{placeholder}"), c.placeholder.name());
    result.replace(QStringLiteral("{titleBar}"), c.titleBar.name());
    result.replace(QStringLiteral("{navBar}"), c.navBar.name());
    result.replace(QStringLiteral("{bottomBar}"), c.bottomBar.name());
    result.replace(QStringLiteral("{navActiveBg}"), c.navActiveBg.name());
    result.replace(QStringLiteral("{navHoverBg}"), c.navHoverBg.name());
    result.replace(QStringLiteral("{navActionHover}"), c.navActionHover.name());
    result.replace(QStringLiteral("{navActionPressed}"),
                   c.navActionPressed.name());
    result.replace(QStringLiteral("{tileSelectedBg}"), c.tileSelectedBg.name());
    result.replace(QStringLiteral("{tileSelectedBorder}"),
                   c.tileSelectedBorder.name());
    result.replace(QStringLiteral("{tileHoverBg}"), c.tileHoverBg.name());
    result.replace(QStringLiteral("{tileHoverBorder}"), c.tileHoverBorder.name());
    result.replace(QStringLiteral("{tileBg}"), c.tileBg.name());
    result.replace(QStringLiteral("{tileBorder}"), c.tileBorder.name());
    result.replace(QStringLiteral("{keyChipBg}"), c.keyChipBg.name());
    result.replace(QStringLiteral("{keyChipBorder}"), c.keyChipBorder.name());
    result.replace(QStringLiteral("{pathBoxBg}"), c.pathBoxBg.name());
    result.replace(QStringLiteral("{menuBg}"), c.menuBg.name());
    result.replace(QStringLiteral("{menuBorder}"), c.menuBorder.name());
    result.replace(QStringLiteral("{errorBadgeBg}"), c.errorBadgeBg.name());
    return result;
}
