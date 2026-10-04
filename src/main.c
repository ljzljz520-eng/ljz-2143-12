/*
 * main.c - C 展示端主循环
 *
 * 关键正确性点：
 *  1) 尺寸事件只置 dirty，快速拖动期间合并为一次重绘（coalesce）；
 *  2) 用户的模式/焦点选择带单调递增的 choice_seq：保存请求基于编辑开始时
 *     拿到的配置生成，迟到的 resize 只重绘、绝不能改写模式；
 *  3) 后台轮询到更新的远程版本 => 标记冲突并提示，未保存选择不被覆盖；
 *     用户显式确认后才拉取新版本（或强制保存）；
 *  4) 撤销/重做只在本次编辑会话内，不越过本次编辑基础 (baseline)；
 *  5) 触摸/鼠标命中经 物理->逻辑->源图像 反算，与视觉位置一致。
 *
 * 快捷键：
 *   m 切换 铺满(cover)/完整(contain)
 *   r 旋转 0/90/180/270      d 循环模拟 DPR 1/1.5/2/3
 *   f 焦点回到画面中心        方向键 微调焦点
 *   s 保存到后端（乐观锁）    u 撤销   U(Ctrl+u 亦可用，这里用 i) 重做
 *   g 拉取远程版本（冲突确认） D 切换调试覆盖层
 */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "app_config.h"
#include "client_api.h"
#include "geometry.h"
#include "http_client.h"
#include "json.h"
#include "layout.h"
#include "renderer.h"
#include "window.h"

#define WINDOW_TITLE "Layout Debug Display"
#define BASE_WINDOW_W 1280
#define BASE_WINDOW_H 720
#define POLL_INTERVAL_MS 5000
#define REPORT_MIN_INTERVAL_MS 1000

static unsigned int g_choice_seq = 0; /* 用户显式选择代际 */
static unsigned int g_resize_seq = 0; /* 尺寸事件代际 */

typedef struct {
    AppConfig cfg;
    HttpClient api;
    AppWindow win;
    SceneRenderer scene;

    LayoutConfig layout;      /* 当前生效配置（用户选择优先） */
    bool have_remote;
    int remote_version;
    int base_version;         /* 本次编辑开始时的版本（保存乐观锁用） */
    bool remote_conflict;

    EditSession session;
    bool session_active;

    GPoint pointer_logical;
    bool pointer_valid;

    bool dirty;
    unsigned int last_poll_ms;
    unsigned int last_report_ms;
    GRect last_report_crop;
    int last_report_vp_w;
    int last_report_vp_h;
} App;

static unsigned int now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned int)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static bool init_sdl(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    int flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(flags) & flags) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        SDL_Quit();
        return false;
    }
    return true;
}

static void shutdown_sdl(void) {
    IMG_Quit();
    SDL_Quit();
}

static void begin_edit_session(App *app) {
    edit_session_begin(&app->session, &app->layout, app->remote_version);
    app->session_active = true;
    app->base_version = app->remote_version;
}

static void push_user_choice(App *app, const LayoutConfig *next) {
    /*
     * 用户显式选择：代际 +1。resize_seq 再大都无法回退该模式。
     * 保存时序列化的是当前 layout（即最新用户选择），而非任何迟到尺寸事件值。
     */
    g_choice_seq++;
    if (!app->session_active) begin_edit_session(app);
    edit_session_push(&app->session, next);
    app->layout = *next;
    app->dirty = true;
    printf("[choice seq=%u] mode=%s focus=(%.3f,%.3f)\n",
           g_choice_seq, layout_mode_str(next->mode),
           next->focus_x, next->focus_y);
}

