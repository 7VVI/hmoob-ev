#pragma once

#include <QImage>
#include <QWidget>
#include <memory>

#include "core/preview/PreviewFrame.h"

// Shows the latest BGRA preview frame, published by the recorder while
// recording or by the idle desktop preview otherwise, scaled to fit while
// preserving the aspect ratio. No alpha blending: the frame format is BGRA8
// but the A byte is ignored.
class PreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    // Passing nullptr clears the preview. The widget keeps `frame` alive for as
    // long as the QImage built over its pixels is in use.
    void SetFrame(std::shared_ptr<const hmmob::PreviewFrame> frame);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // Declared before image_ so the pixel buffer outlives the QImage.
    std::shared_ptr<const hmmob::PreviewFrame> frame_;
    QImage image_;
};
