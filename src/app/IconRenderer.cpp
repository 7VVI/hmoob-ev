#include "app/IconRenderer.h"

#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QScreen>
#include <QtMath>

namespace {

void PaintPlay(QPainter& p, const QColor& color) {
    // Filled play mark with a smaller play-shaped cut-out, matching the app
    // logo in the reference rather than a solid triangle.
    QPainterPath triangle;
    triangle.moveTo(7.1, 3.2);
    triangle.lineTo(20.4, 12.0);
    triangle.lineTo(7.1, 20.8);
    triangle.closeSubpath();

    QPainterPathStroker stroker;
    stroker.setWidth(2.2);
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    QPainterPath body = stroker.createStroke(triangle).united(triangle);

    QPainterPath cutout;
    cutout.moveTo(11.7, 8.9);
    cutout.lineTo(16.0, 12.0);
    cutout.lineTo(11.7, 15.1);
    cutout.closeSubpath();
    body = body.subtracted(cutout);

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(body);
}

void PaintMonitor(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(3.2, 5.0, 17.6, 12.0), 1.8, 1.8);
    p.drawLine(QPointF(12.0, 17.0), QPointF(12.0, 20.0));
    p.drawLine(QPointF(8.2, 20.0), QPointF(15.8, 20.0));
}

void PaintWindow(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(3.6, 4.6, 16.8, 14.8), 2.2, 2.2);
    p.drawLine(QPointF(3.6, 9.1), QPointF(20.4, 9.1));
}

void PaintRegion(QPainter& p, const QColor& color, qreal stroke) {
    QPen pen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QVector<qreal> dashes;
    dashes << 3.0 << 2.6;
    pen.setDashPattern(dashes);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(4.0, 4.6, 16.0, 14.8), 2.0, 2.0);
}

void PaintCamera(QPainter& p, const QColor& color, qreal stroke) {
    // Camcorder: rounded body with a lens, a small top bump and a D-shaped
    // grip on the right (the reference's 摄像头 tab icon).
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.6, 5.8, 13.4, 12.8), 2.4, 2.4);
    p.drawEllipse(QPointF(9.3, 12.2), 3.1, 3.1);
    QPainterPath bump;
    bump.moveTo(7.8, 5.8);
    bump.lineTo(8.8, 4.0);
    bump.quadTo(9.2, 3.4, 9.9, 3.4);
    bump.lineTo(12.0, 3.4);
    bump.quadTo(12.7, 3.4, 13.1, 4.0);
    bump.lineTo(14.1, 5.8);
    p.drawPath(bump);
    p.drawLine(QPointF(16.0, 9.6), QPointF(17.4, 9.6));
    p.drawLine(QPointF(16.0, 14.8), QPointF(17.4, 14.8));
    p.drawRoundedRect(QRectF(17.4, 8.6, 4.0, 7.2), 1.6, 1.6);
}

void PaintWebcam(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(12.0, 11.0), 7.2, 7.2);
    p.drawEllipse(QPointF(12.4, 11.8), 2.9, 2.9);
    p.drawArc(QRectF(7.4, 8.6, 9.2, 9.2), 205 * 16, 130 * 16);
}

void PaintGear(QPainter& p, const QColor& color, qreal stroke) {
    auto polar = [](qreal radius, qreal degrees) {
        const qreal angle = qDegreesToRadians(degrees - 90.0);
        return QPointF(12.0 + radius * qCos(angle),
                       12.0 + radius * qSin(angle));
    };
    QPainterPath path;
    constexpr qreal inner_radius = 7.0;
    constexpr qreal outer_radius = 9.6;
    for (int i = 0; i < 8; ++i) {
        const qreal center = i * 45.0;
        const QPointF points[4] = {
            polar(inner_radius, center - 21.0),
            polar(outer_radius, center - 11.0),
            polar(outer_radius, center + 11.0),
            polar(inner_radius, center + 21.0),
        };
        if (i == 0) {
            path.moveTo(points[0]);
        } else {
            path.lineTo(points[0]);
        }
        path.lineTo(points[1]);
        path.lineTo(points[2]);
        path.lineTo(points[3]);
    }
    path.closeSubpath();
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.drawEllipse(QPointF(12.0, 12.0), 2.6, 2.6);
}

