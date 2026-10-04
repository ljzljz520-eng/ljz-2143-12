#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *body;
    size_t size;
    int status;
} HttpResponse;

typedef struct {
    char host[128];
    int  port;
    int  timeout_ms;
} HttpClient;

void http_init(HttpClient *c, const char *host, int port, int timeout_ms);
void http_response_free(HttpResponse *r);

/* GET path；200 时返回 true（其他状态码仍可在 r->status 检查）。 */
bool http_get(HttpClient *c, const char *path, HttpResponse *out);

/* POST path，content_type 例如 "application/json"。 */
bool http_post(HttpClient *c, const char *path, const char *content_type,
               const char *body, HttpResponse *out);

#endif
