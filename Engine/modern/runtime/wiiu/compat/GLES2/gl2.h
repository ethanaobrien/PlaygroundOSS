#ifndef PLAYGROUND_WIIU_GLES2_GL2_H
#define PLAYGROUND_WIIU_GLES2_GL2_H

/*
 * WUT has no OpenGL implementation, but SDL2 ships the Khronos GLES2 type,
 * constant, and prototype headers.  The Wii U renderer implements the
 * functions behind these declarations on GX2.
 */
#include <SDL2/SDL_opengles2_khrplatform.h>
#include <SDL2/SDL_opengles2_gl2platform.h>
#include <SDL2/SDL_opengles2_gl2.h>

#endif
