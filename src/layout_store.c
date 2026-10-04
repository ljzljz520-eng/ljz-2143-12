#include "layout_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void layout_record_init(LayoutRecord *r) {
    memset(r, 0, sizeof(*r));
}

static bool get_rotation(const JsonValue *cfgj, Rotation *out) {
    const JsonValue *rv = json_obj_get(cfgj, "rotation");
    double r = json_as_number(rv, 0);
    if (r == 90) { *out = ROT_90; return true; }
    if (r == 180) { *out = ROT_180; return true; }
    if (r == 270) { *out = ROT_270; return true; }
    if (r == 0) { *out = ROT_0; return true; }
    return false;
}

bool layout_config_from_json(const JsonValue *cfgj, LayoutConfig *out,
                             char *err, size_t errsz) {
    if (!cfgj || cfgj->type != J_OBJECT) {
        if (err) snprintf(err, errsz, "config missing or not object");
        return false;
    }
    LayoutConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    const char *id = json_as_string(json_obj_get(cfgj, "image_id"), "");
    snprintf(cfg.image_id, sizeof(cfg.image_id), "%s", id);
    cfg.image_w = (int)json_as_number(json_obj_get(cfgj, "image_w"), -1);
    cfg.image_h = (int)json_as_number(json_obj_get(cfgj, "image_h"), -1);
    const char *mode = json_as_string(json_obj_get(cfgj, "mode"), "cover");
    if (!layout_mode_parse(mode, &cfg.mode)) {
        if (err) snprintf(err, errsz, "invalid mode '%s'", mode);
        return false;
    }
    cfg.focus_x = json_as_number(json_obj_get(cfgj, "focus_x"), 0.5);
    cfg.focus_y = json_as_number(json_obj_get(cfgj, "focus_y"), 0.5);
    cfg.dpr_override = json_as_number(json_obj_get(cfgj, "dpr_override"), 0.0);
    if (!get_rotation(cfgj, &cfg.rotation)) {
        if (err) snprintf(err, errsz, "invalid rotation");
        return false;
    }
    char verr[128];
    if (!layout_config_valid(&cfg, verr, sizeof(verr))) {
        if (err) snprintf(err, errsz, "%s", verr);
        return false;
    }
    *out = cfg;
    return true;
}

bool layout_record_from_json(const JsonValue *root, LayoutRecord *out,
                             char *err, size_t errsz) {
    if (!root || root->type != J_OBJECT) {
        if (err) snprintf(err, errsz, "response not json object");
        return false;
    }
    LayoutRecord r;
    layout_record_init(&r);
    r.version = (int)json_as_number(json_obj_get(root, "version"), -1);
    const char *ts = json_as_string(json_obj_get(root, "updated_at"), "");
    snprintf(r.updated_at, sizeof(r.updated_at), "%s", ts);
    const char *cs = json_as_string(json_obj_get(root, "checksum"), "");
    snprintf(r.checksum, sizeof(r.checksum), "%s", cs);
    const JsonValue *cfgj = json_obj_get(root, "config");
    if (!layout_config_from_json(cfgj, &r.config, err, errsz)) {
        return false;
    }
    if (r.version < 0) {
        if (err) snprintf(err, errsz, "version missing");
        return false;
    }
    *out = r;
    return true;
}

static void append_config(JsonBuf *b, const LayoutConfig *cfg) {
    char num[64];
    jb_raw(b, "{");
    jb_key(b, "image_id"); jb_string(b, cfg->image_id); jb_raw(b, ",");
    jb_key(b, "image_w"); snprintf(num, sizeof(num), "%d", cfg->image_w); jb_raw(b, num); jb_raw(b, ",");
    jb_key(b, "image_h"); snprintf(num, sizeof(num), "%d", cfg->image_h); jb_raw(b, num); jb_raw(b, ",");
    jb_key(b, "mode"); jb_string(b, layout_mode_str(cfg->mode)); jb_raw(b, ",");
    snprintf(num, sizeof(num), "%.6f", cfg->focus_x);
    jb_key(b, "focus_x"); jb_raw(b, num); jb_raw(b, ",");
    snprintf(num, sizeof(num), "%.6f", cfg->focus_y);
    jb_key(b, "focus_y"); jb_raw(b, num); jb_raw(b, ",");
    snprintf(num, sizeof(num), "%d", (int)cfg->rotation);
    jb_key(b, "rotation"); jb_raw(b, num); jb_raw(b, ",");
    snprintf(num, sizeof(num), "%.3f", cfg->dpr_override);
    jb_key(b, "dpr_override"); jb_raw(b, num);
    jb_raw(b, "}");
}

char *layout_save_body(const LayoutConfig *cfg, int base_version, const char *client) {
    JsonBuf b;
    jb_init(&b);
    char num[32];
    jb_raw(&b, "{");
    jb_key(&b, "base_version");
    snprintf(num, sizeof(num), "%d", base_version);
    jb_raw(&b, num);
    jb_raw(&b, ",");
    jb_key(&b, "client");
    jb_string(&b, client ? client : "c-display");
    jb_raw(&b, ",");
    jb_key(&b, "config");
    append_config(&b, cfg);
    jb_raw(&b, "}");
    return b.data;
}

char *layout_report_body(const char *device_id, const LayoutConfig *cfg,
                         const GRect *crop, GSize viewport, double dpr,
                         Rotation applied_rotation) {
    JsonBuf b;
    jb_init(&b);
    char num[64];
    jb_raw(&b, "{");
    jb_key(&b, "device_id"); jb_string(&b, device_id ? device_id : "unknown");
    jb_raw(&b, ","); jb_key(&b, "image_id"); jb_string(&b, cfg->image_id);
    jb_raw(&b, ","); jb_key(&b, "mode"); jb_string(&b, layout_mode_str(cfg->mode));
    jb_raw(&b, ","); jb_key(&b, "crop");
    snprintf(num, sizeof(num), "{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
             (int)(crop->x + 0.5), (int)(crop->y + 0.5),
             (int)(crop->w + 0.5), (int)(crop->h + 0.5));
    jb_raw(&b, num);
    jb_raw(&b, ","); jb_key(&b, "viewport_logical");
    snprintf(num, sizeof(num), "{\"w\":%d,\"h\":%d}",
             (int)(viewport.w + 0.5), (int)(viewport.h + 0.5));
    jb_raw(&b, num);
    jb_raw(&b, ","); jb_key(&b, "dpr");
    snprintf(num, sizeof(num), "%.3f", dpr);
    jb_raw(&b, num);
    jb_raw(&b, ","); jb_key(&b, "rotation");
    snprintf(num, sizeof(num), "%d", (int)applied_rotation);
    jb_raw(&b, num);
    jb_raw(&b, "}");
    return b.data;
}
