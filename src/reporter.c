#include "reporter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_mini.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void reporter_init(Reporter *r, const char *host, int port,
                   const char *device_id, int debounce_ms) {
    http_init(&r->http, host, port, 1500);
    snprintf(r->device_id, sizeof(r->device_id), "%s", device_id);
    r->last_version = 0;
    r->last_image_rev = -1;
    r->debounce_ms = debounce_ms > 0 ? debounce_ms : 500;
    memset(&r->last_report, 0, sizeof(r->last_report));
    r->online = 0;
}

bool reporter_poll_layout(Reporter *r, LayoutConfig *cfg) {
    char path[700];
    snprintf(path, sizeof(path),
             "/api/layout?device_id=%s&since=%d", r->device_id, r->last_version);
    HttpResponse resp = {0};
    if (!http_get(&r->http, path, &resp)) {
        r->online = 0;
        return false;
    }
    r->online = 1;
    bool changed = false;
    if (resp.status == 200 && resp.body) {
        char err[128] = {0};
        JsonNode *root = json_parse(resp.body);
        if (root) {
            const JsonNode *d = json_get(root, "data");
            if (!d) d = root;
            int newver = (int)json_get_num(d, "version", cfg->version);
            if (newver > r->last_version) {
                if (layout_parse(cfg, resp.body, err, sizeof(err))) {
                    r->last_version = cfg->version;
                    changed = true;
                } else {
                    fprintf(stderr, "layout rejected: %s\n", err);
                }
            }
            json_free(root);
        }
    }
    http_response_free(&resp);
    return changed;
}

unsigned char *reporter_fetch_image(Reporter *r, const LayoutConfig *cfg,
                                    int *out_len) {
    char path[700];
    if (cfg->image_url[0] == '/')
        snprintf(path, sizeof(path), "%s?device_id=%s&rev=%d",
                 cfg->image_url, r->device_id, cfg->image_rev);
    else
        snprintf(path, sizeof(path), "/api/images/current?device_id=%s&rev=%d",
                 r->device_id, cfg->image_rev);
    HttpResponse resp = {0};
    if (!http_get(&r->http, path, &resp) || resp.status != 200 || !resp.body) {
        http_response_free(&resp);
        return NULL;
    }
    unsigned char *buf = malloc(resp.size + 1);
    memcpy(buf, resp.body, resp.size);
    buf[resp.size] = 0;
    *out_len = (int)resp.size;
    http_response_free(&resp);
    return buf;
}

static void send_report(Reporter *r, const Geometry *g, const LayoutConfig *cfg,
                        int logical_w, int logical_h) {
    double crop[4];
    geo_crop_normalized(g, crop);
    JsonNode *o = json_obj();
    obj_set(o, "device_id", json_str(r->device_id));
    obj_set(o, "layout_version", json_num(cfg->version));
    obj_set(o, "image_rev", json_num(cfg->image_rev));
    JsonNode *phys = json_arr();
    arr_push(phys, json_num(g->phys_w));
    arr_push(phys, json_num(g->phys_h));
    obj_set(o, "physical", phys);
    JsonNode *log = json_arr();
    arr_push(log, json_num(logical_w));
    arr_push(log, json_num(logical_h));
    obj_set(o, "logical", log);
    obj_set(o, "dpr", json_num(g->dpr));
    obj_set(o, "rotation", json_num(g->rotation));
    obj_set(o, "fit", json_str(fit_mode_to_str(g->fit)));
    JsonNode *c = json_arr();
    for (int i = 0; i < 4; i++) arr_push(c, json_num(crop[i]));
    obj_set(o, "crop", c);
    JsonNode *f = json_arr();
    arr_push(f, json_num(cfg->focus_x));
    arr_push(f, json_num(cfg->focus_y));
    obj_set(o, "focus", f);
    JsonNode *lb = json_obj();
    obj_set(lb, "top", json_num(g->lb_top));
    obj_set(lb, "left", json_num(g->lb_left));
    obj_set(lb, "bottom", json_num(g->lb_bottom));
    obj_set(lb, "right", json_num(g->lb_right));
    obj_set(o, "letterbox", lb);
    obj_set(o, "degenerate", json_bool(g->degenerate != 0));
    char *body = json_dump(o);
    HttpResponse resp = {0};
    if (http_post(&r->http, "/api/reports", "application/json", body, &resp)) {
        r->online = 1;
        if (resp.status != 200 && resp.status != 204)
            fprintf(stderr, "report status %d\n", resp.status);
    } else {
        r->online = 0;
    }
    http_response_free(&resp);
    free(body);
    json_free(o);
}

void reporter_report(Reporter *r, const Geometry *g, const LayoutConfig *cfg,
                     int logical_w, int logical_h, bool force) {
    double t = now_ms();
    double last = r->last_report.tv_sec * 1000.0 +
                  r->last_report.tv_nsec / 1e6;
    if (!force && t - last < r->debounce_ms) return;
    clock_gettime(CLOCK_MONOTONIC, &r->last_report);
    send_report(r, g, cfg, logical_w, logical_h);
}

void reporter_flush(Reporter *r, const Geometry *g, const LayoutConfig *cfg,
                    int logical_w, int logical_h) {
    send_report(r, g, cfg, logical_w, logical_h);
}