static bool load_remote(App *app) {
    LayoutApiResult r = client_fetch_layout(&app->api);
    if (!r.ok) {
        fprintf(stderr, "fetch layout failed: %s（沿用本地配置）\n", r.error);
        return false;
    }
    app->layout = r.record.config;
    app->remote_version = r.record.version;
    app->base_version = r.record.version;
    app->have_remote = true;
    app->remote_conflict = false;
    begin_edit_session(app);

    /* 图像尺寸以后端记录为准；按 image_id 拼本地路径（容器内 assets 挂载） */
    char path[640];
    snprintf(path, sizeof(path), "assets/%s", app->layout.image_id);
    if (access(path, R_OK) != 0) {
        snprintf(path, sizeof(path), "%s", app->cfg.default_image);
    }
    if (!renderer_load_image(&app->scene, app->win.renderer, path,
                             app->layout.image_id, app->cfg.vram_budget)) {
        fprintf(stderr, "reload image failed: %s\n", path);
        return false;
    }
    printf("loaded remote layout version=%d image=%s\n",
           r.record.version, app->layout.image_id);
    app->dirty = true;
    return true;
}

static void save_layout(App *app) {
    char err[128];
    if (!layout_config_valid(&app->layout, err, sizeof(err))) {
        fprintf(stderr, "config invalid, not saving: %s\n", err);
        return;
    }
    LayoutApiResult r = client_save_layout(&app->api, &app->layout,
                                           app->base_version);
    if (r.ok) {
        app->remote_version = r.record.version;
        app->base_version = r.record.version;
        app->remote_conflict = false;
        /* 保存成功：以当前结果作为新的本次编辑基础 */
        begin_edit_session(app);
        printf("saved as version=%d (choice seq=%u preserved)\n",
               r.record.version, g_choice_seq);
    } else if (r.status == 409) {
        app->remote_conflict = true;
        fprintf(stderr,
                "!! 保存冲突：远程已更新到 version=%d，你的选择(seq=%u)未被覆盖。\n"
                "   按 g 查看远程新版本并基于它重做，或再次按 s 强制覆盖。\n",
                r.server_version, g_choice_seq);
    } else {
        fprintf(stderr, "save failed: %s（本地选择保留）\n", r.error);
    }
}

static void force_save_layout(App *app) {
    /* 用户在冲突提示后显式强制：用服务端当前版本作为基线重试一次 */
    LayoutApiResult latest = client_fetch_layout(&app->api);
    if (latest.ok) {
        app->base_version = latest.record.version;
    }
    save_layout(app);
}

static void poll_remote(App *app, bool force) {
    unsigned int t = now_ms();
    if (!force && t - app->last_poll_ms < POLL_INTERVAL_MS) return;
    app->last_poll_ms = t;
    LayoutApiResult r = client_fetch_layout(&app->api);
    if (!r.ok) return;
    if (r.record.version > app->remote_version) {
        if (app->session_active &&
            edit_session_current(&app->session) != edit_session_baseline(&app->session)) {
            /* 有未保存编辑：只提示，不覆盖用户最终选择 */
            app->remote_conflict = true;
            edit_session_notify_remote(&app->session, r.record.version);
            fprintf(stderr,
                    "!! 远程有新版本 v%d（本地基线 v%d，当前未保存选择 seq=%u）。\n"
                    "   按 g 放弃本地改动并加载新版本；按 s 保留选择并强制保存。\n",
                    r.record.version, app->base_version, g_choice_seq);
        } else {
            app->layout = r.record.config;
            app->remote_version = r.record.version;
            app->base_version = r.record.version;
            app->remote_conflict = false;
            begin_edit_session(app);
            char path[640];
            snprintf(path, sizeof(path), "assets/%s", app->layout.image_id);
            if (access(path, R_OK) != 0) {
                snprintf(path, sizeof(path), "%s", app->cfg.default_image);
            }
            renderer_load_image(&app->scene, app->win.renderer, path,
                                app->layout.image_id, app->cfg.vram_budget);
            app->dirty = true;
            printf("applied remote update version=%d\n", r.record.version);
        }
    }
}

