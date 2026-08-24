include(ExternalProject)

set(_wiiu_ffmpeg_prefix "${CMAKE_BINARY_DIR}/_deps/wiiu-ffmpeg")
set(_wiiu_ffmpeg_install "${_wiiu_ffmpeg_prefix}/install")
file(MAKE_DIRECTORY "${_wiiu_ffmpeg_install}/include")

ExternalProject_Add(playground_wiiu_ffmpeg_external
    PREFIX "${_wiiu_ffmpeg_prefix}"
    URL "https://ffmpeg.org/releases/ffmpeg-6.1.2.tar.xz"
    URL_HASH
        "SHA256=3b624649725ecdc565c903ca6643d41f33bd49239922e45c9b1442c63dca4e38"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=<SOURCE_DIR>"
        "-DINSTALL_DIR=${_wiiu_ffmpeg_install}"
        "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        -P "${PROJECT_SOURCE_DIR}/cmake/ConfigureWiiUFFmpeg.cmake"
    BUILD_COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=/opt/devkitpro/devkitPPC/bin:$ENV{PATH}"
        make -j2
    INSTALL_COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=/opt/devkitpro/devkitPPC/bin:$ENV{PATH}"
        make install
    BUILD_BYPRODUCTS
        "${_wiiu_ffmpeg_install}/lib/libavformat.a"
        "${_wiiu_ffmpeg_install}/lib/libavcodec.a"
        "${_wiiu_ffmpeg_install}/lib/libswscale.a"
        "${_wiiu_ffmpeg_install}/lib/libavutil.a")

add_library(playground_wiiu_ffmpeg INTERFACE)
add_dependencies(playground_wiiu_ffmpeg playground_wiiu_ffmpeg_external)
target_include_directories(playground_wiiu_ffmpeg INTERFACE
    "${_wiiu_ffmpeg_install}/include")
target_link_libraries(playground_wiiu_ffmpeg INTERFACE
    "${_wiiu_ffmpeg_install}/lib/libavformat.a"
    "${_wiiu_ffmpeg_install}/lib/libavcodec.a"
    "${_wiiu_ffmpeg_install}/lib/libswscale.a"
    "${_wiiu_ffmpeg_install}/lib/libavutil.a"
    m pthread)
add_library(Playground::FFmpeg ALIAS playground_wiiu_ffmpeg)
