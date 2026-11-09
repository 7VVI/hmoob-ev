#pragma once

#include <QImage>
#include <QString>

// Basic stream metadata used by the file manager's cards and details panel.
struct VideoInfo {
    int width = 0;
    int height = 0;
    double duration_seconds = 0.0;
};

// Opens `path` with libavformat and reads the first video stream's size and
// the container duration. Returns false when the file is not a readable video.
bool ReadVideoInfo(const QString& path, VideoInfo& info);

// Decodes the first video frame and scales it to exactly `width` x `height`
// with a centre crop (cover, no distortion). Returns a null QImage when the
// file cannot be decoded. The result is QImage::Format_RGB32.
QImage DecodeThumbnail(const QString& path, int width, int height);
