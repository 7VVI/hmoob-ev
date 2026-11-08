#include "app/NavTabButton.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

#include "app/Theme.h"

namespace {
constexpr int kHeight = 56;
constexpr int kSidePadding = 22;
constexpr int kIconSize = 24;
constexpr int kIconGap = 9;
constexpr int kUnderlineInset = 24;
constexpr int kUnderlineHeight = 3;
}  // namespace

NavTabButton::NavTabButton(Icon icon, const QString& text, QWidget* parent)
    : QAbstractButton(parent), icon_(icon), text_(text) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(kHeight);
    setAttribute(Qt::WA_Hover, true);
    connect(&Theme::Instance(), &Theme::changed, this,
            [this] { update(); });
}

QSize NavTabButton::sizeHint() const {
    const QFontMetrics metrics(font());
    return QSize(kSidePadding * 2 + kIconSize + kIconGap +
                     metrics.horizontalAdvance(text_),
                 kHeight);
}

void NavTabButton::SetActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    update();
}

void NavTabButton::enterEvent(QEnterEvent* event) {
    hover_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void NavTabButton::leaveEvent(QEvent* event) {
    hover_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void NavTabButton::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const ThemeColors& colors = Theme::Instance().colors();
    const QRectF bounds = rect();
    if (active_) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.navActiveBg);
        painter.drawRoundedRect(bounds, 10.0, 10.0);
    } else if (hover_) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.navHoverBg);
        painter.drawRoundedRect(bounds, 10.0, 10.0);
    }

    const QColor color =
        active_ ? colors.primary : colors.textSecondary;
    const QRectF icon_rect(kSidePadding, (height() - kIconSize) / 2.0,
                           kIconSize, kIconSize);
    IconRenderer::Paint(painter, icon_, icon_rect, color);

    QFont label_font = font();
    label_font.setPixelSize(15);
    label_font.setWeight(active_ ? QFont::DemiBold : QFont::Normal);
    painter.setFont(label_font);
    painter.setPen(color);
    const QRectF text_rect(kSidePadding + kIconSize + kIconGap, 0,
                           width() - kSidePadding - kIconSize - kIconGap,
                           height());
    painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, text_);

    if (active_) {
        const qreal underline_width =
            qMax<qreal>(0.0, width() - kUnderlineInset * 2.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.primary);
        painter.drawRoundedRect(
            QRectF((width() - underline_width) / 2.0,
                   height() - kUnderlineHeight, underline_width,
                   kUnderlineHeight),
            kUnderlineHeight / 2.0, kUnderlineHeight / 2.0);
    }
}
