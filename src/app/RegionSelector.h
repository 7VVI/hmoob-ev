#pragma once

#include <QPoint>
#include <QRect>
#include <QWidget>

class QEventLoop;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QPushButton;
class QScreen;

// Fullscreen frameless overlay that lets the user drag a capture region on one
// display. The overlay covers the display's monitor rect; the returned rect is
// in display-local pixels, ready for RecorderConfig::region_*.
//
// Once a drag settles on a usable selection a small toolbar with real
// QPushButtons appears next to it, so the selection can be confirmed with the
// mouse alone. The overlay is a Qt::Tool window and often does not receive
// keyboard focus on Windows, therefore Enter/Esc are conveniences only and
// never the sole way to finish.
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
    void hideEvent(QHideEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    RegionSelector(QWidget* parent, const QRect& displayRect);

    qreal DevicePixelRatio() const;
    // The current selection converted to pixels relative to the display's
    // top-left corner (the coordinate space of RecorderConfig::region_*).
    QRect SelectionInDisplayPixels() const;
    // SelectionInDisplayPixels() clipped to the display and with the even
    // dimensions the encoder requires. This is the rectangle a confirmation
    // would record.
    QRect NormalizedSelection() const;
    bool HasUsableSelection() const;

    void Confirm();
    void Cancel();
    // Best-effort foreground activation. Every interaction path works without
    // it, this only restores keyboard shortcuts when Windows allows it.
    void ActivateOverlay();

    void ApplyButtonStyles();
    void UpdateToolbar();
    void PositionToolbar();
    void StartDrag(const QPoint& pos);
    void DrawHintPill(QPainter& painter);
    void DrawSizePill(QPainter& painter, const QPoint& anchor,
                      bool below_anchor);

    const QScreen* screen_ = nullptr;
    QRect display_rect_;
    QRect selection_;
    QPoint drag_origin_;
    QPoint cursor_pos_;
    QRect result_rect_;
    bool dragging_ = false;
    // A non-empty rectangle exists (during the drag and after release).
    bool has_selection_ = false;
    // The drag has finished and the selection is big enough to record; the
    // confirm/cancel toolbar is shown and presses inside the rectangle no
    // longer start a new drag.
    bool settled_ = false;
    bool accepted_ = false;
    QEventLoop* loop_ = nullptr;

    QPushButton* confirm_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    QRect toolbar_rect_;
};
