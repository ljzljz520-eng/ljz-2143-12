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

void http_init(HttpClient *c, const char *host, int port, int timeout_ms) {
    snprintf(c->host, sizeof(c->host), "%s", host);
    c->port = port;
    c->timeout_ms = timeout_ms > 0 ? timeout_ms : 2000;
}

void http_response_free(HttpResponse *r) {
    free(r->body);
    r->body = NULL;
    r->size = 0;
    r->status = 0;
}

static int connect_all(HttpClient *c) {
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char port[16];
    snprintf(port, sizeof(port), "%d", c->port);
    if (getaddrinfo(c->host, port, &hints, &res) != 0) return -1;
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        struct timeval tv = {c->timeout_ms / 1000, (c->timeout_ms % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static char *read_all(int fd, size_t *out_len) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        ssize_t n = recv(fd, buf + len, cap - len - 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        len += (size_t)n;
    }
    buf[len] = 0;
    *out_len = len;
    return buf;
}

static bool do_request(HttpClient *c, const char *method, const char *path,
                       const char *content_type, const char *body,
                       HttpResponse *out) {
    int fd = connect_all(c);
    if (fd < 0) return false;

    FILE *req = fdopen(dup(fd), "w");
    fprintf(req, "%s %s HTTP/1.1\r\n", method, path);
    fprintf(req, "Host: %s:%d\r\n", c->host, c->port);
    fprintf(req, "Connection: close\r\n");
    if (body) {
        fprintf(req, "Content-Type: %s\r\n", content_type);
        fprintf(req, "Content-Length: %zu\r\n", strlen(body));
    }
    fputs("\r\n", req);
    if (body) fputs(body, req);
    fflush(req);
    fclose(req);

    size_t raw_len = 0;
    char *raw = read_all(fd, &raw_len);
    close(fd);
    if (!raw) return false;

    char *body_start = strstr(raw, "\r\n\r\n");
    out->status = 0;
    if (raw) {
        if (sscanf(raw, "HTTP/1.%*d %d", &out->status) != 1) out->status = 0;
    }
    if (body_start) {
        body_start += 4;
        /* chunked 支持 */
        char *te = strcasestr(raw, "transfer-encoding: chunked");
        size_t remain = raw + raw_len - body_start;
        if (te && te < body_start) {
            size_t cap = remain, ol = 0;
            char *dec = malloc(cap);
            char *p = body_start;
            while (p < raw + raw_len) {
                char *eol = strstr(p, "\r\n");
                if (!eol) break;
                long sz = strtol(p, NULL, 16);
                if (sz <= 0) break;
                p = eol + 2;
                if (p + sz > raw + raw_len) break;
                if (ol + (size_t)sz + 1 > cap) { cap = ol + sz + 1; dec = realloc(dec, cap); }
                memcpy(dec + ol, p, sz);
                ol += (size_t)sz;
                p += sz + 2;
            }
            dec[ol] = 0;
            out->body = dec;
            out->size = ol;
        } else {
            out->body = malloc(remain + 1);
            memcpy(out->body, body_start, remain + 1);
            out->size = remain;
        }
    } else {
        out->body = malloc(1);
        out->body[0] = 0;
        out->size = 0;
    }
    free(raw);
    return out->status > 0;
}

bool http_get(HttpClient *c, const char *path, HttpResponse *out) {
    return do_request(c, "GET", path, NULL, NULL, out);
}

bool http_post(HttpClient *c, const char *path, const char *content_type,
               const char *body, HttpResponse *out) {
    return do_request(c, "POST", path, content_type, body, out);
}
