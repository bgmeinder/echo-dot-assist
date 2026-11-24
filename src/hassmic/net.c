#ifndef _GNU_SOURCE
#define _GNU_SOURCE                     /* getresgid on glibc */
#endif
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/fsuid.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "logread.h"
#include "net.h"

/* An IPv4 address on an interface with a link (IFF_RUNNING: carrier), other than loopback.  The address alone says
 * nothing: dhcpcd runs with -K and keeps it on wlan0 when the network is gone (Dot 2 after a reset, 2026-10-07: no
 * network in wpa_supplicant, INACTIVE, carrier 0, flags 0x1003, the old address still there).  HASSMIC_FAKE_ONLINE
 * (tests): a file, online while it exists */
int net_online(void)
{
    const char *f = getenv("HASSMIC_FAKE_ONLINE");
    if (f) return access(f, F_OK) == 0;
    struct ifaddrs *ifs, *i; int on = 0;
    if (getifaddrs(&ifs)) return 1;                     /* cannot tell: assume it is fine (improv.c: never advertise for nothing) */
    /* an IPv4 address, or an IPv6 one beyond the link (unique local or global): an IPv6-only network has no IPv4 at
     * all, and counting only IPv4 took it for no network (Improv advertising, the action button taken: 2026-11-22) */
    for (i = ifs; i && !on; i = i->ifa_next) {
        if (!i->ifa_addr || !(i->ifa_flags & IFF_UP) || !(i->ifa_flags & IFF_RUNNING) || (i->ifa_flags & IFF_LOOPBACK)) continue;
        if (i->ifa_addr->sa_family == AF_INET) on = 1;
        else if (i->ifa_addr->sa_family == AF_INET6) {
            const struct in6_addr *a = &((const struct sockaddr_in6 *)i->ifa_addr)->sin6_addr;
            on = !IN6_IS_ADDR_LINKLOCAL(a) && !IN6_IS_ADDR_LOOPBACK(a) && !IN6_IS_ADDR_UNSPECIFIED(a);
        }
    }
    freeifaddrs(ifs);
    return on;
}

/* ---------------------------------------------------------------- mDNS */

static int put_name(uint8_t *p, size_t cap, const char *name)
{
    size_t o = 0;
    while (*name) {
        const char *dot = strchr(name, '.'); size_t l = dot ? (size_t)(dot - name) : strlen(name);
        if (!l || l > 63 || o + l + 2 > cap) return -1;
        p[o++] = l; memcpy(p + o, name, l); o += l;
        name += l + (dot ? 1 : 0);
    }
    p[o++] = 0;
    return o;
}

/* Name at *off (compression pointers followed) as dotted text; *off moves past it.  -1 on a malformed packet. */
static int get_name(const uint8_t *m, size_t len, size_t *off, char *out, size_t outsz)
{
    size_t o = *off, n = 0; int jumps = 0, moved = 0;
    for (;;) {
        if (o >= len) return -1;
        uint8_t l = m[o];
        if ((l & 0xc0) == 0xc0) {
            if (o + 1 >= len || ++jumps > 16) return -1;
            if (!moved) { *off = o + 2; moved = 1; }
            o = (l & 0x3f) << 8 | m[o + 1];
            continue;
        }
        if (l & 0xc0) return -1;
        if (!l) { if (!moved) *off = o + 1; break; }
        if (o + 1 + l > len || n + l + 2 > outsz) return -1;
        if (n) out[n++] = '.';
        memcpy(out + n, m + o + 1, l); n += l; o += 1 + l;
    }
    out[n] = 0;
    return 0;
}

/* how good an address is to reach a host at: a ULA (fc00::/7) best, it stays; a global one; link-local last (it needs
 * the interface it was heard on) */
