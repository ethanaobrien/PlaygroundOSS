find_package(CURL REQUIRED)
find_package(Freetype REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PNG REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

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
    src/DesktopServices.cpp
    src/NullAudio.cpp
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
)
add_library(Playground::EngineRuntime ALIAS playground_engine_runtime)

target_compile_features(playground_engine_runtime PUBLIC cxx_std_11)
target_compile_features(playground_engine_runtime PRIVATE cxx_std_17)
target_compile_features(playground_engine_runtime PRIVATE c_std_99)
target_compile_definitions(playground_engine_runtime
    PRIVATE
        IOAPI_NO_64
        LUA_ANSI
        ONLY_C
        OPENGL2
)

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
        PNG::PNG
        Threads::Threads
        ZLIB::ZLIB
)

find_library(PLAYGROUND_GLES2_LIBRARY GLESv2 REQUIRED)
target_link_libraries(playground_engine_runtime PUBLIC ${PLAYGROUND_GLES2_LIBRARY})

set_target_properties(playground_engine_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if(PLAYGROUND_TARGET_PLATFORM STREQUAL linux)
    add_executable(playground-engine-link-probe tools/runtime_link_probe.cpp)
    target_link_libraries(playground-engine-link-probe
        PRIVATE
            $<LINK_LIBRARY:WHOLE_ARCHIVE,playground_engine_runtime>
    )
    add_executable(playground-stream-probe tools/runtime_stream_probe.cpp)
    target_link_libraries(playground-stream-probe PRIVATE playground_engine_runtime)
    add_executable(playground-platform-services-probe
        tools/platform_services_probe.cpp)
    target_link_libraries(playground-platform-services-probe
        PRIVATE playground_engine_runtime)
endif()
