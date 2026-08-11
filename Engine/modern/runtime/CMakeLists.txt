find_package(CURL REQUIRED)
find_package(Freetype REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PNG REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

if(WIN32)
    find_path(PLAYGROUND_FFMPEG_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(PLAYGROUND_VCPKG_LIBRARY_DIR
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
        set(PLAYGROUND_VCPKG_DEBUG_LIBRARY_DIR
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")
    endif()
    find_library(PLAYGROUND_AVCODEC_LIBRARY NAMES avcodec
        PATHS "${PLAYGROUND_VCPKG_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(PLAYGROUND_AVFORMAT_LIBRARY NAMES avformat
        PATHS "${PLAYGROUND_VCPKG_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(PLAYGROUND_AVUTIL_LIBRARY NAMES avutil
        PATHS "${PLAYGROUND_VCPKG_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(PLAYGROUND_SWSCALE_LIBRARY NAMES swscale
        PATHS "${PLAYGROUND_VCPKG_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(PLAYGROUND_AVCODEC_DEBUG_LIBRARY NAMES avcodec
        PATHS "${PLAYGROUND_VCPKG_DEBUG_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(PLAYGROUND_AVFORMAT_DEBUG_LIBRARY NAMES avformat
        PATHS "${PLAYGROUND_VCPKG_DEBUG_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(PLAYGROUND_AVUTIL_DEBUG_LIBRARY NAMES avutil
        PATHS "${PLAYGROUND_VCPKG_DEBUG_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(PLAYGROUND_SWSCALE_DEBUG_LIBRARY NAMES swscale
        PATHS "${PLAYGROUND_VCPKG_DEBUG_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    add_library(playground_ffmpeg INTERFACE)
    target_include_directories(playground_ffmpeg INTERFACE
        "${PLAYGROUND_FFMPEG_INCLUDE_DIR}")
    target_link_libraries(playground_ffmpeg INTERFACE
        optimized "${PLAYGROUND_AVCODEC_LIBRARY}"
        optimized "${PLAYGROUND_AVFORMAT_LIBRARY}"
        optimized "${PLAYGROUND_AVUTIL_LIBRARY}"
        optimized "${PLAYGROUND_SWSCALE_LIBRARY}"
        debug "${PLAYGROUND_AVCODEC_DEBUG_LIBRARY}"
        debug "${PLAYGROUND_AVFORMAT_DEBUG_LIBRARY}"
        debug "${PLAYGROUND_AVUTIL_DEBUG_LIBRARY}"
        debug "${PLAYGROUND_SWSCALE_DEBUG_LIBRARY}"
    )
    add_library(Playground::FFmpeg ALIAS playground_ffmpeg)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec libavformat libavutil libswscale)
    add_library(Playground::FFmpeg ALIAS PkgConfig::FFMPEG)
endif()

file(GLOB_RECURSE playground_engine_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/source/*.c"
    "${PROJECT_SOURCE_DIR}/Engine/source/*.cpp"
)
list(FILTER playground_engine_sources EXCLUDE REGEX "/Core/CKLBStream\\.cpp$")
list(FILTER playground_engine_sources EXCLUDE REGEX "/Scripting/ExportListC_Likefunction\\.cpp$")
# These implementation files are deliberately included by their owning
# translation units in the legacy build and must not also be compiled alone.
list(FILTER playground_engine_sources EXCLUDE REGEX "/Database/CKLBDataTask\\.cpp$")
list(FILTER playground_engine_sources EXCLUDE REGEX "/UISystem/CKLBRenderBufferTask\\.cpp$")
list(FILTER playground_engine_sources EXCLUDE REGEX "/UISystem/CKLBUIPolygon\\.cpp$")
list(FILTER playground_engine_sources EXCLUDE REGEX "/UISystem/CKLBUIPolyline2\\.cpp$")
list(FILTER playground_engine_sources EXCLUDE REGEX "/UISystem/CKLBUIScale9Btn\\.cpp$")

file(GLOB playground_json_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/libs/JSonParser/*.c"
)
file(GLOB playground_lua_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/libs/lua/*.c"
)
file(GLOB playground_minizip_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/libs/minizip/*.c"
)
file(GLOB playground_sha1_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/libs/sha1/*.c"
)
file(GLOB playground_tremolo_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/libs/Tremolo/*.c"
)
file(GLOB playground_utf8_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/libs/utf8_converter/*.c"
)
file(GLOB playground_spine_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/libs/spine-c-3.8.75/src/spine/*.c"
)
file(GLOB playground_timeline_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/libs/timelinefx-2016-10-30/source/*.cpp"
)
file(GLOB playground_jpeg_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/libs/libjpeg-9a/*.c"
)

set(playground_msgpack_sources
    "${PROJECT_SOURCE_DIR}/Engine/libs/msgpack-c-0.5.7/src/object.cpp"
    "${PROJECT_SOURCE_DIR}/Engine/libs/msgpack-c-0.5.7/src/objectc.c"
    "${PROJECT_SOURCE_DIR}/Engine/libs/msgpack-c-0.5.7/src/unpack.c"
    "${PROJECT_SOURCE_DIR}/Engine/libs/msgpack-c-0.5.7/src/version.c"
    "${PROJECT_SOURCE_DIR}/Engine/libs/msgpack-c-0.5.7/src/vrefbuffer.c"
    "${PROJECT_SOURCE_DIR}/Engine/libs/msgpack-c-0.5.7/src/zone.c"
)

set(playground_chipmunk_sources
    chipmunk.c
    cpArbiter.c
    cpArray.c
    cpBBTree.c
    cpBody.c
    cpCollision.c
    cpHashSet.c
    cpPolyShape.c
    cpRobust.c
    cpShape.c
    cpSpace.c
    cpSpaceComponent.c
    cpSpaceHash.c
    cpSpaceStep.c
    cpSpatialIndex.c
)
list(TRANSFORM playground_chipmunk_sources
    PREPEND "${PROJECT_SOURCE_DIR}/Engine/libs/Chipmunk2D-7.0.3/src/"
)

add_library(playground_engine_runtime STATIC
    src/DesktopPlatform.cpp
    src/DesktopStateStore.cpp
    src/DesktopWidgets.cpp
    src/DesktopServices.cpp
    src/DesktopAudioPlatform.cpp
    src/DesktopMoviePlatform.cpp
    "${PROJECT_SOURCE_DIR}/Engine/porting/Android/jni/Android/KLBOpenSLNewEngine.cpp"
    ${playground_engine_sources}
    ${playground_json_sources}
    ${playground_lua_sources}
    ${playground_minizip_sources}
    ${playground_sha1_sources}
    ${playground_tremolo_sources}
    ${playground_utf8_sources}
    ${playground_spine_sources}
    ${playground_timeline_sources}
    ${playground_jpeg_sources}
    ${playground_msgpack_sources}
    ${playground_chipmunk_sources}
    "${PROJECT_SOURCE_DIR}/Engine/libs/SQLite/sqlite3.c"
    "${PROJECT_SOURCE_DIR}/Engine/libs/poly2tri-2015-01-27/poly2tri/poly2tri_unity.cc"
    "${PROJECT_SOURCE_DIR}/Engine/libs/pugixml-1.2/src/pugixml.cpp"
    "${PROJECT_SOURCE_DIR}/Engine/porting/FileDelete.cpp"
    "${PROJECT_SOURCE_DIR}/Engine/porting/FontRendering.cpp"
    "${PROJECT_SOURCE_DIR}/Engine/porting/dirent.c"
)
add_library(Playground::EngineRuntime ALIAS playground_engine_runtime)

target_compile_features(playground_engine_runtime PUBLIC cxx_std_17)
target_compile_features(playground_engine_runtime PRIVATE c_std_99)
target_compile_definitions(playground_engine_runtime
    PRIVATE
        IOAPI_NO_64
        LUA_ANSI
        ONLY_C
        PLAYGROUND_MODERN_RUNTIME
        PLAYGROUND_RUNTIME_DIAGNOSTICS
        $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>
        $<$<PLATFORM_ID:Windows>:_HAS_AUTO_PTR_ETC=1>
        $<$<PLATFORM_ID:Windows>:_USE_MATH_DEFINES>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
)
target_compile_definitions(playground_engine_runtime PUBLIC OPENGL2)

set(playground_engine_include_directories
    Engine/include
    Engine/source
    Engine/source/include
    Engine/source/Animation
    Engine/source/Assets
    Engine/source/Core
    Engine/source/Database
    Engine/source/HTTP
    Engine/source/LuaLib
    Engine/source/Rendering
    Engine/source/SceneGraph
    Engine/source/Scripting
    Engine/source/Sound
    Engine/source/SystemTask
    Engine/source/UISystem
    Engine/source/UserTask
    Engine/source/UserTask/game
    Engine/libs
    Engine/libs/Chipmunk2D-7.0.3/include
    Engine/libs/JSonParser
    Engine/libs/JSonParser/api
    Engine/libs/SQLite
    Engine/libs/Tremolo
    Engine/libs/libjpeg-9a
    Engine/libs/lua
    Engine/libs/minizip
    Engine/libs/msgpack-c-0.5.7/include
    Engine/libs/poly2tri-2015-01-27
    Engine/libs/pugixml-1.2/include
    Engine/libs/sha1
    Engine/libs/spine-c-3.8.75/include
    Engine/libs/timelinefx-2016-10-30/source
    Engine/libs/utf8_converter
    Engine/porting
    Engine/porting/Android/jni/Android
)
list(TRANSFORM playground_engine_include_directories PREPEND "${PROJECT_SOURCE_DIR}/")

target_include_directories(playground_engine_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        ${playground_engine_include_directories}
    PRIVATE
        "${CURL_INCLUDE_DIRS}/curl"
)
target_link_libraries(playground_engine_runtime
    PUBLIC
        CURL::libcurl
        Freetype::Freetype
        OpenSSL::Crypto
        Playground::FFmpeg
        PNG::PNG
        SDL3::SDL3
        Threads::Threads
        ZLIB::ZLIB
)

if(WIN32)
    find_package(unofficial-angle CONFIG REQUIRED)
    # vcpkg defines ANGLE's imported targets in the calling directory. Promote
    # them so the sibling SDL host can deploy the same runtime selected here.
    set_property(TARGET unofficial::angle::libEGL PROPERTY IMPORTED_GLOBAL TRUE)
    set_property(TARGET unofficial::angle::libGLESv2 PROPERTY IMPORTED_GLOBAL TRUE)
    target_link_libraries(playground_engine_runtime PUBLIC
        unofficial::angle::libEGL
        unofficial::angle::libGLESv2
        bcrypt
        psapi
    )
else()
    find_library(PLAYGROUND_GLES2_LIBRARY GLESv2 REQUIRED)
    target_link_libraries(playground_engine_runtime PUBLIC
        ${PLAYGROUND_GLES2_LIBRARY})
endif()

set_target_properties(playground_engine_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

function(playground_copy_sdl_runtime target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()

if(PLAYGROUND_TARGET_PLATFORM STREQUAL linux
   OR PLAYGROUND_TARGET_PLATFORM STREQUAL windows)
    add_executable(playground-engine-link-probe tools/runtime_link_probe.cpp)
    target_link_libraries(playground-engine-link-probe
        PRIVATE
            $<LINK_LIBRARY:WHOLE_ARCHIVE,playground_engine_runtime>
    )
    playground_copy_sdl_runtime(playground-engine-link-probe)
    add_executable(playground-stream-probe tools/runtime_stream_probe.cpp)
    target_link_libraries(playground-stream-probe PRIVATE playground_engine_runtime)
    playground_copy_sdl_runtime(playground-stream-probe)
    add_executable(playground-platform-services-probe
        tools/platform_services_probe.cpp)
    target_link_libraries(playground-platform-services-probe
        PRIVATE playground_engine_runtime)
    playground_copy_sdl_runtime(playground-platform-services-probe)
    add_executable(playground-audio-probe tools/audio_probe.cpp)
    target_link_libraries(playground-audio-probe PRIVATE playground_engine_runtime)
    playground_copy_sdl_runtime(playground-audio-probe)
    add_executable(playground-movie-probe tools/movie_probe.cpp)
    target_link_libraries(playground-movie-probe PRIVATE playground_engine_runtime)
    playground_copy_sdl_runtime(playground-movie-probe)
endif()