static int rank6(const struct in6_addr *a)
{
    if (IN6_IS_ADDR_UNSPECIFIED(a) || IN6_IS_ADDR_LOOPBACK(a) || IN6_IS_ADDR_MULTICAST(a) || IN6_IS_ADDR_V4MAPPED(a)) return 0;
    return (a->s6_addr[0] & 0xfe) == 0xfc ? 3 : IN6_IS_ADDR_LINKLOCAL(a) ? 1 : 2;
}

static int parse_answer(const uint8_t *m, size_t len, const char *name, struct in6_addr *out)
{
    char rn[256]; size_t off = 12; int best = 0;
    if (len < 12 || !(m[2] & 0x80)) return 0;                                  /* responses only */
    unsigned qd = m[4] << 8 | m[5], rr = (m[6] << 8 | m[7]) + (m[8] << 8 | m[9]) + (m[10] << 8 | m[11]);
    for (unsigned i = 0; i < qd; i++) { if (get_name(m, len, &off, rn, sizeof rn)) return 0; off += 4; }
    for (unsigned i = 0; i < rr; i++) {
        if (get_name(m, len, &off, rn, sizeof rn) || off + 10 > len) return 0;
        unsigned type = m[off] << 8 | m[off + 1], cls = (m[off + 2] << 8 | m[off + 3]) & 0x7fff, rdlen = m[off + 8] << 8 | m[off + 9];
        off += 10;
        if (off + rdlen > len) return 0;
        if (type == 28 && cls == 1 && rdlen == 16 && !strcasecmp(rn, name)) {
            struct in6_addr x; memcpy(&x, m + off, 16);
            int r = rank6(&x);
            if (r > best) { best = r; *out = x; }
        }
        off += rdlen;
    }
    return best;
}

/* Sent from a port other than 5353, so responders answer by unicast to it (RFC 6762 5.1, 6.7) and the responder that
 * owns 5353 on the Echo (mdnsd) is not in the way.  IPv6 first: an AAAA question to ff02::fb on every interface that
 * can carry it (multicast has no route to follow); of the answers, a ULA before a global address before a link-local
 * one, which keeps the interface it was heard on.  No interface with IPv6: none asked, -1 at once. */
int mdns_resolve6(const char *host, struct sockaddr_in6 *out)
{
    char name[256]; uint8_t q[300], r[1500]; int got = 0;
    snprintf(name, sizeof name, "%s", host);
    size_t nl = strlen(name); if (nl && name[nl - 1] == '.') name[nl - 1] = 0;
    memset(q, 0, 12); q[1] = 0x42;                                              /* id; flags 0: standard query */
    q[5] = 1;                                                                   /* one question */
    int l = put_name(q + 12, sizeof q - 16, name);
    if (l < 0) return -1;
    size_t qlen = 12 + l;
    q[qlen++] = 0; q[qlen++] = 28;                                              /* type AAAA */
    q[qlen++] = 0x80; q[qlen++] = 1;                                            /* class IN, unicast response wanted */
    int s = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0), bound = 0, on = 1;
    if (s < 0) return -1;
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof on);
    struct sockaddr_in6 me; memset(&me, 0, sizeof me); me.sin6_family = AF_INET6;
    for (int i = 0; i < 20 && !bound; i++) {
        me.sin6_port = htons(16384 + ((unsigned)getpid() * 7919u + i * 104729u) % 16384);
        bound = !bind(s, (struct sockaddr *)&me, sizeof me);
    }
    if (!bound) { close(s); return -1; }
    int hops = 255; setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof hops);
    struct sockaddr_in6 to; memset(&to, 0, sizeof to); to.sin6_family = AF_INET6; to.sin6_port = htons(5353);
    inet_pton(AF_INET6, "ff02::fb", &to.sin6_addr);
    static const int wait_ms[] = { 250, 500, 1000 };
    for (int round = 0; round < 3 && !got; round++) {
        struct ifaddrs *ifs = NULL; unsigned done[16]; int nd = 0;
        if (!getifaddrs(&ifs)) {
            for (struct ifaddrs *i = ifs; i; i = i->ifa_next) {
                if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET6 || (i->ifa_flags & IFF_LOOPBACK) ||
                    !(i->ifa_flags & IFF_UP) || !(i->ifa_flags & IFF_MULTICAST)) continue;
                unsigned x = if_nametoindex(i->ifa_name); int seen = 0;
                for (int k = 0; k < nd; k++) seen |= done[k] == x;
                if (seen || !x || nd == 16) continue;                           /* once per interface, not per address */
                done[nd++] = x;
                setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &x, sizeof x);
                to.sin6_scope_id = x;
                sendto(s, q, qlen, 0, (struct sockaddr *)&to, sizeof to);
            }
            freeifaddrs(ifs);
        }
        if (!nd) break;                                 /* no interface with IPv6: nobody to ask (IPv4 next, at once) */
        struct pollfd pfd = { .fd = s, .events = POLLIN };
        while (!got && poll(&pfd, 1, wait_ms[round]) > 0) {
            struct sockaddr_in6 from; socklen_t fl = sizeof from;
            ssize_t n = recvfrom(s, r, sizeof r, 0, (struct sockaddr *)&from, &fl);
            struct in6_addr a;
            if (n > 0 && parse_answer(r, (size_t)n, name, &a)) {
                memset(out, 0, sizeof *out); out->sin6_family = AF_INET6; out->sin6_addr = a;
                if (IN6_IS_ADDR_LINKLOCAL(&a)) out->sin6_scope_id = from.sin6_scope_id;
                got = 1;
            }
        }
    }
    close(s);
    return got ? 0 : -1;
}

