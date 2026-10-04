#define _GNU_SOURCE
#include "http_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

void http_response_free(HttpResponse *r) {
    if (!r) return;
    free(r->body);
    r->body = NULL;
    r->body_len = 0;
}

static int dial(const char *host, const char *port, int timeout_ms, char *err, size_t errsz) {
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0) {
        snprintf(err, errsz, "getaddrinfo: %s", gai_strerror(gai));
        return -1;
    }
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        snprintf(err, errsz, "connect %s:%s failed: %s", host, port, strerror(errno));
    }
    return fd;
}

static char *read_all(int fd, size_t *out_len, int timeout_ms, char *err, size_t errsz) {
    (void)timeout_ms;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        ssize_t n = recv(fd, buf + len, cap - len - 1, 0);
        if (n > 0) {
            len += (size_t)n;
            buf[len] = '\0';
        } else if (n == 0) {
            break;
        } else {
            if (errno == EINTR) continue;
            break; /* 超时或对端关闭：尽量解析已收到的内容 */
        }
    }
    if (len == 0) {
        free(buf);
        snprintf(err, errsz, "empty response / recv timeout");
        return NULL;
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

HttpResponse http_request(const HttpClient *c, const char *method,
                          const char *path, const char *content_type,
                          const char *body, size_t body_len) {
    HttpResponse resp = {0};
    resp.status = 0;
    int fd = dial(c->host, c->port, c->timeout_ms, resp.errmsg, sizeof(resp.errmsg));
    if (fd < 0) {
        resp.error = 1;
        return resp;
    }

    char header[1024];
    int hl = snprintf(header, sizeof(header),
                      "%s %s HTTP/1.1\r\n"
                      "Host: %s:%s\r\n"
                      "Connection: close\r\n"
                      "Accept: application/json\r\n",
                      method, path, c->host, c->port);
    if (body && body_len) {
        hl += snprintf(header + hl, sizeof(header) - (size_t)hl,
                       "Content-Type: %s\r\nContent-Length: %zu\r\n",
                       content_type ? content_type : "application/octet-stream",
                       body_len);
    }
    hl += snprintf(header + hl, sizeof(header) - (size_t)hl, "\r\n");
    size_t sent = 0;
    while (sent < (size_t)hl) {
        ssize_t n = send(fd, header + sent, (size_t)hl - sent, 0);
        if (n <= 0) {
            snprintf(resp.errmsg, sizeof(resp.errmsg), "send header: %s", strerror(errno));
            close(fd);
            resp.error = 1;
            return resp;
        }
        sent += (size_t)n;
    }
    if (body && body_len) {
        size_t bs = 0;
        while (bs < body_len) {
            ssize_t n = send(fd, body + bs, body_len - bs, 0);
            if (n <= 0) {
                snprintf(resp.errmsg, sizeof(resp.errmsg), "send body: %s", strerror(errno));
                close(fd);
                resp.error = 1;
                return resp;
            }
            bs += (size_t)n;
        }
    }

    size_t raw_len = 0;
    char *raw = read_all(fd, &raw_len, c->timeout_ms, resp.errmsg, sizeof(resp.errmsg));
    close(fd);
    if (!raw) {
        resp.error = 1;
        return resp;
    }

    /* 解析状态行 */
    if (sscanf(raw, "HTTP/1.%*d %d", &resp.status) != 1) {
        resp.error = 1;
        snprintf(resp.errmsg, sizeof(resp.errmsg), "bad status line");
        free(raw);
        return resp;
    }
    /* 定位 body：兼容 \r\n\r\n 与 \n\n */
    char *body_start = strstr(raw, "\r\n\r\n");
    size_t skip = 4;
    if (!body_start) {
        body_start = strstr(raw, "\n\n");
        skip = 2;
    }
    if (body_start) {
        body_start += skip;
        size_t blen = raw + raw_len - body_start;
        resp.body = malloc(blen + 1);
        memcpy(resp.body, body_start, blen);
        resp.body[blen] = '\0';
        resp.body_len = blen;
    } else {
        resp.body = malloc(1);
        resp.body[0] = '\0';
    }
    free(raw);
    return resp;
}