static void report_crop(App *app, const LayoutTransform *t, bool force) {
    unsigned int tm = now_ms();
    if (!force && tm - app->last_report_ms < REPORT_MIN_INTERVAL_MS) return;
    GRect crop = layout_reported_crop(&app->layout, t);
    if (!force &&
        (int)crop.x == (int)app->last_report_crop.x &&
        (int)crop.y == (int)app->last_report_crop.y &&
        (int)crop.w == (int)app->last_report_crop.w &&
        (int)crop.h == (int)app->last_report_crop.h &&
        app->win.logical_w == app->last_report_vp_w &&
        app->win.logical_h == app->last_report_vp_h) {
        return;
    }
    bool ok = client_report_crop(&app->api, app->cfg.device_id, &app->layout,
                                 &crop, gsize((double)app->win.logical_w,
                                              (double)app->win.logical_h),
                                 app->win.dpr, app->win.rotation);
    if (ok) {
        app->last_report_crop = crop;
        app->last_report_vp_w = app->win.logical_w;
        app->last_report_vp_h = app->win.logical_h;
        app->last_report_ms = tm;
    }
}

static void do_draw(App *app) {
    LayoutTransform t = renderer_draw(
        &app->scene, app->win.renderer,
        app->win.logical_w, app->win.logical_h,
        app->win.dpr, app->win.rotation,
        &app->layout, app->pointer_logical, app->pointer_valid);
    app->dirty = false;
    report_crop(app, &t, false);
}

static void nudge_focus(LayoutConfig *c, double dx, double dy) {
    c->focus_x += dx;
    c->focus_y += dy;
    if (c->focus_x < 0.02) c->focus_x = 0.02;
    if (c->focus_x > 0.98) c->focus_x = 0.98;
    if (c->focus_y < 0.02) c->focus_y = 0.02;
    if (c->focus_y > 0.98) c->focus_y = 0.98;
}

static void handle_key(App *app, SDL_Keycode key) {
    LayoutConfig next = app->layout;
    bool changed = false;
    switch (key) {
        case SDLK_m:
            next.mode = (next.mode == LAYOUT_COVER) ? LAYOUT_CONTAIN : LAYOUT_COVER;
            changed = true;
            break;
        case SDLK_r:
            app->win.rotation = (Rotation)((app->win.rotation + 90) % 360);
            window_refresh_metrics(&app->win);
            app->dirty = true;
            break;
        case SDLK_d: {
            static const double dprs[] = {1.0, 1.5, 2.0, 3.0};
            static int di = 0;
            di = (di + 1) % 4;
            window_set_dpr(&app->win, dprs[di]);
            printf("simulated DPR=%.1f logical=%dx%d phys=%dx%d\n",
                   app->win.dpr, app->win.logical_w, app->win.logical_h,
                   app->win.phys_w, app->win.phys_h);
            app->dirty = true;
            break;
        }
        case SDLK_f:
            next.focus_x = 0.5;
            next.focus_y = 0.5;
            changed = true;
            break;
        case SDLK_LEFT:  nudge_focus(&next, -0.03, 0); changed = true; break;
        case SDLK_RIGHT: nudge_focus(&next, 0.03, 0); changed = true; break;
        case SDLK_UP:    nudge_focus(&next, 0, -0.03); changed = true; break;
        case SDLK_DOWN:  nudge_focus(&next, 0, 0.03); changed = true; break;
        case SDLK_s:
            if (app->remote_conflict) force_save_layout(app);
            else save_layout(app);
            break;
        case SDLK_u: {
            LayoutConfig out;
            if (edit_session_undo(&app->session, &out)) {
                g_choice_seq++;
                app->layout = out;
                app->dirty = true;
                printf("undo -> mode=%s focus=(%.3f,%.3f)\n",
                       layout_mode_str(out.mode), out.focus_x, out.focus_y);
            } else {
                printf("undo: 已到本次编辑基础，不能再撤销（不跨会话）\n");
            }
            break;
        }
        case SDLK_i: {
            LayoutConfig out;
            if (edit_session_redo(&app->session, &out)) {
                g_choice_seq++;
                app->layout = out;
                app->dirty = true;
                printf("redo -> mode=%s focus=(%.3f,%.3f)\n",
                       layout_mode_str(out.mode), out.focus_x, out.focus_y);
            }
            break;
        }
        case SDLK_g:
            if (load_remote(app)) {
                app->remote_conflict = false;
            }
            break;
        case SDLK_q:
            if (app->remote_conflict) {
                /* 忽略远程：清除冲突标记，继续用本地选择 */
                app->remote_conflict = false;
                printf("已忽略远程更新，继续使用本地选择 seq=%u\n", g_choice_seq);
            }
            break;
        case SDLK_F1:
            app->scene.show_debug = !app->scene.show_debug;
            app->dirty = true;
            break;
        default:
            break;
    }
    if (changed) {
        push_user_choice(app, &next);
    }
}

