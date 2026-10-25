/*
 * adapter_http.c — 最小 HTTP/1.1 客户端（T13，beta）
 */
#include "adapter_http.h"

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int nl_http_parse_url(const char *url, NlHttpUrl *out)
{
    if (!url || !out) return -1;
    memset(out, 0, sizeof(*out));

    const char *p;
    if (strncmp(url, "http://", 7) == 0) {
        out->https = 0;
        p = url + 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        out->https = 1;
        return 1;
    } else {
        return -1;
    }

    const char *slash = strchr(p, '/');
    size_t hostlen = slash ? (size_t)(slash - p) : strlen(p);
    if (hostlen == 0 || hostlen >= sizeof(out->host)) return -1;

    const char *colon = memchr(p, ':', hostlen);
    if (colon) {
        size_t hlen = (size_t)(colon - p);
        if (hlen == 0 || hlen >= sizeof(out->host)) return -1;
        memcpy(out->host, p, hlen);
        out->host[hlen] = '\0';
        const char *ps = colon + 1;
        size_t plen = hostlen - hlen - 1;
        if (plen == 0 || plen > 5) return -1;
        int port = 0;
        for (size_t i = 0; i < plen; i++) {
            if (!isdigit((unsigned char)ps[i])) return -1;
            port = port * 10 + (ps[i] - '0');
        }
        if (port <= 0 || port > 65535) return -1;
        out->port = port;
    } else {
        memcpy(out->host, p, hostlen);
        out->host[hostlen] = '\0';
        out->port = 80;
    }

    const char *path = slash ? slash : "/";
    if (strlen(path) >= sizeof(out->path)) return -1;
    snprintf(out->path, sizeof(out->path), "%s", path);
    return 0;
}

static long http_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int connect_timeout(const char *host, int port, int timeout_ms)
{
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int nl_http_request(const char *method, const char *url, const char *body,
                    int timeout_ms, char *out, size_t cap)
{
    if (!method || !url || !out || cap < 2) return -1;
    NlHttpUrl u;
    int pr = nl_http_parse_url(url, &u);
    if (pr == 1) return -2;
    if (pr != 0) return -1;

    int fd = connect_timeout(u.host, u.port, timeout_ms);
    if (fd < 0) return -1;

    char req[2048];
    const char *b = body ? body : "";
    int n = snprintf(req, sizeof(req),
                     "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
                     "Content-Length: %zu\r\n\r\n%s",
                     method, u.path, u.host, strlen(b), b);
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        close(fd);
        return -1;
    }
    if (write(fd, req, (size_t)n) != n) {
        close(fd);
        return -1;
    }

    size_t used = 0;
    long deadline = http_now_ms() + timeout_ms;
    char raw[8192];
    size_t rawlen = 0;
    for (;;) {
        long remain = deadline - http_now_ms();
        if (remain <= 0) break;
        struct pollfd p = { .fd = fd, .events = POLLIN, .revents = 0 };
        int r = poll(&p, 1, (int)remain);
        if (r <= 0) break;
        ssize_t got = read(fd, raw + rawlen, sizeof(raw) - rawlen);
        if (got <= 0) break;
        rawlen += (size_t)got;
        if (rawlen >= sizeof(raw)) break;
    }
    close(fd);

    if (rawlen == 0) return -1;
    /* 跳过响应头 */
    size_t hdr_end = 0;
    for (size_t i = 0; i + 3 < rawlen; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') {
            hdr_end = i + 4;
            break;
        }
    }
    const char *bodyp = raw + hdr_end;
    size_t bodylen = rawlen - hdr_end;
    size_t take = bodylen < cap - 1 ? bodylen : cap - 1;
    memcpy(out, bodyp, take);
    out[take] = '\0';
    used = take;
    (void)used;
    return 0;
}
