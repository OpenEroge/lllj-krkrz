# movie-ffmpeg: VideoOverlay backend for mp4/wmv/asf/mov/avi on FFmpeg, plus
# ASS subtitles (libass) from registered providers (include/compat/subtitles.h).
#
# FFmpeg is the prebuilt LGPL shared build from tools/fetch_ffmpeg.sh. The
# launcher copies the libraries next to the executable; $ORIGIN is a DT_RPATH
# (the engine links with --disable-new-dtags), which also covers libavcodec's
# own libswresample, so same-soname system libraries are never picked up.
if(NOT COMPAT_FFMPEG_DIR)
    get_filename_component(COMPAT_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/third_party/ffmpeg" ABSOLUTE)
endif()
if(NOT EXISTS "${COMPAT_FFMPEG_DIR}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFmpeg not found in ${COMPAT_FFMPEG_DIR}; run tools/fetch_ffmpeg.sh")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(COMPAT_LIBASS REQUIRED IMPORTED_TARGET GLOBAL libass)
compat_module(movie-ffmpeg
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE_INCLUDE ${COMPAT_FFMPEG_DIR}/include
                    ${KRKRZ_CORE_DIR}/external/movie-player/extlibs/libyuv/include
    LIBS ${COMPAT_FFMPEG_DIR}/lib/libavformat.so ${COMPAT_FFMPEG_DIR}/lib/libavcodec.so
         ${COMPAT_FFMPEG_DIR}/lib/libavutil.so PkgConfig::COMPAT_LIBASS)
