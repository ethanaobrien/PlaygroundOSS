#include "Playground/Platform/BuildTarget.h"

#include <stdio.h>

int main(void)
{
    const PlaygroundBuildTarget* target = playgroundGetBuildTarget();
    printf(
        "platform=%s architecture=%s pointer_bits=%u\n",
        target->platform,
        target->architecture,
        target->pointerBits
    );
    return 0;
}
