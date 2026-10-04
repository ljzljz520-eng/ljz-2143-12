/* http_client.h - 极简 HTTP/1.1 客户端（POSIX，带连接/总超时） */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

typedef struct {
    int status;
    char *body;
    size_t body_len;
    int error; /* 0 成功 */
    char errmsg[160];
} HttpResponse;

typedef struct {
    const char *host;
    const char *port;
    int timeout_ms;
} HttpClient;

void http_response_free(HttpResponse *r);

/* method: GET / POST / PUT；path 以 / 开头；body 可为 NULL */
HttpResponse http_request(const HttpClient *c, const char *method,
                          const char *path, const char *content_type,
                          const char *body, size_t body_len);

#endif
