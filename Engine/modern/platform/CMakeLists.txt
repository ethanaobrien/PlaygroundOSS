configure_file(
    include/Playground/Platform/BuildTargetConfig.h.in
    generated/Playground/Platform/BuildTargetConfig.h
    @ONLY
)

add_library(playground_platform_contract STATIC
    src/BuildTarget.c
)
add_library(Playground::PlatformContract ALIAS playground_platform_contract)

target_include_directories(playground_platform_contract
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_features(playground_platform_contract PUBLIC c_std_11)
set_target_properties(playground_platform_contract PROPERTIES
    EXPORT_NAME PlatformContract
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
)

if(PLAYGROUND_BUILD_PLATFORM_PROBE)
    add_executable(playground-platform-info tools/platform_info.c)
    target_link_libraries(playground-platform-info PRIVATE Playground::PlatformContract)
endif()

install(TARGETS playground_platform_contract
    EXPORT PlaygroundModernizationTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(
    FILES include/Playground/Platform/BuildTarget.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Playground/Platform
)
install(EXPORT PlaygroundModernizationTargets
    NAMESPACE Playground::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PlaygroundModernization
)
