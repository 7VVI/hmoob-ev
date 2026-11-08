#include "app/RegionSelector.h"

#include <QCloseEvent>
#include <QEventLoop>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <cmath>

#include "app/Theme.h"
#include "app/UiFont.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

// Minimum accepted selection, in physical display pixels.
constexpr int kMinSize = 32;
constexpr qreal kHandleSize = 8.0;

// Toolbar metrics (logical pixels).
constexpr int kToolbarPadding = 8;
constexpr int kToolbarGap = 8;
constexpr int kButtonHeight = 36;
constexpr int kConfirmWidth = 132;
constexpr int kCancelWidth = 80;
constexpr int kScreenMargin = 8;

QRect PhysicalGeometry(const QScreen* screen) {
    const qreal dpr = screen->devicePixelRatio();
    const QRect geometry = screen->geometry();
    return QRect(qRound(geometry.x() * dpr), qRound(geometry.y() * dpr),
                 qRound(geometry.width() * dpr),
                 qRound(geometry.height() * dpr));
}

const QScreen* FindScreen(const QRect& display_rect) {
    const QScreen* best = nullptr;
    qint64 best_area = 0;
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (const QScreen* screen : screens) {
        const QRect overlap = PhysicalGeometry(screen).intersected(display_rect);
        const qint64 area =
            static_cast<qint64>(overlap.width()) * overlap.height();
        if (area > best_area) {
            best_area = area;
            best = screen;
        }
    }
    return best ? best : QGuiApplication::primaryScreen();
}

}  // namespace

RegionSelector::RegionSelector(QWidget* parent, const QRect& display_rect)
    : QWidget(nullptr), display_rect_(display_rect) {
    (void)parent;
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setObjectName(QStringLiteral("regionSelector"));

    screen_ = FindScreen(display_rect);
    if (screen_) {
        // Qt geometry is in logical pixels; the native window then covers the
        // whole monitor, including when the display is scaled.
        setGeometry(screen_->geometry());
    } else {
        setGeometry(display_rect);
    }

    // Mouse-only confirmation. These are real buttons, so a click does not
    // depend on the overlay ever receiving keyboard focus.
    confirm_button_ = new QPushButton(QStringLiteral("✓ 确认录制"), this);
    confirm_button_->setObjectName(QStringLiteral("regionConfirmButton"));
    confirm_button_->setCursor(Qt::PointingHandCursor);
    confirm_button_->setFocusPolicy(Qt::NoFocus);
    confirm_button_->setFixedSize(kConfirmWidth, kButtonHeight);
    confirm_button_->hide();
    connect(confirm_button_, &QPushButton::clicked, this,
            &RegionSelector::Confirm);

    cancel_button_ = new QPushButton(QStringLiteral("取消"), this);
    cancel_button_->setObjectName(QStringLiteral("regionCancelButton"));
    cancel_button_->setCursor(Qt::PointingHandCursor);
    cancel_button_->setFocusPolicy(Qt::NoFocus);
    cancel_button_->setFixedSize(kCancelWidth, kButtonHeight);
    cancel_button_->hide();
    connect(cancel_button_, &QPushButton::clicked, this,
            &RegionSelector::Cancel);

    confirm_button_->setFont(UiFont(14, QFont::DemiBold));
    cancel_button_->setFont(UiFont(14));
    ApplyButtonStyles();
    connect(&Theme::Instance(), &Theme::changed, this, [this] {
        ApplyButtonStyles();
        update();
    });
}

bool RegionSelector::Select(QWidget* parent, const QRect& displayRect,
                            QRect& out) {
    if (displayRect.width() <= 0 || displayRect.height() <= 0) {
        return false;
    }
    RegionSelector selector(parent, displayRect);
    QEventLoop loop;
    selector.loop_ = &loop;
    selector.show();
    selector.raise();
    selector.ActivateOverlay();

    loop.exec();
    selector.loop_ = nullptr;
    // The overlay must be gone before the caller starts recording; otherwise
    // the mask lingers for a frame or longer over the freshly captured screen.
    selector.hide();

    if (selector.accepted_) {
        out = selector.result_rect_;
    }
    // Never keep keyboard focus on a window that is about to disappear.
    if (parent) {
        parent->activateWindow();
        parent->raise();
    }
    return selector.accepted_;
}

