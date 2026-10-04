#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "events.h"
#include "geometry.h"
#include "http_client.h"
#include "layout.h"
#include "reporter.h"
#include "scene.h"
#include "sim.h"
#include "window.h"

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

typedef struct {
    const char *device_id;
    const char *host;
    int port;
    int width, height, sim_dpr;
    int x, y;
    const char *config_file;   /* 离线模式：本地布局 JSON */
    const char *image_file;    /* 离线模式：本地图片 */
    const char *report_file;   /* 离线模式：上报写 JSONL */
    const char *script;
    int overlay;
} Args;

static AppWindow app;
static Scene scene;
static LayoutConfig cfg;
static EventQueue eq;
static Reporter reporter;
static Geometry geo;
static int g_rotation_override = -100; /* sim rotate */
static int dirty = 1;

static GeoRect hotzones[2];
static int n_hotzones = 0;

static void usage(void) {
    fprintf(stderr,
        "usage: visual-window-app [--device ID] [--host H] [--port P]\n"
        "  [--width W --height H] [--dpr F] [--x X --y Y]\n"
        "  [--config layout.json --image bg.png --report-file out.jsonl]\n"
        "  [--script sim.txt] [--no-overlay]\n");
}

static int parse_args(int argc, char **argv, Args *a) {
    memset(a, 0, sizeof(*a));
    a->device_id = "screen-A";
    a->host = "127.0.0.1";
    a->port = 8080;
    a->width = DEFAULT_WIDTH;
    a->height = DEFAULT_HEIGHT;
    a->sim_dpr = 1;
    a->x = a->y = -1;
    a->overlay = 1;
    for (int i = 1; i < argc; i++) {
        const char *k = argv[i];
        const char *v = (i + 1 < argc) ? argv[++i] : "";
        if (!strcmp(k, "--device")) a->device_id = v;
        else if (!strcmp(k, "--host")) a->host = v;
        else if (!strcmp(k, "--port")) a->port = atoi(v);
        else if (!strcmp(k, "--width")) a->width = atoi(v);
        else if (!strcmp(k, "--height")) a->height = atoi(v);
        else if (!strcmp(k, "--dpr")) a->sim_dpr = atoi(v);
        else if (!strcmp(k, "--x")) a->x = atoi(v);
        else if (!strcmp(k, "--y")) a->y = atoi(v);
        else if (!strcmp(k, "--config")) a->config_file = v;
        else if (!strcmp(k, "--image")) a->image_file = v;
        else if (!strcmp(k, "--report-file")) a->report_file = v;
        else if (!strcmp(k, "--script")) a->script = v;
        else if (!strcmp(k, "--no-overlay")) a->overlay = 0;
        else if (!strcmp(k, "--help") || !strcmp(k, "-h")) { usage(); return 1; }
        else { fprintf(stderr, "unknown arg: %s\n", k); usage(); return -1; }
    }
    return 0;
}

static unsigned char *read_file(const char *path, int *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    *len = (int)n;
    return b;
}

static void build_hotzones(void) {
    n_hotzones = 0;
    if (!scene.master) return;
    int iw = scene.master->w, ih = scene.master->h;
    /* 两个与分辨率无关的热区（源图归一化声明）：中心 1/2 区域、左下角 1/4 */
    hotzones[n_hotzones++] = (GeoRect){iw * 0.25, ih * 0.25, iw * 0.5, ih * 0.5};
    hotzones[n_hotzones++] = (GeoRect){0, ih * 0.75, iw * 0.25, ih * 0.25};
}

static void recompute_geometry(void) {
    geo.src_w = scene.master ? scene.master->w : 0;
    geo.src_h = scene.master ? scene.master->h : 0;
    if (app.rotation == 90 || app.rotation == 270) {
        geo.win_w = app.logical_h;  /* 竖屏 UI 视口（面板仍为横向物理屏） */
        geo.win_h = app.logical_w;
    } else {
        geo.win_w = app.logical_w;
        geo.win_h = app.logical_h;
    }
    geo.dpr = app.dpr;
    geo.rotation = app.rotation;
    geo.fit = fit_mode_from_str(cfg.fit);
    geo.focus_x = cfg.focus_x;
    geo.focus_y = cfg.focus_y;
    geometry_compute(&geo);
}

