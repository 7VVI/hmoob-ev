#include "app/ModeTile.h"

#include <QPainter>
#include <QPaintEvent>

namespace {
constexpr int kIconSize = 30;
constexpr int kCheckSize = 20;
}  // namespace

ModeTile::ModeTile(Icon icon, const QString& text, QWidget* parent)
    : QAbstractButton(parent), icon_(icon), text_(text) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    setMinimumHeight(76);
    setMaximumHeight(96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize ModeTile::sizeHint() const {
    return QSize(96, 96);
}

void ModeTile::enterEvent(QEnterEvent* event) {
    hover_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ModeTile::leaveEvent(QEvent* event) {
    hover_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ModeTile::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool selected = isChecked();
    QRectF tile(rect());
    tile.adjust(1.0, 1.0, -1.0, -1.0);

    QColor fill(0xFF, 0xFF, 0xFF);
    QColor border(0xE5, 0xE6, 0xEB);
    if (selected) {
        fill = QColor(0xEE, 0xF5, 0xFF);
        border = QColor(0x2F, 0x6B, 0xFF);
    } else if (hover_) {
        fill = QColor(0xF7, 0xF9, 0xFD);
        border = QColor(0xD5, 0xDB, 0xE5);
    }
    painter.setBrush(fill);
    painter.setPen(QPen(border, selected ? 2.0 : 1.0));
    painter.drawRoundedRect(tile, 10.0, 10.0);

    const QColor content = selected ? QColor(0x2F, 0x6B, 0xFF)
                                    : QColor(0x4E, 0x59, 0x69);
    const qreal icon_top = tile.top() + tile.height() * 0.24 - kIconSize / 2.0;
    const QRectF icon_rect((width() - kIconSize) / 2.0, icon_top, kIconSize,
                           kIconSize);
    IconRenderer::Paint(painter, icon_, icon_rect, content, 1.7);

    QFont label_font = font();
    label_font.setPixelSize(15);
    label_font.setWeight(QFont::DemiBold);
    painter.setFont(label_font);
    painter.setPen(selected ? QColor(0x2F, 0x6B, 0xFF) : QColor(0x1D, 0x21, 0x29));
    painter.drawText(QRectF(0, height() - 34.0, width(), 24.0),
                     Qt::AlignHCenter | Qt::AlignVCenter, text_);

    if (selected) {
        const QRectF badge(width() - kCheckSize - 8.0, 6.0, kCheckSize,
                           kCheckSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x2F, 0x6B, 0xFF));
        painter.drawEllipse(badge);
        IconRenderer::Paint(painter, Icon::Check, badge.adjusted(4, 4, -4, -4),
                            QColor(Qt::white), 2.6);
    }
}