qreal RegionSelector::DevicePixelRatio() const {
    return screen_ ? screen_->devicePixelRatio() : 1.0;
}

QRect RegionSelector::SelectionInDisplayPixels() const {
    if (selection_.width() <= 0 || selection_.height() <= 0) {
        return QRect();
    }
    const qreal dpr = DevicePixelRatio();
    // Physical origin of the widget's screen, then translated into the target
    // display's own pixel space.
    const QPointF screen_origin =
        screen_ ? QPointF(screen_->geometry().topLeft()) * dpr : QPointF(0, 0);
    const QPointF offset = screen_origin - QPointF(display_rect_.topLeft());
    const int left = qRound(offset.x() + selection_.left() * dpr);
    const int top = qRound(offset.y() + selection_.top() * dpr);
    const int right =
        qRound(offset.x() + (selection_.left() + selection_.width()) * dpr);
    const int bottom =
        qRound(offset.y() + (selection_.top() + selection_.height()) * dpr);
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
}

QRect RegionSelector::NormalizedSelection() const {
    const QRect display(0, 0, display_rect_.width(), display_rect_.height());
    QRect selected = SelectionInDisplayPixels().intersected(display);
    // The encoder converts to NV12, whose textures need even dimensions. Drop
    // at most one pixel per axis so an arbitrary drag always records.
    selected.setWidth(selected.width() & ~1);
    selected.setHeight(selected.height() & ~1);
    return selected;
}

bool RegionSelector::HasUsableSelection() const {
    const QRect selected = NormalizedSelection();
    return selected.width() >= kMinSize && selected.height() >= kMinSize;
}

void RegionSelector::Confirm() {
    const QRect selected = NormalizedSelection();
    if (selected.width() < kMinSize || selected.height() < kMinSize) {
        return;
    }
    result_rect_ = selected;
    accepted_ = true;
    dragging_ = false;
    hide();
    if (loop_) {
        loop_->quit();
    }
}

void RegionSelector::Cancel() {
    accepted_ = false;
    dragging_ = false;
    hide();
    if (loop_) {
        loop_->quit();
    }
}

void RegionSelector::ActivateOverlay() {
    raise();
    activateWindow();
    setFocus(Qt::MouseFocusReason);
#ifdef Q_OS_WIN
    // Best effort only: Windows may refuse a foreground change from a
    // background process. Mouse confirmation works either way.
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));
#endif
}

void RegionSelector::ApplyButtonStyles() {
    const Theme& theme = Theme::Instance();
    confirm_button_->setStyleSheet(theme.Expand(QStringLiteral(
        "QPushButton {"
        " background: {primary}; color: #FFFFFF; border: none;"
        " border-radius: 9px; font-size: 14px; font-weight: 600;"
        " padding: 0 14px; }"
        "QPushButton:hover { background: {primaryHover}; }"
        "QPushButton:pressed { background: {primaryPressed}; }")));
    cancel_button_->setStyleSheet(theme.Expand(QStringLiteral(
        "QPushButton {"
        " background: {pathBoxBg}; color: {text};"
        " border: 1px solid {inputBorder}; border-radius: 9px;"
        " font-size: 14px; }"
        "QPushButton:hover { background: {hoverBg}; }"
        "QPushButton:pressed { background: {pressedBg}; }")));
}

void RegionSelector::StartDrag(const QPoint& pos) {
    settled_ = false;
    dragging_ = true;
    has_selection_ = false;
    drag_origin_ = pos;
    cursor_pos_ = pos;
    selection_ = QRect(pos, pos);
    UpdateToolbar();
}

void RegionSelector::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ActivateOverlay();
    QTimer::singleShot(0, this, [this] { ActivateOverlay(); });
}

void RegionSelector::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    // Any hide path (confirm, cancel, window manager) must release the caller.
    if (loop_) {
        loop_->quit();
    }
}