void PaintFolder(QPainter& p, const QColor& color, qreal stroke) {
    // Open folder: back panel with a tab, plus the angled front panel.
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    QPainterPath back;
    back.moveTo(3.0, 10.2);
    back.lineTo(3.0, 6.0);
    back.quadTo(3.0, 4.6, 4.4, 4.6);
    back.lineTo(8.6, 4.6);
    back.lineTo(10.6, 6.8);
    back.lineTo(19.0, 6.8);
    back.quadTo(20.9, 6.8, 20.9, 8.7);
    back.lineTo(20.9, 10.2);
    p.drawPath(back);

    QPainterPath front;
    front.moveTo(3.0, 10.2);
    front.lineTo(20.9, 10.2);
    front.lineTo(19.5, 18.5);
    front.quadTo(19.2, 19.9, 17.8, 19.9);
    front.lineTo(5.2, 19.9);
    front.quadTo(3.8, 19.9, 3.5, 18.5);
    front.closeSubpath();
    p.drawPath(front);
}

void PaintMenu(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, qMax<qreal>(stroke, 1.8), Qt::SolidLine,
                  Qt::RoundCap));
    for (const qreal y : {6.6, 12.0, 17.4}) {
        p.drawLine(QPointF(4.0, y), QPointF(20.0, y));
    }
}

void PaintMic(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(8.8, 3.0, 6.4, 11.0), 3.2, 3.2);
    p.drawArc(QRectF(5.8, 6.6, 12.4, 11.2), 180 * 16, 180 * 16);
    p.drawLine(QPointF(12.0, 17.8), QPointF(12.0, 20.4));
    p.drawLine(QPointF(8.6, 20.4), QPointF(15.4, 20.4));
}

void PaintSpeaker(QPainter& p, const QColor& color, qreal stroke, bool muted) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath body;
    body.moveTo(3.2, 9.4);
    body.lineTo(6.8, 9.4);
    body.lineTo(11.0, 5.4);
    body.lineTo(11.0, 18.6);
    body.lineTo(6.8, 14.6);
    body.lineTo(3.2, 14.6);
    body.closeSubpath();
    p.drawPath(body);
    if (!muted) {
        p.drawArc(QRectF(10.4, 8.2, 6.4, 7.6), -58 * 16, 116 * 16);
        p.drawArc(QRectF(10.2, 5.4, 10.6, 13.2), -55 * 16, 110 * 16);
    } else {
        p.drawLine(QPointF(15.4, 9.4), QPointF(20.4, 14.6));
        p.drawLine(QPointF(20.4, 9.4), QPointF(15.4, 14.6));
    }
}

void PaintHd(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, qMax<qreal>(stroke, 1.5), Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.6, 4.8, 18.8, 14.4), 3.2, 3.2);
    QFont font = p.font();
    font.setPixelSize(7);
    font.setBold(true);
    font.setFamilies({QStringLiteral("Microsoft YaHei UI"),
                      QStringLiteral("Arial")});
    p.setFont(font);
    p.drawText(QRectF(2.6, 4.2, 18.8, 15.6), Qt::AlignCenter,
               QStringLiteral("HD"));
}

