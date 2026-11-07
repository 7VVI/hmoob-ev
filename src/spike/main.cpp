#include <cstdio>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

static_assert(LIBAVCODEC_VERSION_MAJOR == 63, "Expected FFmpeg 9.x; update the vcpkg baseline deliberately");
static_assert(LIBAVFORMAT_VERSION_MAJOR == 63, "Expected FFmpeg 9.x; update the vcpkg baseline deliberately");

int main() {
    std::printf("FFmpeg avcodec=%u avformat=%u avutil=%u swscale=%u swresample=%u\n",
                avcodec_version(), avformat_version(), avutil_version(),
                swscale_version(), swresample_version());
    return 0;
}
