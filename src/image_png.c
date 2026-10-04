#include "image_png.h"

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SDL_Surface *png_load_from_memory(const unsigned char *data, int len) {
    if (!data || len < 8) return NULL;
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_memory(&img, data, (png_size_t)len) == 0) {
        fprintf(stderr, "png begin failed: %s\n", img.message);
        return NULL;
    }
    img.format = PNG_FORMAT_BGRA; /* 小端字节序 B,G,R,A == SDL_PIXELFORMAT_ARGB8888 */
    png_size_t stride = PNG_IMAGE_ROW_STRIDE(img);
    png_size_t size = PNG_IMAGE_BUFFER_SIZE(img, stride);
    unsigned char *buf = malloc(size);
    if (!buf) { png_image_free(&img); return NULL; }
    if (png_image_finish_read(&img, NULL, buf, (png_uint_32)stride, NULL) == 0) {
        fprintf(stderr, "png finish failed: %s\n", img.message);
        free(buf);
        png_image_free(&img);
        return NULL;
    }
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(
        0, (int)img.width, (int)img.height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) { free(buf); png_image_free(&img); return NULL; }
    if (SDL_MUSTLOCK(s)) SDL_LockSurface(s);
    for (png_uint_32 y = 0; y < img.height; y++) {
        memcpy((unsigned char *)s->pixels + (size_t)y * s->pitch,
               buf + (size_t)y * stride, (size_t)img.width * 4u);
    }
    if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
    free(buf);
    png_image_free(&img);
    return s;
}