void PaintFilm(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(3.4, 4.4, 17.2, 15.2), 2.0, 2.0);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 3; ++i) {
        const qreal y = 6.1 + i * 4.3;
        p.drawRect(QRectF(4.9, y, 1.7, 1.9));
        p.drawRect(QRectF(17.4, y, 1.7, 1.9));
    }
    p.setPen(QPen(color, qMax<qreal>(stroke, 1.4), Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(QPointF(7.9, 4.4), QPointF(7.9, 19.6));
    p.drawLine(QPointF(16.1, 4.4), QPointF(16.1, 19.6));
}

void PaintInfo(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(12.0, 12.0), 8.6, 8.6);
    p.setPen(QPen(color, qMax<qreal>(stroke, 1.8), Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(QPointF(12.0, 7.4), QPointF(12.0, 8.2));
    p.drawLine(QPointF(12.0, 11.0), QPointF(12.0, 16.6));
}

void PaintClock(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(12.0, 12.0), 8.4, 8.4);
    p.drawLine(QPointF(12.0, 7.2), QPointF(12.0, 12.2));
    p.drawLine(QPointF(12.0, 12.2), QPointF(15.6, 14.4));
}

void PaintFile(QPainter& p, const QColor& color, qreal stroke) {
    QPainterPath path;
    path.moveTo(6.0, 3.4);
    path.lineTo(13.8, 3.4);
    path.lineTo(19.0, 8.6);
    path.lineTo(19.0, 20.6);
    path.lineTo(6.0, 20.6);
    path.closeSubpath();
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.drawLine(QPointF(13.8, 3.4), QPointF(13.8, 8.6));
    p.drawLine(QPointF(13.8, 8.6), QPointF(19.0, 8.6));
}

void PaintCheck(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, qMax<qreal>(stroke, 2.2), Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(5.0, 12.4);
    path.lineTo(9.6, 17.0);
    path.lineTo(19.0, 7.0);
    p.drawPath(path);
}

void PaintKeyboard(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.8, 6.2, 18.4, 11.8), 2.2, 2.2);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (const qreal y : {9.4, 12.2}) {
        for (const qreal x : {5.6, 8.8, 12.0, 15.2, 18.4}) {
            p.drawRoundedRect(QRectF(x - 0.8, y - 0.7, 1.6, 1.4), 0.4, 0.4);
        }
    }
    p.setPen(QPen(color, qMax<qreal>(stroke, 1.6), Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(QPointF(8.6, 15.2), QPointF(15.4, 15.2));
}

void PaintSun(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(12.0, 12.0), 4.3, 4.3);
    for (int i = 0; i < 8; ++i) {
        const qreal angle = qDegreesToRadians(i * 45.0);
        const qreal dx = qCos(angle);
        const qreal dy = qSin(angle);
        p.drawLine(QPointF(12.0 + dx * 7.0, 12.0 + dy * 7.0),
                   QPointF(12.0 + dx * 9.4, 12.0 + dy * 9.4));
    }
}

void PaintMoon(QPainter& p, const QColor& color, qreal stroke) {
    QPainterPath full;
    full.addEllipse(QPointF(12.0, 12.0), 7.8, 7.8);
    QPainterPath cut;
    cut.addEllipse(QPointF(16.6, 8.2), 7.0, 7.0);
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(full.subtracted(cut));
}

void PaintSearch(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(10.8, 10.8), 5.8, 5.8);
    p.drawLine(QPointF(15.2, 15.2), QPointF(20.2, 20.2));
}

void PaintGrid(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(3.6, 3.6, 7.0, 7.0), 1.6, 1.6);
    p.drawRoundedRect(QRectF(13.4, 3.6, 7.0, 7.0), 1.6, 1.6);
    p.drawRoundedRect(QRectF(3.6, 13.4, 7.0, 7.0), 1.6, 1.6);
    p.drawRoundedRect(QRectF(13.4, 13.4, 7.0, 7.0), 1.6, 1.6);
}

void PaintListView(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, qMax<qreal>(stroke, 1.6), Qt::SolidLine,
                  Qt::RoundCap));
    for (const qreal y : {6.6, 12.0, 17.4}) {
        p.drawLine(QPointF(9.4, y), QPointF(20.2, y));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (const qreal y : {6.6, 12.0, 17.4}) {
        p.drawRoundedRect(QRectF(3.6, y - 1.2, 2.4, 2.4), 0.7, 0.7);
    }
}

