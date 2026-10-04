#include "app_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t parse_size(const char *v, size_t dflt) {
    if (!v || !*v) return dflt;
    char *end = NULL;
    unsigned long long n = strtoull(v, &end, 10);
    if (end && *end) {
        if (*end == 'M' || *end == 'm') n *= 1024ULL * 1024ULL;
        else if (*end == 'K' || *end == 'k') n *= 1024ULL;
        else if (*end == 'G' || *end == 'g') n *= 1024ULL * 1024ULL * 1024ULL;
    }
    return (size_t)n;
}

void app_config_load(AppConfig *c) {
    memset(c, 0, sizeof(*c));
    const char *h = getenv("LAYOUT_SERVER_HOST");
    snprintf(c->host, sizeof(c->host), "%s", h ? h : "127.0.0.1");
    const char *p = getenv("LAYOUT_SERVER_PORT");
    snprintf(c->port, sizeof(c->port), "%s", p ? p : "8080");
    const char *d = getenv("LAYOUT_DEVICE_ID");
    snprintf(c->device_id, sizeof(c->device_id), "%s", d ? d : "display-01");
    const char *img = getenv("LAYOUT_DEFAULT_IMAGE");
    snprintf(c->default_image, sizeof(c->default_image), "%s",
             img ? img : "assets/background.png");
    c->vram_budget = parse_size(getenv("LAYOUT_VRAM_BUDGET"),
                                96ULL * 1024ULL * 1024ULL);
    c->http_timeout_ms = 2000;
}
