/* What bionic before API 24 lacks of what hassmic uses (compat22.h): getifaddrs/freeifaddrs (API 24) over netlink,
 * as bionic's own are, and open_memstream (API 23) over funopen, which bionic has had since the start.  Linked only for
 * a target below API 24 (the Makefile); the Spot's Android 5.1 is the first. */
#include "compat22.h"
#if defined(__ANDROID_API__) && __ANDROID_API__ < 24
#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ---- getifaddrs: RTM_GETLINK for names and flags, RTM_GETADDR for IPv4 and IPv6 addresses, as the callers need
 * (netaddr.c, net.c, improv.c: ifa_name, ifa_flags, ifa_addr, ifa_netmask; no broadcast or AF_PACKET entries) */
struct ifa_node {
    struct ifaddrs ifa;
    char name[IF_NAMESIZE];
    union { struct sockaddr_in in; struct sockaddr_in6 in6; } addr, mask;
};

struct link { int index; unsigned flags; char name[IF_NAMESIZE]; };

static int nl_dump(int fd, int type, int (*each)(struct nlmsghdr *, void *), void *arg)
{
    struct { struct nlmsghdr h; struct rtgenmsg g; } req = {
        { NLMSG_LENGTH(sizeof(struct rtgenmsg)), type, NLM_F_REQUEST | NLM_F_DUMP, 1, 0 }, { AF_UNSPEC } };
    if (send(fd, &req, req.h.nlmsg_len, 0) < 0) return -1;
    char buf[16384];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n < 0) return -1;
        for (struct nlmsghdr *h = (struct nlmsghdr *)buf; NLMSG_OK(h, (size_t)n); h = NLMSG_NEXT(h, n)) {
            if (h->nlmsg_type == NLMSG_DONE) return 0;
            if (h->nlmsg_type == NLMSG_ERROR) { errno = EIO; return -1; }
            if (each(h, arg)) return -1;
        }
    }
}

struct links { struct link *v; int n; };

static int on_link(struct nlmsghdr *h, void *arg)
{
    struct links *l = arg;
    struct ifinfomsg *m = NLMSG_DATA(h);
    struct link *x = realloc(l->v, (l->n + 1) * sizeof *x);
    if (!x) return -1;
    l->v = x; x += l->n++;
    x->index = m->ifi_index; x->flags = m->ifi_flags; x->name[0] = 0;
    int len = h->nlmsg_len - NLMSG_LENGTH(sizeof *m);
    for (struct rtattr *a = IFLA_RTA(m); RTA_OK(a, len); a = RTA_NEXT(a, len))
        if (a->rta_type == IFLA_IFNAME) {
            size_t sz = RTA_PAYLOAD(a) < sizeof x->name ? RTA_PAYLOAD(a) : sizeof x->name - 1;
            memcpy(x->name, RTA_DATA(a), sz); x->name[sz] = 0;
        }
    return 0;
}

struct addrs { struct links *links; struct ifa_node *head, *tail; };

static void set_mask(void *sa, int family, int prefix)
{
    unsigned char *b = family == AF_INET ? (unsigned char *)&((struct sockaddr_in *)sa)->sin_addr
                                         : (unsigned char *)&((struct sockaddr_in6 *)sa)->sin6_addr;
    int bytes = family == AF_INET ? 4 : 16;
    for (int i = 0; i < bytes; i++, prefix -= 8)
        b[i] = prefix >= 8 ? 0xff : prefix > 0 ? (unsigned char)(0xff << (8 - prefix)) : 0;
}

