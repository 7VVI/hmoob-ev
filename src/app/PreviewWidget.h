#pragma once

#include <QImage>
#include <QString>
#include <QWidget>
#include <memory>

#include "core/preview/PreviewFrame.h"

// Shows the latest BGRA preview frame, published by the recorder while
// recording or by the idle desktop preview otherwise. The image is scaled to
// cover the rounded card and clipped to it; the monitor badge is vector-drawn.
class PreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    // Passing nullptr clears the preview. The widget keeps `frame` alive for as
    // long as the QImage built over its pixels is in use.
    void SetFrame(std::shared_ptr<const hmmob::PreviewFrame> frame);

    void SetBadgeText(const QString& text);

    // Persistent error pill at the top-right of the preview. Empty clears it.
    // Independent of the status line above the bottom bar.
    void SetErrorBadge(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // Declared before image_ so the pixel buffer outlives the QImage.
    std::shared_ptr<const hmmob::PreviewFrame> frame_;
    QImage image_;
    QString badge_text_ = QStringLiteral("预览区域");
    QString error_badge_;
};
