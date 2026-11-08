#pragma once

#include <QPoint>
#include <QRect>
#include <QWidget>

class QEventLoop;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QScreen;

// Fullscreen frameless overlay that lets the user drag a capture region on one
// display. The overlay covers the display's monitor rect; the returned rect is
// in display-local pixels, ready for RecorderConfig::region_*.
class RegionSelector : public QWidget {
    Q_OBJECT

public:
    // Shows the selector over `displayRect` (a monitor rect in physical desktop
    // pixels). Returns true with `out` set to the selection in display-local
    // pixels, or false when the user cancels.
    static bool Select(QWidget* parent, const QRect& displayRect, QRect& out);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    RegionSelector(QWidget* parent, const QRect& displayRect);

    qreal DevicePixelRatio() const;
    // The current selection converted to pixels relative to the display's
    // top-left corner (the coordinate space of RecorderConfig::region_*).
    QRect SelectionInDisplayPixels() const;
    void Confirm();
    void Cancel();

    const QScreen* screen_ = nullptr;
    QRect display_rect_;
    QRect selection_;
    QPoint drag_origin_;
    QRect result_rect_;
    bool dragging_ = false;
    bool has_selection_ = false;
    bool accepted_ = false;
    QEventLoop* loop_ = nullptr;
};