void RegionSelector::closeEvent(QCloseEvent* event) {
    // Alt+F4 and any other close path cancel the selection instead of leaving
    // the caller waiting in the event loop.
    accepted_ = false;
    if (loop_) {
        loop_->quit();
    }
    event->accept();
}

void RegionSelector::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        Cancel();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        Confirm();
        return;
    }
    QWidget::keyPressEvent(event);
}

void RegionSelector::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    // Pressing inside a settled selection must not wipe it out and restart the
    // drag: that is what made the overlay feel like it never finished. A
    // double-click here confirms instead.
    if (settled_ && selection_.contains(event->pos())) {
        dragging_ = false;
        return;
    }
    StartDrag(event->pos());
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event) {
    cursor_pos_ = event->pos();
    if (!dragging_) {
        return;
    }
    // Exclusive edges: dragging from x=900 to x=1500 selects 600 pixels, not
    // 601. QRect(QPoint, QPoint) treats both points as inclusive corners.
    const QPoint pos = event->pos();
    const int left = qMin(drag_origin_.x(), pos.x());
    const int top = qMin(drag_origin_.y(), pos.y());
    const int right = qMax(drag_origin_.x(), pos.x());
    const int bottom = qMax(drag_origin_.y(), pos.y());
    selection_ = QRect(QPoint(left, top), QSize(right - left, bottom - top))
                     .intersected(rect());
    has_selection_ = selection_.width() > 0 && selection_.height() > 0;
    update();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !dragging_) {
        return;
    }
    dragging_ = false;
    cursor_pos_ = event->pos();
    has_selection_ = selection_.width() > 0 && selection_.height() > 0;
    // A too-small drag (or a stray click) goes back to the empty state, where
    // the next press starts a fresh drag.
    settled_ = has_selection_ && HasUsableSelection();
    if (!settled_) {
        has_selection_ = false;
        selection_ = QRect();
    }
    UpdateToolbar();
}

void RegionSelector::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && settled_ &&
        selection_.contains(event->pos())) {
        Confirm();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void RegionSelector::UpdateToolbar() {
    const bool show = settled_ && has_selection_;
    confirm_button_->setVisible(show);
    cancel_button_->setVisible(show);
    if (show) {
        PositionToolbar();
        // Children are painted above the mask and the painted toolbar card.
        confirm_button_->raise();
        cancel_button_->raise();
    } else {
        toolbar_rect_ = QRect();
    }
    update();
}

void RegionSelector::PositionToolbar() {
    const int width = kToolbarPadding * 2 + kConfirmWidth + kToolbarGap +
                      kCancelWidth;
    const int height = kToolbarPadding * 2 + kButtonHeight;

    int x = selection_.left();
    if (x + width > this->width() - kScreenMargin) {
        x = this->width() - width - kScreenMargin;
    }
    x = qMax(kScreenMargin, x);

    // Prefer under the selection; flip above when the bottom is too close.
    int y = selection_.bottom() + 1 + 10;
    if (y + height > this->height() - kScreenMargin) {
        y = selection_.top() - height - 10;
    }
    y = qBound(kScreenMargin, y, this->height() - height - kScreenMargin);

    toolbar_rect_ = QRect(x, y, width, height);
    confirm_button_->move(x + kToolbarPadding, y + kToolbarPadding);
    cancel_button_->move(x + kToolbarPadding + kConfirmWidth + kToolbarGap,
                         y + kToolbarPadding);
}

void RegionSelector::DrawHintPill(QPainter& painter) {
    const QString hint =
        QStringLiteral("拖拽选择录制区域 · 双击或点击“确认”开始 · Esc 取消");
    const QFont label_font = UiFont(13, QFont::DemiBold);
    const QFontMetrics metrics(label_font);
    const int width = metrics.horizontalAdvance(hint) + 32;
    const QRectF pill((this->width() - width) / 2.0, 24.0, width, 40.0);

    const ThemeColors& colors = Theme::Instance().colors();
    painter.setFont(label_font);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(colors.menuBg.red(), colors.menuBg.green(),
                            colors.menuBg.blue(), 230));
    painter.drawRoundedRect(pill, 8.0, 8.0);
    painter.setPen(colors.menuBg.lightness() > 128 ? colors.text
                                                   : QColor(Qt::white));
    painter.drawText(pill, Qt::AlignCenter, hint);
}

