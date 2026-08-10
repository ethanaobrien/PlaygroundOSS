#ifndef PLAYGROUND_PLATFORM_BUILD_TARGET_H
#define PLAYGROUND_PLATFORM_BUILD_TARGET_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PlaygroundBuildTarget {
    const char* platform;
    const char* architecture;
    unsigned int pointerBits;
    unsigned int isAndroid;
    unsigned int isWindows;
    unsigned int isLinux;
    unsigned int isMacOS;
} PlaygroundBuildTarget;

const PlaygroundBuildTarget* playgroundGetBuildTarget(void);

#ifdef __cplusplus
}
#endif

#endif
