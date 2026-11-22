/* Tiny helpers shared by the command-line tools: one-client TCP listener and full writes. */
#ifndef NETIO_H
#define NETIO_H
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* IPv6 and IPv4 on one socket (IPV6_V6ONLY off: an IPv4 client shows as ::ffff:a.b.c.d), so an Echo on an IPv6-only
 * network is reachable at the addresses its mDNS hands out (the Spot under plain Linux announces AAAA records only).
 * Where the kernel has no IPv6, IPv4 alone as before */
static inline int net_listen(int port)
{
    int s = socket(AF_INET6, SOCK_STREAM, 0), one = 1, zero = 0;
    if (s >= 0) {
        struct sockaddr_in6 a6; memset(&a6, 0, sizeof a6);
        a6.sin6_family = AF_INET6; a6.sin6_port = htons(port); a6.sin6_addr = in6addr_any;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
        if (bind(s, (struct sockaddr *)&a6, sizeof a6) == 0 && listen(s, 1) == 0) return s;
        close(s);
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = INADDR_ANY };
    if (s < 0) return -1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 1) < 0) { close(s); return -1; }
    return s;
}

static inline int net_accept(int s)
{
    int c, one = 1;
    do c = accept(s, NULL, NULL); while (c < 0 && errno == EINTR);
    if (c >= 0) setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return c;
}

static inline int write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; n -= w;
    }
    return 0;
}

static inline ssize_t read_full(int fd, void *buf, size_t n)
{
    char *p = buf; size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) break;
        got += r;
    }
    return got;
}
#endif
