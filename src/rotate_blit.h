#ifndef ROTATE_BLIT_H
#define ROTATE_BLIT_H

#include <SDL2/SDL.h>

/*
 * 把 src 纹理（pw×ph，ARGB8888）按 90/270 旋转 1:1 写入 dst（ph×pw）。
 * 通过锁像素直接搬运，绕开 RenderCopyEx 在软件后端对非正方形纹理的缩放。
 * 旋转方向与 geometry.c 中 logical_to_phys 完全一致（顺时针）。
 * 返回 0 成功，-1 失败。
 */
int rotate_blit_90_270(SDL_Renderer *r, SDL_Texture *src, int pw, int ph,
                       SDL_Texture *dst, int rotation /*90 or 270*/);

#endif
