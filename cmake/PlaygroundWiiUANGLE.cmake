include(ExternalProject)
find_package(Git REQUIRED)
find_package(ZLIB REQUIRED)

# This pinned GX2 backend preserves the engine's GLES2 API contract on Wii U.
# It is a reviewed platform dependency, not a floating ANGLE mainline build.
set(_playground_wiiu_angle_commit
    "315cdea217bbbfcdb27f8ba43de9ce2187011839")
set(_playground_wiiu_angle_prefix
    "${CMAKE_BINARY_DIR}/_deps/playground-wiiu-angle")
set(_playground_wiiu_angle_install
    "${_playground_wiiu_angle_prefix}/install")
set(_playground_wiiu_angle_source
    "${_playground_wiiu_angle_prefix}/src/playground_wiiu_angle_external")

ExternalProject_Add(playground_wiiu_angle_external
    PREFIX "${_playground_wiiu_angle_prefix}"
    SOURCE_DIR "${_playground_wiiu_angle_source}"
    GIT_REPOSITORY "https://github.com/GaryOderNichts/angle.git"
    GIT_TAG "${_playground_wiiu_angle_commit}"
    GIT_SHALLOW FALSE
    GIT_SUBMODULES "third_party/zlib"
    GIT_SUBMODULES_RECURSE FALSE
    UPDATE_DISCONNECTED TRUE
    PATCH_COMMAND
        "${GIT_EXECUTABLE}" apply --ignore-space-change
        "${CMAKE_CURRENT_LIST_DIR}/patches/angle-wiiu-runtime-compat.patch"
    CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
        "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_INSTALL_PREFIX=${_playground_wiiu_angle_install}"
        "-DPORT=WiiU"
        "-DBUILD_SHARED_LIBS=OFF"
        "-DBUILD_SAMPLES=OFF"
        "-DBUILD_TESTS=OFF"
    BUILD_BYPRODUCTS
        "${_playground_wiiu_angle_install}/lib/libEGL.a"
        "${_playground_wiiu_angle_install}/lib/libGLESv2.a"
        "${_playground_wiiu_angle_install}/lib/libANGLE.a"
    INSTALL_DIR "${_playground_wiiu_angle_install}"
    USES_TERMINAL_DOWNLOAD TRUE
    USES_TERMINAL_BUILD TRUE
    USES_TERMINAL_INSTALL TRUE)

file(MAKE_DIRECTORY "${_playground_wiiu_angle_install}/include/angle")
file(MAKE_DIRECTORY "${_playground_wiiu_angle_source}/src/common/wiiu")

add_library(playground_wiiu_angle_core STATIC IMPORTED GLOBAL)
set_target_properties(playground_wiiu_angle_core PROPERTIES
    IMPORTED_LOCATION "${_playground_wiiu_angle_install}/lib/libANGLE.a")
add_dependencies(playground_wiiu_angle_core playground_wiiu_angle_external)

add_library(playground_wiiu_gles2 STATIC IMPORTED GLOBAL)
set_target_properties(playground_wiiu_gles2 PROPERTIES
    IMPORTED_LOCATION "${_playground_wiiu_angle_install}/lib/libGLESv2.a")
add_dependencies(playground_wiiu_gles2 playground_wiiu_angle_external)

add_library(playground_wiiu_egl STATIC IMPORTED GLOBAL)
set_target_properties(playground_wiiu_egl PROPERTIES
    IMPORTED_LOCATION "${_playground_wiiu_angle_install}/lib/libEGL.a")
add_dependencies(playground_wiiu_egl playground_wiiu_angle_external)

add_library(playground_wiiu_angle INTERFACE)
add_library(Playground::WiiUANGLE ALIAS playground_wiiu_angle)
add_dependencies(playground_wiiu_angle playground_wiiu_angle_external)
target_include_directories(playground_wiiu_angle INTERFACE
    "${_playground_wiiu_angle_install}/include/angle"
    "${_playground_wiiu_angle_source}/src/common/wiiu")
# Static archive order is significant: EGL calls GLES, and GLES calls core.
target_link_libraries(playground_wiiu_angle INTERFACE
    playground_wiiu_egl
    playground_wiiu_gles2
    playground_wiiu_angle_core
    ZLIB::ZLIB)
