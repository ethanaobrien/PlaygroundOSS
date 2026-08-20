#include "Playground/Platform/BuildTarget.h"
#include "Playground/Platform/BuildTargetConfig.h"

const PlaygroundBuildTarget* playgroundGetBuildTarget(void)
{
    static const PlaygroundBuildTarget target = {
        PLAYGROUND_TARGET_PLATFORM,
        PLAYGROUND_TARGET_ARCHITECTURE,
        PLAYGROUND_TARGET_POINTER_BITS,
        PLAYGROUND_PLATFORM_ANDROID,
        PLAYGROUND_PLATFORM_WINDOWS,
        PLAYGROUND_PLATFORM_LINUX,
        PLAYGROUND_PLATFORM_MACOS,
        PLAYGROUND_PLATFORM_SWITCH
    };
    return &target;
}
