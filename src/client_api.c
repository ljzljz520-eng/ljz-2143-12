#include "client_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parse_api_error(LayoutApiResult *r, const HttpResponse *resp) {
    r->status = resp->status;
    if (resp->body) {
        JsonValue *root = json_parse(resp->body);
        if (root) {
            const char *msg = json_as_string(json_obj_get(root, "error"), "");
            snprintf(r->error, sizeof(r->error), "%s", msg);
            double sv = json_as_number(json_obj_get(root, "current_version"), -1);
            if (sv >= 0) r->server_version = (int)sv;
            json_free(root);
        }
    }
}

LayoutApiResult client_fetch_layout(const HttpClient *c) {
    LayoutApiResult r = {0};
    HttpResponse resp = http_request(c, "GET", "/api/layout/latest",
                                     "application/json", NULL, 0);
    if (resp.error) {
        r.ok = false;
        snprintf(r.error, sizeof(r.error), "%s", resp.errmsg);
        http_response_free(&resp);
        return r;
    }
    parse_api_error(&r, &resp);
    if (resp.status != 200 || !resp.body) {
        r.ok = false;
        if (!r.error[0]) snprintf(r.error, sizeof(r.error), "HTTP %d", resp.status);
        http_response_free(&resp);
        return r;
    }
    JsonValue *root = json_parse(resp.body);
    char err[128];
    if (root && layout_record_from_json(root, &r.record, err, sizeof(err))) {
        r.ok = true;
        r.server_version = r.record.version;
    } else {
        r.ok = false;
        snprintf(r.error, sizeof(r.error), "bad payload: %s", err);
    }
    json_free(root);
    http_response_free(&resp);
    return r;
}

LayoutApiResult client_save_layout(const HttpClient *c, const LayoutConfig *cfg,
                                   int base_version) {
    LayoutApiResult r = {0};
    char *body = layout_save_body(cfg, base_version, "c-display");
    size_t blen = strlen(body);
    HttpResponse resp = http_request(c, "PUT", "/api/layout",
                                     "application/json", body, blen);
    free(body);
    if (resp.error) {
        r.ok = false;
        snprintf(r.error, sizeof(r.error), "%s", resp.errmsg);
        http_response_free(&resp);
        return r;
    }
    parse_api_error(&r, &resp);
    if (resp.status == 200 && resp.body) {
        JsonValue *root = json_parse(resp.body);
        char err[128];
        if (root && layout_record_from_json(root, &r.record, err, sizeof(err))) {
            r.ok = true;
            r.server_version = r.record.version;
        }
        json_free(root);
    } else if (resp.status == 409) {
        r.ok = false; /* 冲突：调用方必须提示，不得覆盖 */
        if (!r.error[0]) snprintf(r.error, sizeof(r.error), "version conflict");
    } else {
        r.ok = false;
        if (!r.error[0]) snprintf(r.error, sizeof(r.error), "HTTP %d", resp.status);
    }
    http_response_free(&resp);
    return r;
}

bool client_report_crop(const HttpClient *c, const char *device_id,
                        const LayoutConfig *cfg, const GRect *crop,
                        GSize viewport, double dpr, Rotation rotation) {
    char *body = layout_report_body(device_id, cfg, crop, viewport, dpr, rotation);
    size_t blen = strlen(body);
    HttpResponse resp = http_request(c, "POST", "/api/reports/crop",
                                     "application/json", body, blen);
    free(body);
    bool ok = !resp.error && resp.status >= 200 && resp.status < 300;
    http_response_free(&resp);
    return ok;
}
