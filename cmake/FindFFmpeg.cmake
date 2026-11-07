find_path(AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h REQUIRED)
find_path(AVFORMAT_INCLUDE_DIR NAMES libavformat/avformat.h REQUIRED)
find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h REQUIRED)
find_path(SWSCALE_INCLUDE_DIR NAMES libswscale/swscale.h REQUIRED)
find_path(SWRESAMPLE_INCLUDE_DIR NAMES libswresample/swresample.h REQUIRED)

find_library(AVCODEC_LIBRARY NAMES avcodec REQUIRED)
find_library(AVFORMAT_LIBRARY NAMES avformat REQUIRED)
find_library(AVUTIL_LIBRARY NAMES avutil REQUIRED)
find_library(SWSCALE_LIBRARY NAMES swscale REQUIRED)
find_library(SWRESAMPLE_LIBRARY NAMES swresample REQUIRED)

function(hmmob_add_ffmpeg_target name library include_dir)
  add_library(FFmpeg::${name} UNKNOWN IMPORTED)
  set_target_properties(FFmpeg::${name} PROPERTIES
    IMPORTED_LOCATION "${library}"
    INTERFACE_INCLUDE_DIRECTORIES "${include_dir}")
endfunction()

hmmob_add_ffmpeg_target(avcodec "${AVCODEC_LIBRARY}" "${AVCODEC_INCLUDE_DIR}")
hmmob_add_ffmpeg_target(avformat "${AVFORMAT_LIBRARY}" "${AVFORMAT_INCLUDE_DIR}")
hmmob_add_ffmpeg_target(avutil "${AVUTIL_LIBRARY}" "${AVUTIL_INCLUDE_DIR}")
hmmob_add_ffmpeg_target(swscale "${SWSCALE_LIBRARY}" "${SWSCALE_INCLUDE_DIR}")
hmmob_add_ffmpeg_target(swresample "${SWRESAMPLE_LIBRARY}" "${SWRESAMPLE_INCLUDE_DIR}")
