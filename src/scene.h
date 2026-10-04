#ifndef SCENE_H
#define SCENE_H

#include <SDL2/SDL.h>

#include "geometry.h"
#include "layout.h"
#include "texcache.h"

typedef struct {
    TexCache cache;
    SDL_Surface *master;
    int master_rev;
    /* logical: 逻辑视口绘制目标 pw×ph（TARGET）；rotated: 旋转输出 ph×pw（STREAMING） */
    SDL_Texture *logical;
    SDL_Texture *rotated;
    int logical_w, logical_h;
    int rotated_w, rotated_h;
} Scene;

void scene_init(Scene *s, SDL_Renderer *r, const LayoutConfig *cfg);
void scene_destroy(Scene *s);

int scene_set_image(Scene *s, SDL_Renderer *r, const LayoutConfig *cfg,
                    const unsigned char *png, int len);

void scene_render(Scene *s, SDL_Renderer *r, const Geometry *g,
                  const GeoRect *hotzones, int n_hotzones, int show_overlay);

const GeoRect *scene_pick(Scene *s, const Geometry *g, double px, double py,
                          const GeoRect *hotzones, int n);

#endif
