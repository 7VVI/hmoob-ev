#include <cstdio>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

int main() {
    std::printf("FFmpeg avcodec=%u avformat=%u avutil=%u\n",
                avcodec_version(), avformat_version(), avutil_version());
    return 0;
}