static unsigned parse_a(const uint8_t *m, size_t len, const char *name)
{
    char rn[256]; size_t off = 12;
    if (len < 12 || !(m[2] & 0x80)) return 0;                                  /* responses only */
    unsigned qd = m[4] << 8 | m[5], rr = (m[6] << 8 | m[7]) + (m[8] << 8 | m[9]) + (m[10] << 8 | m[11]);
    for (unsigned i = 0; i < qd; i++) { if (get_name(m, len, &off, rn, sizeof rn)) return 0; off += 4; }
    for (unsigned i = 0; i < rr; i++) {
        if (get_name(m, len, &off, rn, sizeof rn) || off + 10 > len) return 0;
        unsigned type = m[off] << 8 | m[off + 1], cls = (m[off + 2] << 8 | m[off + 3]) & 0x7fff, rdlen = m[off + 8] << 8 | m[off + 9];
        off += 10;
        if (off + rdlen > len) return 0;
        if (type == 1 && cls == 1 && rdlen == 4 && !strcasecmp(rn, name)) { unsigned a; memcpy(&a, m + off, 4); return a; }
        off += rdlen;
    }
    return 0;
}

/* The same over IPv4 (an A question to 224.0.0.251 on every IPv4 interface), for a name with no IPv6 address */
unsigned mdns_resolve4(const char *host)
{
    char name[256]; uint8_t q[300], r[1500]; unsigned addr = 0;
    snprintf(name, sizeof name, "%s", host);
    size_t nl = strlen(name); if (nl && name[nl - 1] == '.') name[nl - 1] = 0;
    memset(q, 0, 12); q[1] = 0x42;                                              /* id; flags 0: standard query */
    q[5] = 1;                                                                   /* one question */
    int l = put_name(q + 12, sizeof q - 16, name);
    if (l < 0) return 0;
    size_t qlen = 12 + l;
    q[qlen++] = 0; q[qlen++] = 1;                                               /* type A */
    q[qlen++] = 0x80; q[qlen++] = 1;                                            /* class IN, unicast response wanted */
    int s = socket(AF_INET, SOCK_DGRAM, 0), bound = 0;
    if (s < 0) return 0;
    struct sockaddr_in me = { .sin_family = AF_INET };
    for (int i = 0; i < 20 && !bound; i++) {
        me.sin_port = htons(16384 + ((unsigned)getpid() * 7919u + i * 104729u) % 16384);
        bound = !bind(s, (struct sockaddr *)&me, sizeof me);
    }
    if (!bound) { close(s); return 0; }
    unsigned char ttl = 255; setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(5353) };
    inet_pton(AF_INET, "224.0.0.251", &to.sin_addr);
    static const int wait_ms[] = { 250, 500, 1000 };
    for (int round = 0; round < 3 && !addr; round++) {
        struct ifaddrs *ifs = NULL; int sent = 0;
        if (!getifaddrs(&ifs)) {
            for (struct ifaddrs *i = ifs; i; i = i->ifa_next) {
                if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) ||
                    !(i->ifa_flags & IFF_UP) || !(i->ifa_flags & IFF_MULTICAST)) continue;
                struct in_addr via = ((struct sockaddr_in *)i->ifa_addr)->sin_addr;
                setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &via, sizeof via);
                if (sendto(s, q, qlen, 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)qlen) sent++;
            }
            freeifaddrs(ifs);
        }
        if (!sent) sendto(s, q, qlen, 0, (struct sockaddr *)&to, sizeof to);
        struct pollfd pfd = { .fd = s, .events = POLLIN };
        while (!addr && poll(&pfd, 1, wait_ms[round]) > 0) {
            ssize_t n = recv(s, r, sizeof r, 0);
            if (n > 0) addr = parse_a(r, n, name);
        }
    }
    close(s);
    return addr;
}