static void write_report_offline(const Geometry *g) {
    const char *path = getenv("REPORT_FILE");
    if (!path) return;
    FILE *f = fopen(path, "a");
    if (!f) return;
    double c[4];
    geo_crop_normalized(g, c);
    fprintf(f,
        "{\"device_id\":\"%s\",\"layout_version\":%d,\"image_rev\":%d,"
        "\"physical\":[%d,%d],\"logical\":[%d,%d],\"dpr\":%.4f,"
        "\"rotation\":%d,\"fit\":\"%s\",\"crop\":[%.6f,%.6f,%.6f,%.6f],"
        "\"focus\":[%.4f,%.4f],\"letterbox\":{\"top\":%.3f,\"left\":%.3f,"
        "\"bottom\":%.3f,\"right\":%.3f},\"degenerate\":%s}\n",
        "offline", cfg.version, cfg.image_rev,
        g->phys_w, g->phys_h, app.logical_w, app.logical_h, g->dpr,
        g->rotation, fit_mode_to_str(g->fit), c[0], c[1], c[2], c[3],
        cfg.focus_x, cfg.focus_y, g->lb_top, g->lb_left, g->lb_bottom,
        g->lb_right, g->degenerate ? "true" : "false");
    fclose(f);
}

static void apply_config_image(int force) {
    (void)force;
    if (cfg.image_rev == scene.master_rev) return;
    int len = 0;
    unsigned char *png = NULL;
    png = reporter_fetch_image(&reporter, &cfg, &len);
    if (png) {
        if (scene_set_image(&scene, app.renderer, &cfg, png, len) == 0)
            build_hotzones();
        free(png);
    }
}

static Uint32 push_timer(Uint32 interval, void *p) { (void)p; return interval; }

static void do_shot_marker(const char *path) {
    SDL_RenderPresent(app.renderer);
    int wx, wy;
    SDL_GetWindowPosition(app.window, &wx, &wy);
    int ww, wh;
    SDL_GetWindowSize(app.window, &ww, &wh);
    FILE *f = fopen(path, "w");
    if (f) {
        /* 窗口几何（物理像素）+ 渲染几何，供验收工具精确裁窗，不靠亮度猜 */
        fprintf(f, "ready %ld\n", (long)time(NULL));
        fprintf(f, "window %d %d %d %d\n", wx, wy, ww, wh);
        fprintf(f, "logical %d %d\n", app.logical_w, app.logical_h);
        fprintf(f, "dpr %.4f rotation %d fit %s\n", app.dpr, app.rotation, cfg.fit);
        fprintf(f, "phys %d %d\n", geo.phys_w, geo.phys_h);
        fclose(f);
    }
}

