/* The Echo's own address changing under a running satellite (issue #17: the router hands out new addresses; after that
 * Home Assistant did not find the Echo again until a power cycle, sometimes two).  Two things go wrong:
 *
 * 1. Links over the old address stay.  Linux keeps a TCP socket whose local address was removed, and nothing leaves it
 *    any more (there is no route from an address the host lacks), so neither side hears of it.  Our keepalive (main.c)
 *    only probes while nothing waits for an acknowledgement; with a state update in flight the socket retransmits for
 *    some 15 min (tcp_retries2).  Meanwhile the dead link holds a client slot and, worse, the voice assistant: it goes
 *    to the first subscriber (proto_esphome.c SUBSCRIBE_VA), so Home Assistant back over the new address would get no
 *    pipelines.  So every poll closes the links whose local address is gone, once there is another one.
 *
 * 2. Home Assistant ignores the announcement of the new address.  Its ESPHome config flow takes a new host from mDNS
 *    only while the device is not connected (config_flow.py _async_validate_mac_abort_configured: "transient mDNS
 *    announcements during Wi-Fi mesh roaming" must not move a working entry), and aioesphomeapi gives up on a silent
 *    link only after 90 s (a ping every 20 s, KEEP_ALIVE_TIMEOUT_RATIO 4.5).  avahi announces the new address at once,
 *    while Home Assistant still holds the old link: ignored, and nothing announces again, so it dials the old address
 *    for ever.  A power cycle makes avahi announce again at boot, but an Echo is back on Wi-Fi within those 90 s more
 *    often than not: hence the second power cycle.
 *    So while Home Assistant should be there (the Echo is set up) and is not, root restarts avahi (state/mdns, main.sh
 *    netwatch): after 100 s without a client, then 200, 400, ... at most every 30 min.  A restart, not a repeat: avahi
 *    says goodbye to its records and announces them anew a few seconds later, so Home Assistant's zeroconf sees the
 *    service go and come and starts its discovery, which moves the entry to the new address.  The same records merely
 *    repeated change nothing in its cache and start nothing.  A new address is announced at once as well.
 *
 * 3. Music Assistant (2.10.6) finds the Sendspin player only by mDNS and dials it; a link it lost while running it
 *    dials again by itself (aiosendspin: after 1, 3, 7, 15 s, ...), but after its own restart it only browses anew.
 *    Where its queries do not reach the Echo (python-zeroconf bound to 0.0.0.0 sends them out of the default route's
 *    interface only; issue #28: the Echo on a VLAN of its own) it never finds the player again, though avahi's
 *    announcement at boot reached it.  So the same while a Sendspin server that played here is gone: after 30 s, then
 *    doubling up to 30 min.  Each peer has its own schedule: Home Assistant and Music Assistant restarting together
 *    cost two avahi restarts, which no link notices.
 *
 * Addresses: every IPv4 and IPv6 address the interfaces have (getifaddrs), up or not: a link over an interface that is
 * down for a moment is not stale, and dhcpcd -K leaves the address in place anyway.  Kept as IPv6, IPv4 ones as
 * ::ffff:a.b.c.d, which is how the listeners' dual-stack sockets (netio.h net_listen) see an IPv4 link's own address;
 * a link over IPv6 is checked the same way (2026-11-22: the listeners took IPv4 only before).  HASSMIC_FAKE_ADDRS
 * (tests) names a file with one address per line instead, either family, loopback not set apart.  HASSMIC_ANNOUNCE_TIMES="<first> <max>" (s; first for every
 * peer). */
#define _GNU_SOURCE
#include "netaddr.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define POLL_S      2
#define MAX_ADDRS   64          /* an Echo has two or three; a PC with containers and VPNs had 17; IPv6 brings several per interface */
#define MAX_LINKS   16          /* ESPHome takes 4 clients; the rest are refused at once */
#define MAX_PEERS   4
static int FIRST_S;             /* HASSMIC_ANNOUNCE_TIMES: instead of each peer's own first_s */
static int MAX_S = 1800;

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int links[MAX_LINKS], nlinks;
static const struct netaddr_hooks *hooks;

void netaddr_link(int fd, int on)
{
    pthread_mutex_lock(&mu);
    if (on) { if (nlinks < MAX_LINKS) links[nlinks++] = fd; }
    else for (int i = 0; i < nlinks; i++) if (links[i] == fd) { links[i] = links[--nlinks]; break; }
    pthread_mutex_unlock(&mu);
}

struct addrs { int n, own; struct in6_addr a[MAX_ADDRS]; };  /* own: how many of a[] are not loopback (those come first) */

static int cmp(const void *x, const void *y) { return memcmp(x, y, sizeof(struct in6_addr)); }

/* an address as IPv6: IPv4 as ::ffff:a.b.c.d.  0: neither family */
static int to6(const struct sockaddr *sa, struct in6_addr *out)
{
    if (sa->sa_family == AF_INET6) { *out = ((const struct sockaddr_in6 *)sa)->sin6_addr; return 1; }
    if (sa->sa_family != AF_INET) return 0;
    memset(out, 0, sizeof *out); out->s6_addr[10] = out->s6_addr[11] = 0xff;
    memcpy(out->s6_addr + 12, &((const struct sockaddr_in *)sa)->sin_addr, 4);
    return 1;
}

