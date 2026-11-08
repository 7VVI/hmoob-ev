#include "app/PreviewWidget.h"

#include <QColor>
#include <QPainter>
#include <QPaintEvent>
#include <utility>

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(560, 315);
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

void PreviewWidget::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 24, 24));

    if (image_.isNull()) {
        painter.setPen(QColor(160, 160, 160));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("预览未开始"));
        return;
    }

    const QSize target = image_.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target_rect(QPoint((width() - target.width()) / 2,
                                   (height() - target.height()) / 2),
                            target);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target_rect, image_);
}
