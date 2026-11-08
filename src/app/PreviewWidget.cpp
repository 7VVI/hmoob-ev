#include "app/PreviewWidget.h"

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <utility>

#include "app/IconRenderer.h"

namespace {
constexpr qreal kRadius = 14.0;
constexpr qreal kBadgeMargin = 20.0;
constexpr qreal kBadgeHeight = 40.0;
constexpr qreal kBadgePadding = 16.0;
constexpr qreal kBadgeIcon = 18.0;
constexpr qreal kBadgeGap = 8.0;
}  // namespace

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(420, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
}

void PreviewWidget::SetFrame(std::shared_ptr<const hmmob::PreviewFrame> frame) {
    if (!frame || frame->width <= 0 || frame->height <= 0 ||
        frame->stride < frame->width * 4 ||
        frame->bgra.size() <
            static_cast<size_t>(frame->stride) * static_cast<size_t>(frame->height)) {
        image_ = QImage();
        frame_.reset();
        update();
        return;
    }

    frame_ = std::move(frame);
    // QImage::Format_RGB32 expects 0xffRRGGBB words; on little-endian that is
    // B,G,R,A in memory, which is exactly the BGRA8 preview layout.
    image_ = QImage(frame_->bgra.data(), frame_->width, frame_->height,
                    frame_->stride, QImage::Format_RGB32);
    update();
}

void PreviewWidget::SetBadgeText(const QString& text) {
    if (badge_text_ == text) {
        return;
    }
    badge_text_ = text;
    update();
}

void PreviewWidget::SetPlaceholderText(const QString& text) {
    if (placeholder_text_ == text) {
        return;
    }
    placeholder_text_ = text;
    update();
}

void PreviewWidget::SetErrorBadge(const QString& text) {
    if (error_badge_ == text) {
        return;
    }
    error_badge_ = text;
    update();
}

void PreviewWidget::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(card, kRadius, kRadius);
    painter.setClipPath(clip);

    painter.fillRect(rect(), QColor(0xEE, 0xF2, 0xF8));

    if (!image_.isNull()) {
        // Cover the card so the rounded preview is filled edge to edge, as in
        // the reference design.
        const QSizeF scaled = image_.size().scaled(
            card.size().toSize(), Qt::KeepAspectRatioByExpanding);
        const QRectF target(QPointF(card.center().x() - scaled.width() / 2.0,
                                    card.center().y() - scaled.height() / 2.0),
                            scaled);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, image_);
    } else {
        painter.setPen(QColor(0x9A, 0xA3, 0xB2));
        QFont idle_font = font();
        idle_font.setPixelSize(15);
        painter.setFont(idle_font);
        painter.drawText(rect(), Qt::AlignCenter, placeholder_text_);
    }

    // Badge: dark translucent pill with a monitor glyph.
    QFont badge_font = font();
    badge_font.setPixelSize(14);
    badge_font.setWeight(QFont::Medium);
    painter.setFont(badge_font);
    const QFontMetrics metrics(badge_font);
    const qreal text_width = metrics.horizontalAdvance(badge_text_);
    const qreal badge_width =
        kBadgePadding * 2 + kBadgeIcon + kBadgeGap + text_width;
    const QRectF badge(kBadgeMargin, kBadgeMargin, badge_width, kBadgeHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.drawRoundedRect(badge, 8.0, 8.0);

    const QRectF icon_rect(badge.left() + kBadgePadding,
                           badge.center().y() - kBadgeIcon / 2.0, kBadgeIcon,
                           kBadgeIcon);
    IconRenderer::Paint(painter, Icon::Monitor, icon_rect, QColor(Qt::white),
                        1.7);
    painter.setPen(QColor(Qt::white));
    painter.drawText(
        QRectF(icon_rect.right() + kBadgeGap, badge.top(),
               text_width + 2, badge.height()),
        Qt::AlignLeft | Qt::AlignVCenter, badge_text_);

    if (!error_badge_.isEmpty()) {
        QFont error_font = font();
        error_font.setPixelSize(14);
        error_font.setWeight(QFont::DemiBold);
        painter.setFont(error_font);
        const QFontMetrics error_metrics(error_font);
        const qreal error_width =
            kBadgePadding * 2 + error_metrics.horizontalAdvance(error_badge_);
        const QRectF error_badge(width() - kBadgeMargin - error_width,
                                 kBadgeMargin, error_width, 36);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xE5, 0x48, 0x4D, 245));
        painter.drawRoundedRect(error_badge, 8.0, 8.0);
        painter.setPen(QColor(Qt::white));
        painter.drawText(error_badge, Qt::AlignCenter, error_badge_);
    }

    painter.setClipping(false);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0xE7, 0xEB, 0xF2), 1.0));
    painter.drawRoundedRect(card, kRadius, kRadius);
}
