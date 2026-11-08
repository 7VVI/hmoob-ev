#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

class QTimer;

// 浅色 / 深色 / 跟随系统.
enum class ThemeMode { Light = 0, Dark = 1, System = 2 };

// Named color role for icons painted or cached as pixmaps. Widgets that hold a
// QPixmap regenerate it on Theme::changed.
enum class IconTone { Primary, Text, Secondary, Muted, Icon, IconDisabled };

// Every color used for application chrome. The light values reproduce the
// original design exactly; the dark values are the matching counterpart.
struct ThemeColors {
    QColor window;
    QColor card;
    QColor cardBorder;
    QColor border;
    QColor text;
    QColor textSecondary;
    QColor textMuted;
    QColor primary;
    QColor primaryHover;
    QColor primaryPressed;
    QColor primaryDisabled;
    QColor primaryDisabledText;
    QColor primarySoft;
    QColor inputBg;
    QColor inputBorder;
    QColor divider;
    QColor softBorder;
    QColor separator;
    QColor danger;
    QColor dangerHover;
    QColor dangerStrong;
    QColor selectionBg;
    QColor selectionText;
    QColor icon;
    QColor iconMuted;
    QColor iconDisabled;
    QColor hoverBg;
    QColor pressedBg;
    QColor disabledText;
    QColor disabledBg;
    QColor disabledBorder;
    QColor scrollHandle;
    QColor scrollHandleHover;
    QColor previewBg;
    QColor previewBorder;
    QColor placeholder;
    QColor titleBar;
    QColor navBar;
    QColor bottomBar;
    QColor navActiveBg;
    QColor navHoverBg;
    QColor navActionHover;
    QColor navActionPressed;
    QColor tileBg;
    QColor tileBorder;
    QColor tileHoverBg;
    QColor tileHoverBorder;
    QColor tileSelectedBg;
    QColor tileSelectedBorder;
    QColor keyChipBg;
    QColor keyChipBorder;
    QColor pathBoxBg;
    QColor menuBg;
    QColor menuBorder;
    QColor errorBadgeBg;
};

// Application-wide appearance state. One instance lives for the process; all
// widgets read colors from it and connect to changed() to repaint/rebuild.
class Theme : public QObject {
    Q_OBJECT

public:
    static Theme& Instance();

    [[nodiscard]] ThemeMode mode() const { return mode_; }
    void SetMode(ThemeMode mode);
    // Light or Dark, resolving System through the OS setting.
    [[nodiscard]] ThemeMode effectiveMode() const { return effective_; }
    [[nodiscard]] const ThemeColors& colors() const { return colors_; }
    [[nodiscard]] QPalette palette() const;
    [[nodiscard]] QColor ToneColor(IconTone tone) const;

    // Substitutes {window}, {text}, ... in a stylesheet template with the
    // current colors. Keeps the two stylesheets readable.
    [[nodiscard]] QString Expand(const QString& css) const;

    static QString ModeKey(ThemeMode mode);
    static ThemeMode ModeFromKey(const QString& key);

signals:
    void changed();

private:
    Theme();
    void Refresh();
    [[nodiscard]] bool SystemIsDark() const;

    ThemeMode mode_ = ThemeMode::Light;
    ThemeMode effective_ = ThemeMode::Light;
    ThemeColors colors_;
    QTimer* system_timer_ = nullptr;
};