void PaintRefresh(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    // Three-quarter circle with a tangential arrowhead, so the direction of
    // the refresh reads at 16 px.
    p.drawArc(QRectF(4.2, 4.2, 15.6, 15.6), 40 * 16, 280 * 16);
    const qreal angle = qDegreesToRadians(320.0);
    const QPointF end(12.0 + 7.8 * qCos(angle), 12.0 - 7.8 * qSin(angle));
    const QPointF tangent(-qSin(angle), -qCos(angle));
    const QPointF radial(qCos(angle), -qSin(angle));
    const QPointF back = end - tangent * 4.0;
    p.drawPolyline(QPolygonF{back + radial * 2.6, end, back - radial * 2.6});
}

void PaintEdit(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath body;
    body.moveTo(4.0, 20.0);
    body.lineTo(4.9, 16.2);
    body.lineTo(16.8, 4.3);
    body.quadTo(17.9, 3.2, 19.0, 4.3);
    body.lineTo(19.7, 5.0);
    body.quadTo(20.8, 6.1, 19.7, 7.2);
    body.lineTo(7.8, 19.1);
    body.closeSubpath();
    p.drawPath(body);
    p.drawLine(QPointF(15.4, 5.7), QPointF(18.3, 8.6));
}

void PaintTrash(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(3.8, 7.0), QPointF(20.2, 7.0));
    p.drawLine(QPointF(9.2, 4.2), QPointF(9.2, 7.0));
    p.drawLine(QPointF(14.8, 4.2), QPointF(14.8, 7.0));
    QPainterPath body;
    body.moveTo(5.8, 9.2);
    body.lineTo(7.0, 20.0);
    body.lineTo(17.0, 20.0);
    body.lineTo(18.2, 9.2);
    p.drawPath(body);
    p.drawLine(QPointF(10.2, 12.2), QPointF(10.6, 17.2));
    p.drawLine(QPointF(13.8, 12.2), QPointF(13.4, 17.2));
}

void PaintCalendar(QPainter& p, const QColor& color, qreal stroke) {
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(3.6, 5.4, 16.8, 14.6), 2.0, 2.0);
    p.drawLine(QPointF(3.6, 10.0), QPointF(20.4, 10.0));
    p.drawLine(QPointF(8.2, 3.4), QPointF(8.2, 6.6));
    p.drawLine(QPointF(15.8, 3.4), QPointF(15.8, 6.6));
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (const qreal y : {12.4, 15.8}) {
        for (const qreal x : {7.2, 11.4, 15.6}) {
            p.drawEllipse(QPointF(x, y), 0.9, 0.9);
        }
    }
}

}  // namespace