/* 0, or -1 when it cannot tell (then nothing is decided on it) */
static int addrs_get(struct addrs *s)
{
    const char *fake = getenv("HASSMIC_FAKE_ADDRS");
    s->n = s->own = 0;
    if (fake) {
        FILE *f = fopen(fake, "r"); char l[64]; struct sockaddr_in v4 = { .sin_family = AF_INET };
        if (!f) return 0;
        while (s->n < MAX_ADDRS && fgets(l, sizeof l, f)) {
            l[strcspn(l, " \r\n")] = 0;
            if (inet_pton(AF_INET, l, &v4.sin_addr) == 1) to6((struct sockaddr *)&v4, &s->a[s->n++]);
            else if (inet_pton(AF_INET6, l, &s->a[s->n]) == 1) s->n++;
        }
        fclose(f);
        s->own = s->n;
    } else {
        struct ifaddrs *ifs, *i; struct in6_addr lo[MAX_ADDRS], a; int nlo = 0, over = 0;
        if (getifaddrs(&ifs)) return -1;
        for (i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || !to6(i->ifa_addr, &a)) continue;
            if (i->ifa_flags & IFF_LOOPBACK) { if (nlo < MAX_ADDRS) lo[nlo++] = a; else over = 1; }
            else if (s->own < MAX_ADDRS) s->a[s->own++] = a;
            else over = 1;
        }
        freeifaddrs(ifs);
        /* More than fit, own and loopback together: a set cut short would have an address that is there look gone
         * and its links closed (a PC with 17 had its link over 127.0.0.1 shut, which was the 17th), so cannot tell */
        if (over || s->own + nlo > MAX_ADDRS) return -1;
        s->n = s->own;
        for (int k = 0; k < nlo && s->n < MAX_ADDRS; k++) s->a[s->n++] = lo[k];
    }
    qsort(s->a, (size_t)s->own, sizeof s->a[0], cmp);
    return 0;
}

static int has(const struct addrs *s, const struct in6_addr *a) { for (int i = 0; i < s->n; i++) if (!memcmp(&s->a[i], a, sizeof *a)) return 1; return 0; }

/* IPv4 as itself, IPv6 as IPv6 */
static void addr_text(const struct in6_addr *a, char *out, socklen_t cap)
{
    if (IN6_IS_ADDR_V4MAPPED(a)) inet_ntop(AF_INET, a->s6_addr + 12, out, cap); else inet_ntop(AF_INET6, a, out, cap);
}

static void addrs_text(const struct addrs *s, char *out, size_t cap)
{
    size_t k = 0; out[0] = 0;
    for (int i = 0; i < s->own && k + INET6_ADDRSTRLEN + 1 < cap; i++) {
        if (i) out[k++] = ' ';
        addr_text(&s->a[i], out + k, (socklen_t)(cap - k)); k += strlen(out + k);
    }
    if (!s->own) snprintf(out, cap, "none");
}

/* Close the links whose local address is not among the Echo's; their readers clean up (shutdown, not close: the fd
 * stays theirs).  How many. */
static int drop_stale(const struct addrs *s)
{
    int n = 0;
    pthread_mutex_lock(&mu);
    for (int i = 0; i < nlinks; i++) {
        struct sockaddr_storage me; socklen_t l = sizeof me; struct in6_addr a;
        if (getsockname(links[i], (struct sockaddr *)&me, &l) || !to6((struct sockaddr *)&me, &a) || has(s, &a)) continue;
        char t[INET6_ADDRSTRLEN]; addr_text(&a, t, sizeof t);
        fprintf(stderr, "network: closing a link over %s, which this Echo no longer has\n", t);
        shutdown(links[i], SHUT_RDWR); n++;
    }
    pthread_mutex_unlock(&mu);
    return n;
}

static int first(int i) { return FIRST_S ? FIRST_S : hooks->peers[i].first_s; }

static time_t mono(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec; }

static void *watch(void *arg)
{
    (void)arg;
    struct addrs last = { 0 }, cur; char was[MAX_ADDRS * (INET6_ADDRSTRLEN + 1)], now_t[sizeof was], why[64];
    struct { time_t since, next; int wait; } p[MAX_PEERS] = { { 0 } };
    int np = hooks->npeers < MAX_PEERS ? hooks->npeers : MAX_PEERS;
    for (;; sleep(POLL_S)) {
        if (addrs_get(&cur)) continue;
        time_t now = mono();
        if (cur.own) {
            /* A new address (not the first one, not the same one back after a gap: TCP links survive that) */
            if (last.own && (cur.own != last.own || memcmp(cur.a, last.a, sizeof cur.a[0] * (size_t)cur.own))) {
                addrs_text(&last, was, sizeof was); addrs_text(&cur, now_t, sizeof now_t);
                fprintf(stderr, "network: address %s now (was %s)\n", now_t, was);
                drop_stale(&cur);
                hooks->announce("new address");
                for (int i = 0; i < np; i++) if (p[i].since) { p[i].since = now; p[i].wait = first(i); p[i].next = now + p[i].wait; }
            } else drop_stale(&cur);
            last = cur;
        }
        /* A peer should be there and is not: announce again, at growing intervals (see 2. and 3. above) */
        for (int i = 0; i < np; i++) {
            const struct netaddr_peer *q = &hooks->peers[i];
            if (!cur.own || !q->expected() || q->linked()) { p[i].since = 0; continue; }
            if (!p[i].since) { p[i].since = now; p[i].wait = first(i); p[i].next = now + p[i].wait; }
            if (now >= p[i].next) {
                snprintf(why, sizeof why, "no %s for %lld s", q->what, (long long)(now - p[i].since));
                hooks->announce(why);
                p[i].wait = p[i].wait * 2 > MAX_S ? MAX_S : p[i].wait * 2; p[i].next = now + p[i].wait;
            }
        }
    }
    return NULL;
}

void netaddr_start(const struct netaddr_hooks *h)
{
    const char *t = getenv("HASSMIC_ANNOUNCE_TIMES"); pthread_t th;
    if (t) sscanf(t, "%d %d", &FIRST_S, &MAX_S);
    hooks = h;
    if (!pthread_create(&th, NULL, watch, NULL)) pthread_detach(th);
}