/* ---------------------------------------------------------------- TCP */

/* A socket owned by the real group where it differs from the effective one (a firewall that lets one group out); the
 * filesystem group is per thread and switched back at once.  hassmic as root, as on Linux: a plain socket */
int net_socket(int family, int type, int protocol)
{
    gid_t r, e, s; int fd;
    if (getresgid(&r, &e, &s) || r == e) return socket(family, type, protocol);
    setfsgid(r);
    fd = socket(family, type, protocol);
    setfsgid(e);
    return fd;
}

int net_connect(const char *host, const char *port, int timeout_s)
{
    struct addrinfo hints = { 0 }, *ai = NULL, local = { 0 }, *list = NULL; struct sockaddr_in6 sin; struct sockaddr_in sin4 = { 0 };
    size_t hl = strlen(host);
    if (hl > 6 && !strcasecmp(host + hl - 6, ".local")) {     /* IPv6 first; IPv4 where the name has no IPv6 address */
        unsigned a4;
        if (!mdns_resolve6(host, &sin)) {
            sin.sin6_port = htons(atoi(port));
            local.ai_family = AF_INET6; local.ai_addr = (struct sockaddr *)&sin; local.ai_addrlen = sizeof sin;
            list = &local;
        } else if ((a4 = mdns_resolve4(host))) {
            sin4.sin_family = AF_INET; sin4.sin_port = htons(atoi(port)); sin4.sin_addr.s_addr = a4;
            local.ai_family = AF_INET; local.ai_addr = (struct sockaddr *)&sin4; local.ai_addrlen = sizeof sin4;
            list = &local;
        }
    }
    hints.ai_socktype = SOCK_STREAM;
    if (!list) {                                        /* also for ".local" that nobody answered: some DNS servers serve it */
        /* private: the host of a media or server address may be the household's own name (a DDNS name) */
        if (getaddrinfo(host, port, &hints, &ai)) { log_private("net: cannot resolve %s", host); return -1; }
        list = ai;
    }
    int fd = -1;
    for (struct addrinfo *p = list; p && fd < 0; p = p->ai_next) {
        struct timeval tv = { timeout_s, 0 };           /* connect() obeys the send timeout */
        if ((fd = net_socket(p->ai_family, SOCK_STREAM, 0)) < 0) continue;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (connect(fd, p->ai_addr, p->ai_addrlen)) { close(fd); fd = -1; }
    }
    if (ai) freeaddrinfo(ai);
    if (fd < 0) log_private("net: cannot connect to %s:%s (not a local address? see lockdown.sh)", host, port);
    return fd;
}