static void update_pointer(App *app, int px, int py) {
    /* SDL 鼠标事件给的是逻辑坐标（SetLogicalSize 后）；触摸原始坐标这里同样视为
       物理，先演示物理->逻辑的完整反算（取窗口可绘制尺寸比例）。 */
    int ww = 0, wh = 0;
    SDL_GetWindowSize(app->win.window, &ww, &wh);
    GSize phys;
    if (ww > 0 && wh > 0) {
        phys = gsize((double)px / (double)ww * (double)app->win.phys_w,
                     (double)py / (double)wh * (double)app->win.phys_h);
    } else {
        phys = gsize((double)px, (double)py);
    }
    GAffine inv = g_physical_to_logical(
        gsize((double)app->win.logical_w, (double)app->win.logical_h),
        app->win.dpr, app->win.rotation);
    app->pointer_logical = g_affine_apply(&inv, gpoint(phys.w, phys.h));
    app->pointer_valid = true;
    app->dirty = true;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (!init_sdl()) return 1;

    App app;
    memset(&app, 0, sizeof(app));
    app_config_load(&app.cfg);
    app.api.host = app.cfg.host;
    app.api.port = app.cfg.port;
    app.api.timeout_ms = app.cfg.http_timeout_ms;
    app.last_poll_ms = now_ms();
    app.last_report_ms = now_ms();

    if (!window_init(&app.win, WINDOW_TITLE, BASE_WINDOW_W, BASE_WINDOW_H)) {
        shutdown_sdl();
        return 1;
    }

    /* 本地兜底配置 */
    layout_config_init(&app.layout, "background.png", 1920, 1080);
    if (!renderer_load_image(&app.scene, app.win.renderer,
                             app.cfg.default_image, "background.png",
                             app.cfg.vram_budget)) {
        renderer_destroy(&app.scene);
        window_destroy(&app.win);
        shutdown_sdl();
        return 1;
    }
    layout_config_init(&app.layout, "background.png",
                       app.scene.image_w, app.scene.image_h);
    begin_edit_session(&app);

    /* 尝试拉取后端；失败不致命，仍可离线展示/调试 */
    if (load_remote(&app)) {
        app.dirty = true;
    }

    bool running = true;
    app.dirty = true;
    while (running) {
        SDL_Event event;
        bool had_resize = false;
        /*
         * 抽干事件队列：快速拖动窗口会产生大量 SIZE_CHANGED，
         * 这里只合并成 dirty + 一次 metrics 刷新，避免逐事件重绘。
         * resize 不触碰 layout.mode（g_resize_seq 仅用于观测）。
         */
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                        running = false;
                    } else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                               event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        g_resize_seq++;
                        had_resize = true;
                        app.dirty = true;
                    }
                    break;
                case SDL_KEYDOWN:
                    handle_key(&app, event.key.keysym.sym);
                    break;
                case SDL_MOUSEMOTION:
                case SDL_MOUSEBUTTONDOWN:
                    update_pointer(&app, event.button.x, event.button.y);
                    break;
                default:
                    break;
            }
        }
        if (had_resize) {
            window_refresh_metrics(&app.win);
            /* 迟到尺寸事件只重绘，绝不覆盖用户模式：下面 draw 读的是 app.layout */
        }

        if (app.dirty) {
            do_draw(&app);
        }
        poll_remote(&app, false);

        SDL_Delay(16);
    }

    /* 退出前强制上报一次最终裁切区域 */
    LayoutTransform final_t = layout_resolve(
        &app.layout,
        gsize((double)app.win.logical_w, (double)app.win.logical_h));
    report_crop(&app, &final_t, true);

    renderer_destroy(&app.scene);
    window_destroy(&app.win);
    shutdown_sdl();
    return 0;
}
