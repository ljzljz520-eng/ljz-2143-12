#include "layout.h"

#include <stdio.h>
#include <string.h>

void layout_set_defaults(LayoutConfig *c) {
    memset(c, 0, sizeof(*c));
    snprintf(c->layout_id, sizeof(c->layout_id), "default");
    c->version = 1;
    c->image_rev = 1;
    snprintf(c->image_url, sizeof(c->image_url), "/api/images/current");
    snprintf(c->fit, sizeof(c->fit), "cover");
    c->focus_x = c->focus_y = 0.5;
    c->orientation = -1;
    snprintf(c->texture_strategy, sizeof(c->texture_strategy), "atlas");
    c->budget_bytes_mb = 32;
    c->budget_entries = 16;
    c->min_bucket = 128;
    c->max_bucket = 2048;
    c->bg_rgba = 0xFF000000u;
}

static void copy_str(char *dst, int sz, const char *src) {
    if (!src) return;
    snprintf(dst, sz, "%s", src);
}

TexStrategy layout_strategy(const LayoutConfig *c) {
    return strcmp(c->texture_strategy, "runtime") == 0 ? TEX_RUNTIME : TEX_ATLAS;
}

bool layout_parse(LayoutConfig *cfg, const char *body, char *err, int err_sz) {
    JsonNode *root = json_parse(body);
    if (!root) {
        snprintf(err, err_sz, "invalid json");
        return false;
    }
    const JsonNode *d = root;
    if (root->type == J_OBJ) {
        const JsonNode *inner = json_get(root, "data");
        if (inner) d = inner;
    }
    if (d->type != J_OBJ) {
        snprintf(err, err_sz, "layout payload not object");
        json_free(root);
        return false;
    }

    const char *fit = json_get_str(d, "fit", NULL);
    if (fit) {
        if (strcmp(fit, "cover") && strcmp(fit, "contain") && strcmp(fit, "stretch")) {
            snprintf(err, err_sz, "invalid fit: %s", fit);
            json_free(root);
            return false;
        }
        copy_str(cfg->fit, sizeof(cfg->fit), fit);
    }
    const char *strat = json_get_str(d, "texture_strategy", NULL);
    if (strat) {
        if (strcmp(strat, "atlas") && strcmp(strat, "runtime")) {
            snprintf(err, err_sz, "invalid texture_strategy: %s", strat);
            json_free(root);
            return false;
        }
        copy_str(cfg->texture_strategy, sizeof(cfg->texture_strategy), strat);
    }
    const char *lid = json_get_str(d, "layout_id", NULL);
    if (lid) copy_str(cfg->layout_id, sizeof(cfg->layout_id), lid);
    const char *url = json_get_str(d, "image_url", NULL);
    if (url) copy_str(cfg->image_url, sizeof(cfg->image_url), url);

    const JsonNode *v = json_get(d, "version");
    if (v && v->type == J_NUM) cfg->version = (int)v->num;
    const JsonNode *rev = json_get(d, "image_rev");
    if (rev && rev->type == J_NUM) cfg->image_rev = (int)rev->num;

    const JsonNode *f = json_get(d, "focus");
    if (f && f->type == J_ARR && f->count >= 2) {
        cfg->focus_x = f->items[0]->num;
        cfg->focus_y = f->items[1]->num;
    } else {
        const JsonNode *fx = json_get(d, "focus_x");
        const JsonNode *fy = json_get(d, "focus_y");
        if (fx && fx->type == J_NUM) cfg->focus_x = fx->num;
        if (fy && fy->type == J_NUM) cfg->focus_y = fy->num;
    }
    const JsonNode *o = json_get(d, "orientation");
    if (o && o->type == J_NUM) {
        int val = (int)o->num;
        if (val == -1 || val == 0 || val == 90 || val == 180 || val == 270)
            cfg->orientation = val;
        else {
            snprintf(err, err_sz, "invalid orientation: %d", val);
            json_free(root);
            return false;
        }
    }
    const JsonNode *b = json_get(d, "budget_bytes_mb");
    if (b && b->type == J_NUM && b->num > 0) cfg->budget_bytes_mb = (int)b->num;
    b = json_get(d, "budget_entries");
    if (b && b->type == J_NUM && b->num > 0) cfg->budget_entries = (int)b->num;
    b = json_get(d, "min_bucket");
    if (b && b->type == J_NUM && b->num > 0) cfg->min_bucket = (int)b->num;
    b = json_get(d, "max_bucket");
    if (b && b->type == J_NUM && b->num > 0) cfg->max_bucket = (int)b->num;
    b = json_get(d, "bg_color");
    if (b && b->type == J_NUM) cfg->bg_rgba = (unsigned int)b->num;

    json_free(root);
    return true;
}
