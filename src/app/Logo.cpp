#include "app/Logo.h"

#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPolygonF>
#include <QScreen>
#include <QtMath>

namespace {

// Keep the ring visible when the mark is squeezed into 16 px.
constexpr qreal kRingFraction = 0.075;
constexpr qreal kMinRingStroke = 1.6;

QPixmap Render(int size, qreal dpr) {
    const int pixels = qMax(1, qRound(size * dpr));
    QPixmap pixmap(pixels, pixels);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setPen(Qt::NoPen);

    const qreal s = size;
    QLinearGradient gradient(0, 0, 0, s);
    gradient.setColorAt(0.0, QColor(0x2F, 0x6B, 0xFF));
    gradient.setColorAt(1.0, QColor(0x5F, 0xA8, 0xFF));
    painter.setBrush(gradient);
    const qreal radius = s * 0.22;
    painter.drawRoundedRect(QRectF(0, 0, s, s), radius, radius);

    // White ring. The pen is centred on the path, so the path radius is the
    // requested outer radius minus half the stroke.
    const qreal stroke = qMax(s * kRingFraction, kMinRingStroke);
    const qreal outer_radius = s * 0.37;
    const qreal path_radius = outer_radius - stroke / 2.0;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 235), stroke, Qt::SolidLine,
                        Qt::FlatCap, Qt::MiterJoin));
    painter.drawEllipse(QPointF(s / 2.0, s / 2.0), path_radius, path_radius);

    // Play triangle, nudged right so it reads as optically centred inside the
    // ring. At 16 px a slightly larger mark keeps the shape recognisable.
    const bool compact = size <= 16;
    const qreal width = s * (compact ? 0.30 : 0.26);
    const qreal height = s * (compact ? 0.34 : 0.30);
    const qreal center_x = s / 2.0 + s * 0.025;
    const qreal left = center_x - width / 2.0;
    const qreal right = center_x + width / 2.0;
    const qreal top = s / 2.0 - height / 2.0;
    const qreal bottom = s / 2.0 + height / 2.0;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255));
    painter.drawPolygon(QPolygonF{QPointF(left, top), QPointF(right, s / 2.0),
                                  QPointF(left, bottom)});
    return pixmap;
}

}  // namespace

namespace Logo {

QPixmap Pixmap(int size) {
    qreal dpr = 1.0;
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        dpr = qMax<qreal>(1.0, screen->devicePixelRatio());
    }
    return Render(size, dpr);
}

QIcon Icon() {
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addPixmap(Render(size, 1.0));
    }
    return icon;
}

}  // namespace Logo
