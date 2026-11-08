#include "app/RegionSelector.h"

#include <QCloseEvent>
#include <QEventLoop>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <cmath>

namespace {

// Minimum accepted selection, in physical display pixels.
constexpr int kMinSize = 32;
constexpr qreal kHandleSize = 8.0;

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

    screen_ = FindScreen(display_rect);
    if (screen_) {
        // Qt geometry is in logical pixels; the native window then covers the
        // whole monitor, including when the display is scaled.
        setGeometry(screen_->geometry());
    } else {
        setGeometry(display_rect);
    }
}

bool RegionSelector::Select(QWidget* parent, const QRect& displayRect,
                            QRect& out) {
    if (displayRect.width() <= 0 || displayRect.height() <= 0) {
        return false;
    }
    RegionSelector selector(parent, displayRect);
    selector.show();
    selector.raise();
    selector.activateWindow();
    selector.setFocus(Qt::MouseFocusReason);

    QEventLoop loop;
    selector.loop_ = &loop;
    loop.exec();
    selector.loop_ = nullptr;

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

void RegionSelector::Confirm() {
    const QRect display(0, 0, display_rect_.width(), display_rect_.height());
    QRect selected = SelectionInDisplayPixels().intersected(display);
    // The encoder converts to NV12, whose textures need even dimensions. Drop
    // at most one pixel per axis so an arbitrary drag always records.
    selected.setWidth(selected.width() & ~1);
    selected.setHeight(selected.height() & ~1);
    if (selected.width() < kMinSize || selected.height() < kMinSize) {
        return;
    }
    result_rect_ = selected;
    accepted_ = true;
    if (loop_) {
        loop_->quit();
    }
}

void RegionSelector::Cancel() {
    accepted_ = false;
    if (loop_) {
        loop_->quit();
    }
}

void RegionSelector::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    activateWindow();
    setFocus(Qt::MouseFocusReason);
    QTimer::singleShot(0, this, [this] {
        activateWindow();
        setFocus(Qt::MouseFocusReason);
    });
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
        return;
    }
    // Pressing inside an existing selection keeps it, so a double-click can
    // confirm without the first click wiping it out.
    if (has_selection_ && selection_.contains(event->pos())) {
        dragging_ = false;
        return;
    }
    drag_origin_ = event->pos();
    selection_ = QRect(drag_origin_, drag_origin_);
    has_selection_ = false;
    dragging_ = true;
    update();
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event) {
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
    has_selection_ = selection_.width() > 0 && selection_.height() > 0;
    update();
}

void RegionSelector::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && has_selection_) {
        Confirm();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void RegionSelector::paintEvent(QPaintEvent* event) {
    (void)event;
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

    QFont label_font = font();
    label_font.setPixelSize(13);
    label_font.setWeight(QFont::DemiBold);
    painter.setFont(label_font);

    if (!has_selection_) {
        const QString hint =
            QStringLiteral("拖动选择录制区域 · Enter 确认 · Esc 取消");
        const QFontMetrics metrics(label_font);
        const int width = metrics.horizontalAdvance(hint) + 32;
        const QRectF pill((this->width() - width) / 2.0, 40.0, width, 40.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x1D, 0x21, 0x29, 220));
        painter.drawRoundedRect(pill, 8.0, 8.0);
        painter.setPen(QColor(Qt::white));
        painter.drawText(pill, Qt::AlignCenter, hint);
        return;
    }

    const QRectF border(QRectF(selection_).adjusted(1.0, 1.0, -1.0, -1.0));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0x2F, 0x6B, 0xFF), 2.0));
    painter.drawRect(border);

    // Corner handles.
    const QPointF corners[4] = {
        border.topLeft(), border.topRight(), border.bottomLeft(),
        border.bottomRight()};
    for (const QPointF& corner : corners) {
        const QRectF handle(corner.x() - kHandleSize / 2.0,
                            corner.y() - kHandleSize / 2.0, kHandleSize,
                            kHandleSize);
        painter.setPen(QPen(QColor(0x2F, 0x6B, 0xFF), 1.0));
        painter.setBrush(QColor(Qt::white));
        painter.drawRect(handle);
    }

    // Show the size that will actually be recorded (even dimensions).
    const QRect physical = SelectionInDisplayPixels();
    const QString size = QStringLiteral("%1 × %2")
                             .arg(physical.width() & ~1)
                             .arg(physical.height() & ~1);
    const QFontMetrics metrics(label_font);
    const qreal pill_width = metrics.horizontalAdvance(size) + 16.0;
    const qreal pill_height = 24.0;
    QPointF pill_pos(selection_.left(), selection_.top() - pill_height - 8.0);
    if (pill_pos.y() < 8.0) {
        pill_pos.setY(selection_.top() + 8.0);
    }
    if (pill_pos.x() + pill_width > this->width() - 8.0) {
        pill_pos.setX(this->width() - pill_width - 8.0);
    }
    const QRectF pill(pill_pos, QSizeF(pill_width, pill_height));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x1D, 0x21, 0x29, 220));
    painter.drawRoundedRect(pill, 6.0, 6.0);
    painter.setPen(QColor(Qt::white));
    painter.drawText(pill, Qt::AlignCenter, size);
}