namespace IconRenderer {

void Paint(QPainter& painter, Icon icon, const QRectF& rect,
           const QColor& color, qreal stroke) {
    if (rect.isEmpty() || !color.isValid()) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal scale =
        qMin(rect.width(), rect.height()) / 24.0;
    painter.translate(rect.left() + (rect.width() - 24.0 * scale) / 2.0,
                      rect.top() + (rect.height() - 24.0 * scale) / 2.0);
    painter.scale(scale, scale);

    switch (icon) {
        case Icon::Play:
            PaintPlay(painter, color);
            break;
        case Icon::Monitor:
            PaintMonitor(painter, color, stroke);
            break;
        case Icon::Region:
            PaintRegion(painter, color, stroke);
            break;
        case Icon::Window:
            PaintWindow(painter, color, stroke);
            break;
        case Icon::Camera:
            PaintCamera(painter, color, stroke);
            break;
        case Icon::Webcam:
            PaintWebcam(painter, color, stroke);
            break;
        case Icon::Gear:
            PaintGear(painter, color, stroke);
            break;
        case Icon::Folder:
            PaintFolder(painter, color, stroke);
            break;
        case Icon::Menu:
            PaintMenu(painter, color, stroke);
            break;
        case Icon::Mic:
            PaintMic(painter, color, stroke);
            break;
        case Icon::Speaker:
            PaintSpeaker(painter, color, stroke, false);
            break;
        case Icon::SpeakerMuted:
            PaintSpeaker(painter, color, stroke, true);
            break;
        case Icon::Hd:
            PaintHd(painter, color, stroke);
            break;
        case Icon::Film:
            PaintFilm(painter, color, stroke);
            break;
        case Icon::Info:
            PaintInfo(painter, color, stroke);
            break;
        case Icon::Clock:
            PaintClock(painter, color, stroke);
            break;
        case Icon::File:
            PaintFile(painter, color, stroke);
            break;
        case Icon::Minimize:
            painter.setPen(QPen(color, qMax<qreal>(stroke, 1.5),
                                Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(5.0, 12.0), QPointF(19.0, 12.0));
            break;
        case Icon::Maximize:
            painter.setPen(QPen(color, qMax<qreal>(stroke, 1.5),
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(5.4, 5.4, 13.2, 13.2), 1.4, 1.4);
            break;
        case Icon::Restore: {
            painter.setPen(QPen(color, qMax<qreal>(stroke, 1.5),
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(8.2, 4.2, 11.4, 11.4), 1.2, 1.2);
            painter.drawRoundedRect(QRectF(4.4, 8.0, 11.4, 11.4), 1.2, 1.2);
            break;
        }
        case Icon::Close:
            painter.setPen(QPen(color, qMax<qreal>(stroke, 1.5),
                                Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(5.6, 5.6), QPointF(18.4, 18.4));
            painter.drawLine(QPointF(18.4, 5.6), QPointF(5.6, 18.4));
            break;
        case Icon::Check:
            PaintCheck(painter, color, stroke);
            break;
        case Icon::ChevronDown:
            painter.setPen(QPen(color, qMax<qreal>(stroke, 1.7),
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(QPolygonF{QPointF(7.0, 9.5),
                                           QPointF(12.0, 14.5),
                                           QPointF(17.0, 9.5)});
            break;
        case Icon::ChevronUp:
            painter.setPen(QPen(color, qMax<qreal>(stroke, 1.7),
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(QPolygonF{QPointF(7.0, 14.5),
                                           QPointF(12.0, 9.5),
                                           QPointF(17.0, 14.5)});
            break;
        case Icon::RecordDot:
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(12.0, 12.0), 6.6, 6.6);
            break;
        case Icon::StopSquare:
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawRoundedRect(QRectF(6.0, 6.0, 12.0, 12.0), 2.4, 2.4);
            break;
        case Icon::Keyboard:
            PaintKeyboard(painter, color, stroke);
            break;
        case Icon::Sun:
            PaintSun(painter, color, stroke);
            break;
        case Icon::Moon:
            PaintMoon(painter, color, stroke);
            break;
        case Icon::Search:
            PaintSearch(painter, color, stroke);
            break;
        case Icon::Grid:
            PaintGrid(painter, color, stroke);
            break;
        case Icon::ListView:
            PaintListView(painter, color, stroke);
            break;
        case Icon::Refresh:
            PaintRefresh(painter, color, stroke);
            break;
        case Icon::Edit:
            PaintEdit(painter, color, stroke);
            break;
        case Icon::Trash:
            PaintTrash(painter, color, stroke);
            break;
        case Icon::Calendar:
            PaintCalendar(painter, color, stroke);
            break;
    }
    painter.restore();
}

QPixmap Pixmap(Icon icon, int size, const QColor& color, qreal stroke) {
    qreal dpr = 1.0;
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        dpr = qMax<qreal>(1.0, screen->devicePixelRatio());
    }
    QPixmap pixmap(qRound(size * dpr), qRound(size * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    Paint(painter, icon, QRectF(0, 0, size, size), color, stroke);
    painter.end();
    return pixmap;
}

QIcon QIconFor(Icon icon, int size, const QColor& color, qreal stroke) {
    return QIcon(Pixmap(icon, size, color, stroke));
}

}  // namespace IconRenderer