int main(int argc, char **argv) {
    Args a;
    int pa = parse_args(argc, argv, &a);
    if (pa > 0) return 0;
    if (pa < 0) return 2;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    layout_set_defaults(&cfg);
    strncpy(cfg.layout_id, a.device_id, sizeof(cfg.layout_id) - 1);

    if (!window_init(&app, "Visual Window App", a.width, a.height,
                     a.sim_dpr, a.x, a.y)) {
        SDL_Quit();
        return 1;
    }

    scene_init(&scene, app.renderer, &cfg);
    event_queue_init(&eq);
    reporter_init(&reporter, a.host, a.port, a.device_id, 500);

    int offline = a.config_file != NULL;
    if (offline) {
        int clen = 0;
        unsigned char *cbuf = read_file(a.config_file, &clen);
        if (cbuf) {
            char err[128] = {0};
            if (!layout_parse(&cfg, (const char *)cbuf, err, sizeof(err)))
                fprintf(stderr, "offline config parse: %s\n", err);
            free(cbuf);
        }
        int ilen = 0;
        unsigned char *ibuf = read_file(a.image_file ? a.image_file :
                                        "assets/background.png", &ilen);
        if (ibuf) {
            scene_set_image(&scene, app.renderer, &cfg, ibuf, ilen);
            free(ibuf);
            build_hotzones();
        }
        if (a.report_file) setenv("REPORT_FILE", a.report_file, 1);
    } else {
        /* 首次同步拉取布局 + 图片 */
        reporter_poll_layout(&reporter, &cfg);
        apply_config_image(1);
        build_hotzones();
    }

    if (cfg.orientation >= 0) {
        app.rotation = ((cfg.orientation % 360) + 360) % 360;
        g_rotation_override = app.rotation;
    }
    recompute_geometry();

    SimBridge sim = {app.sim_event_type};
    if (a.script) sim_start(&sim, a.script);

    Uint32 last_poll = SDL_GetTicks();
    Uint32 last_settled_report = 0;
    int last_reported_w = -1, last_reported_h = -1;
    int last_reported_fit = -1;
    int geometry_changed = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            } else if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
                else if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                         e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    int pw = e.window.data1, ph = e.window.data2;
                    int lw = (int)(pw / app.dpr + 0.5);
                    int lh = (int)(ph / app.dpr + 0.5);
                    app.logical_w = lw > 0 ? lw : pw;
                    app.logical_h = lh > 0 ? lh : ph;
                    int di = SDL_GetWindowDisplayIndex(app.window);
                    /* 携带当前配置 epoch 入队；旧模式的迟到事件将被丢弃 */
                    event_queue_push_size(&eq, app.logical_w, app.logical_h,
                                          di >= 0 ? di : 0);
                    dirty = 1;
                } else if (e.window.event == SDL_WINDOWEVENT_MOVED) {
                    int di = SDL_GetWindowDisplayIndex(app.window);
                    if (di >= 0) app.display_index = di;
                    dirty = 1;
                }
            } else if (e.type == app.sim_event_type) {
                SimEvent *s = (SimEvent *)e.user.data1;
                switch (s->kind) {
                    case SIM_SIZE:
                        if (s->a > 0 && s->b > 0) {
                            /* s->a/b 为物理面板像素；窗口直接设为该物理尺寸，
                             * 逻辑面板 = 物理/dpr。 */
                            SDL_SetWindowSize(app.window, s->a, s->b);
                            app.logical_w = (int)(s->a / app.dpr + 0.5);
                            app.logical_h = (int)(s->b / app.dpr + 0.5);
                            event_queue_push_size(&eq, app.logical_w,
                                                  app.logical_h, s->c);
                            dirty = 1;
                        }
                        break;
                    case SIM_ROTATE:
                        window_apply_rotation(&app, s->a);
                        g_rotation_override = s->a;
                        /* 面板尺寸不变；仅视口逻辑互换 -> 重算几何重绘 */
                        recompute_geometry();
                        dirty = 1;
                        break;
                    case SIM_MOVE:
                        SDL_SetWindowPosition(app.window, s->a, s->b);
                        break;
                    case SIM_TAP: {
                        recompute_geometry();
                        const GeoRect *z = scene_pick(&scene, &geo,
                                                      s->a, s->b,
                                                      hotzones, n_hotzones);
                        const char *hp = getenv("HIT_LOG");
                        if (hp) {
                            FILE *f = fopen(hp, "a");
                            if (f) {
                                fprintf(f, "tap %d %d -> %s\n", s->a, s->b,
                                        z ? "HIT" : "MISS");
                                fclose(f);
                            }
                        }
                        break;
                    }
                    case SIM_SHOT:
                        recompute_geometry();
                        scene_render(&scene, app.renderer, &geo, hotzones,
                                     n_hotzones, a.overlay);
                        do_shot_marker(s->text[0] ? s->text : "/tmp/shot.ready");
                        break;
                    case SIM_PUBLISH: {
                        /* e2e 钩子：要求后端在缩放过程中发布新图（rev+1） */
                        HttpResponse r = {0};
                        char path[160];
                        snprintf(path, sizeof(path),
                                 "/api/test/publish?device_id=%s", a.device_id);
                        http_post(&reporter.http, path, "application/json",
                                  "{}", &r);
                        http_response_free(&r);
                        break;
                    }
                    case SIM_QUIT:
                        running = 0;
                        break;
                }
                free(s);
            }
        }

        /* 合并后的尺寸事件：只有与当前 epoch 匹配才应用，然后重算+渲染+（防抖）上报 */
        SizeEvent snap;
        if (event_queue_drain(&eq, &snap)) {
            app.logical_w = snap.width;
            app.logical_h = snap.height;
            app.display_index = snap.display_index;
            recompute_geometry();
            if (app.logical_w != last_reported_w ||
                app.logical_h != last_reported_h ||
                (int)geo.fit != last_reported_fit) {
                geometry_changed = 1;
                last_settled_report = SDL_GetTicks();
            }
            dirty = 1;
            if (offline) {
                if (getenv("REPORT_FILE")) write_report_offline(&geo);
            } else {
                /* 退化（极窄/极矮）这类关键状态必须立即上报，不能被防抖吞掉；
                 * 普通连续 resize 走 debounce，最终停止后由定时器补发。 */
                reporter_report(&reporter, &geo, &cfg,
                                app.logical_w, app.logical_h,
                                geo.degenerate != 0);
            }
        }

        /* 后端轮询：新版本 = 用户最终选择，提交 epoch 使迟到尺寸事件失效 */
        if (!offline && SDL_GetTicks() - last_poll > 500) {
            last_poll = SDL_GetTicks();
            if (reporter_poll_layout(&reporter, &cfg)) {
                event_queue_commit_config(&eq);
                if (cfg.orientation >= 0 && g_rotation_override != cfg.orientation)
                    app.rotation = ((cfg.orientation % 360) + 360) % 360;
                else if (cfg.orientation < 0)
                    app.rotation = 0;
                g_rotation_override = cfg.orientation;
                apply_config_image(0);
                build_hotzones();
                recompute_geometry();
                dirty = 1;
                reporter_report(&reporter, &geo, &cfg,
                                app.logical_w, app.logical_h, true);
            } else if (cfg.image_rev != scene.master_rev) {
                apply_config_image(0);
                build_hotzones();
                recompute_geometry();
                dirty = 1;
                reporter_report(&reporter, &geo, &cfg,
                                app.logical_w, app.logical_h, true);
            }
        }

        /* 尺寸稳定 800ms 后强制补发最终几何（burst 合并也能把终点送到后端） */
        if (!offline && dirty == 0 && geometry_changed &&
            SDL_GetTicks() - last_settled_report > 800) {
            last_settled_report = SDL_GetTicks();
            reporter_report(&reporter, &geo, &cfg,
                            app.logical_w, app.logical_h, true);
            last_reported_w = app.logical_w;
            last_reported_h = app.logical_h;
            last_reported_fit = (int)geo.fit;
            geometry_changed = 0;
        }

        if (dirty) {
            recompute_geometry();
            scene_render(&scene, app.renderer, &geo, hotzones, n_hotzones,
                         a.overlay);
            SDL_RenderPresent(app.renderer);
            dirty = 0;
        }
        SDL_Delay(8);
        (void)push_timer;
    }

    if (!offline)
        reporter_flush(&reporter, &geo, &cfg, app.logical_w, app.logical_h);
    if (a.script) sim_stop();
    scene_destroy(&scene);
    window_destroy(&app);
    SDL_Quit();
    return 0;
}
