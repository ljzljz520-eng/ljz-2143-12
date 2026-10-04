#ifndef IMAGE_PNG_H
#define IMAGE_PNG_H

#include <SDL2/SDL.h>

/* 从内存加载 PNG 为 ARGB8888 surface（供纹理缓存缩放/上传）。 */
SDL_Surface *png_load_from_memory(const unsigned char *data, int len);
#endif