static int on_addr(struct nlmsghdr *h, void *arg)
{
    struct addrs *s = arg;
    struct ifaddrmsg *m = NLMSG_DATA(h);
    if (m->ifa_family != AF_INET && m->ifa_family != AF_INET6) return 0;
    const void *local = NULL, *address = NULL;
    int len = h->nlmsg_len - NLMSG_LENGTH(sizeof *m);
    for (struct rtattr *a = IFA_RTA(m); RTA_OK(a, len); a = RTA_NEXT(a, len)) {
        if (a->rta_type == IFA_LOCAL) local = RTA_DATA(a);
        if (a->rta_type == IFA_ADDRESS) address = RTA_DATA(a);
    }
    const void *ip = local ? local : address;             /* IFA_LOCAL is the interface's own on point-to-point links */
    if (!ip) return 0;
    struct ifa_node *n = calloc(1, sizeof *n);
    if (!n) return -1;
    for (int i = 0; i < s->links->n; i++)
        if (s->links->v[i].index == (int)m->ifa_index) {
            strcpy(n->name, s->links->v[i].name); n->ifa.ifa_flags = s->links->v[i].flags;
        }
    n->ifa.ifa_name = n->name;
    if (m->ifa_family == AF_INET) {
        n->addr.in.sin_family = n->mask.in.sin_family = AF_INET;
        memcpy(&n->addr.in.sin_addr, ip, 4);
    } else {
        n->addr.in6.sin6_family = n->mask.in6.sin6_family = AF_INET6;
        memcpy(&n->addr.in6.sin6_addr, ip, 16);
        n->addr.in6.sin6_scope_id = m->ifa_scope == RT_SCOPE_LINK ? m->ifa_index : 0;
    }
    set_mask(&n->mask, m->ifa_family, m->ifa_prefixlen);
    n->ifa.ifa_addr = (struct sockaddr *)&n->addr;
    n->ifa.ifa_netmask = (struct sockaddr *)&n->mask;
    if (s->tail) s->tail->ifa.ifa_next = &n->ifa; else s->head = n;
    s->tail = n;
    return 0;
}

int getifaddrs(struct ifaddrs **ifap)
{
    *ifap = NULL;
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) return -1;
    struct links links = { NULL, 0 };
    struct addrs addrs = { &links, NULL, NULL };
    int r = nl_dump(fd, RTM_GETLINK, on_link, &links);
    if (!r) r = nl_dump(fd, RTM_GETADDR, on_addr, &addrs);
    close(fd);
    free(links.v);
    if (r) { freeifaddrs(addrs.head ? &addrs.head->ifa : NULL); return -1; }
    *ifap = addrs.head ? &addrs.head->ifa : NULL;
    return 0;
}

void freeifaddrs(struct ifaddrs *ifa)
{
    while (ifa) {
        struct ifaddrs *next = ifa->ifa_next;
        free(ifa);                                              /* each is the first member of its ifa_node */
        ifa = next;
    }
}

/* ---- open_memstream over funopen: the buffer grows as it is written, always NUL-terminated, *sizep is the length
 * written so far (the caller frees *bufp after fclose, as with glibc's) */
struct memstream { char **bufp; size_t *sizep; size_t len, cap; };

static int ms_write(void *cookie, const char *data, int n)
{
    struct memstream *m = cookie;
    if (m->len + n + 1 > m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 256;
        while (cap < m->len + n + 1) cap *= 2;
        char *b = realloc(*m->bufp, cap);
        if (!b) return -1;
        *m->bufp = b; m->cap = cap;
    }
    memcpy(*m->bufp + m->len, data, n);
    m->len += n;
    (*m->bufp)[m->len] = 0;
    *m->sizep = m->len;
    return n;
}

static int ms_close(void *cookie)
{
    free(cookie);
    return 0;
}

FILE *open_memstream(char **bufp, size_t *sizep)
{
    struct memstream *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->bufp = bufp; m->sizep = sizep;
    *bufp = malloc(1);
    if (!*bufp) { free(m); return NULL; }
    (*bufp)[0] = 0; *sizep = 0; m->cap = 1;
    FILE *f = funopen(m, NULL, ms_write, NULL, ms_close);
    if (!f) { free(*bufp); *bufp = NULL; free(m); }
    return f;
}
#endif
