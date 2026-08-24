set(ENV{PATH}
    "/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/portlibs/wiiu/bin:$ENV{PATH}")
set(ENV{PKG_CONFIG}
    "/opt/devkitpro/portlibs/wiiu/bin/powerpc-eabi-pkg-config")
execute_process(
    COMMAND "${SOURCE_DIR}/configure"
        "--prefix=${INSTALL_DIR}"
        --enable-cross-compile --cross-prefix=powerpc-eabi-
        --arch=ppc --cpu=750 --target-os=none
        --disable-programs --disable-doc --disable-debug --disable-autodetect
        --disable-everything --disable-avdevice --disable-avfilter
        --disable-swresample --disable-iconv --disable-pthreads
        --disable-w32threads --disable-os2threads
        --enable-avcodec --enable-avformat --enable-avutil --enable-swscale
        --enable-decoder=h264,mpeg4,mpeg2video,mjpeg
        --enable-demuxer=mov,mpegvideo
        --enable-parser=h264,mpeg4video,mpegvideo
        --enable-protocol=file --disable-pic
        "--extra-cflags=-D__WIIU__ -D_GNU_SOURCE -mcpu=750 -meabi -mhard-float -ffunction-sections -fdata-sections"
        "--extra-ldflags=-specs=/opt/devkitpro/wut/share/wut.specs"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "FFmpeg Wii U configure failed: ${_result}")
endif()
set(CONFIG_HEADER "${SOURCE_DIR}/config.h")
include("${PROJECT_ROOT}/cmake/DisableFFmpegMmap.cmake")