void RegionSelector::DrawSizePill(QPainter& painter, const QPoint& anchor,
                                  bool near_cursor) {
    const QRect physical = NormalizedSelection();
    if (physical.width() <= 0 || physical.height() <= 0) {
        return;
    }
    const QString size = QStringLiteral("%1 × %2")
                             .arg(physical.width())
                             .arg(physical.height());
    const QFont label_font = UiFont(13, QFont::DemiBold);
    const QFontMetrics metrics(label_font);
    const qreal pill_width = metrics.horizontalAdvance(size) + 16.0;
    const qreal pill_height = 24.0;

    QPointF pos;
    if (near_cursor) {
        // Live W×H next to the pointer while dragging.
        pos = QPointF(anchor.x() + 14.0, anchor.y() + 14.0);
    } else {
        pos = QPointF(anchor.x(), anchor.y() - pill_height - 8.0);
        if (pos.y() < kScreenMargin) {
            pos.setY(anchor.y() + 8.0);
        }
        const QRectF desired(pos, QSizeF(pill_width, pill_height));
        if (!toolbar_rect_.isNull() && desired.intersects(toolbar_rect_)) {
            pos.setY(anchor.y() + 8.0);
        }
    }
    pos.setX(qBound<qreal>(kScreenMargin, pos.x(),
                           this->width() - pill_width - kScreenMargin));
    pos.setY(qBound<qreal>(kScreenMargin, pos.y(),
                           this->height() - pill_height - kScreenMargin));

    const QRectF pill(pos, QSizeF(pill_width, pill_height));
    const ThemeColors& colors = Theme::Instance().colors();
    painter.setFont(label_font);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(colors.menuBg.red(), colors.menuBg.green(),
                            colors.menuBg.blue(), 230));
    painter.drawRoundedRect(pill, 6.0, 6.0);
    painter.setPen(colors.menuBg.lightness() > 128 ? colors.text
                                                   : QColor(Qt::white));
    painter.drawText(pill, Qt::AlignCenter, size);
}

void RegionSelector::paintEvent(QPaintEvent* event) {
    (void)event;
    const ThemeColors& colors = Theme::Instance().colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath full;
    full.addRect(QRectF(rect()));
    if (has_selection_) {
        QPainterPath hole;
        hole.addRect(QRectF(selection_));
        painter.fillPath(full.subtracted(hole), QColor(0, 0, 0, 120));
    } else {
        painter.fillPath(full, QColor(0, 0, 0, 120));
    }

    if (has_selection_) {
        const QRectF border(QRectF(selection_).adjusted(1.0, 1.0, -1.0, -1.0));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(colors.primary, 2.0));
        painter.drawRect(border);

        // Corner handles.
        const QPointF corners[4] = {
            border.topLeft(), border.topRight(), border.bottomLeft(),
            border.bottomRight()};
        for (const QPointF& corner : corners) {
            const QRectF handle(corner.x() - kHandleSize / 2.0,
                                corner.y() - kHandleSize / 2.0, kHandleSize,
                                kHandleSize);
            painter.setPen(QPen(colors.primary, 1.0));
            painter.setBrush(QColor(Qt::white));
            painter.drawRect(handle);
        }

        if (dragging_) {
            DrawSizePill(painter, cursor_pos_, true);
        } else {
            DrawSizePill(painter, selection_.topLeft(), false);
        }
    }

    if (settled_ && !toolbar_rect_.isNull()) {
        painter.setPen(QPen(colors.menuBorder, 1.0));
        painter.setBrush(colors.menuBg);
        painter.drawRoundedRect(QRectF(toolbar_rect_), 11.0, 11.0);
    }

    // The hint is always visible; confirmation must not depend on the user
    // remembering a keyboard shortcut of a window that may not be focused.
    DrawHintPill(painter);
}
