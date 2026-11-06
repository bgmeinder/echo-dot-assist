/*
 * Media over HTTPS (issue #30).  Home Assistant builds the URLs of TTS replies, announcements and play_media with
 * get_url(): when it serves HTTPS itself (http: ssl_certificate), that prefers the external URL and never hands out a
 * plain http:// one.  proto_esphome.c's own HTTP/1.0 client stays for http://; https:// goes through the firmware's
 * libcurl 7.50.1 (dlopen'ed, as in update.c and davs.c: OpenSSL and the system CA store, /system/etc/security/cacerts).
 * A thread runs the transfer and writes the body into one end of a socketpair; the other end goes to the decoders, so
 * they read it as they read a plain fetch, and the media player's cancel, a shutdown() of that socket, ends the
 * transfer too (the next write fails, and a write callback that returns short stops curl).
 *
 * The certificate is checked by default (system CAs, and the name).  With HTTPS on, Home Assistant hands out no URL by
 * IP address (get_url: allow_ip false), so a public certificate passes as long as the Echo reaches that name; a
 * self-signed one does not: setting "https_verify" off accepts any certificate (the page says what that costs).
 */
#include "https.h"
#include "hash.h"
#include "net.h"
#include "statefile.h"
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

static atomic_int verify = 1;
int https_verify(int set) { if (set >= 0) atomic_store(&verify, !!set); return atomic_load(&verify); }

/* The certificates to check against (https.h).  libcurl's default is the file it was built with, and the firmware's
 * libcurl 7.50.1 on rook names /etc/ssl/certs/ca-certificates.crt, a Linux file the Echo has not: every HTTPS fetch
 * failed "cannot be authenticated with given CA certificates".  Fire OS's own store, /system/etc/security/cacerts, has
 * the roots (ISRG Root X1; DigiCert Global Root G2, which Amazon's api.amazon.* chain to), but as one file per root
 * named by OpenSSL's *old* subject hash (c90bc37d.0), and rook's OpenSSL looks a directory up by the new one
 * (607986c7): CAPATH alone finds nothing ("Download from Amazon": codepair HTTP 0, 2026-11-05; Home Assistant's
 * Let's Encrypt chain only passed through cacerts.pem).  So hassmic makes one file of them at its first fetch: every
 * PEM block of the store, then cacerts.pem next to the binary (scripts/device/: Let's Encrypt's two roots, which old
 * stores lack or may lack), into state/cacerts.bundle.pem (rewritten only when it differs), and hands that as CAINFO.
 * Where there is no store to read (the PC) nothing is set and libcurl keeps its own default: hassmic never narrows
 * what is trusted to its own two roots.  The tests' own CA (HASSMIC_CA_FILE) takes the place of all of it.
 * CAINFO must name a file that exists, or OpenSSL refuses the lot (CURLE 77) */
static const char STORE[] = "/system/etc/security/cacerts";

/* Certificates never taken into the bundle, whatever a model's store holds: those naming eMudhra (in their subject or
 * issuer, as the DER's own text): not trusted by this project (2026-11-05), left out deliberately.  None was in
 * rook's store (164 roots) or in cacerts.pem when this was written; this keeps it so for every model and firmware */
static const char *const DISTRUSTED[] = { "emudhra" };

/* whether a PEM block (b .. e, the markers included) names one of DISTRUSTED */
static int distrusted(const char *b, const char *e)
{
    char body[8192]; size_t k = 0;
    uint8_t der[6144];
    b = strchr(b, '\n');                            /* past the BEGIN line */
    for (const char *q = b ? b : e; q < e && *q != '-' && k < sizeof body; q++)
        if (*q != '\n' && *q != '\r' && *q != ' ' && *q != '\t') body[k++] = *q;
    long n = k < sizeof body ? b64_decode(body, k, der, sizeof der) : -1;
    if (n <= 0) return 1;                           /* a block that will not decode is not trusted either */
    for (size_t i = 0; i < sizeof DISTRUSTED / sizeof *DISTRUSTED; i++) {
        size_t l = strlen(DISTRUSTED[i]);
        for (long j = 0; j + (long)l <= n; j++)
            if (!strncasecmp((const char *)der + j, DISTRUSTED[i], l)) return 1;
    }
    return 0;
}

/* the PEM certificate blocks of a file appended to *buf (grown); the count, or -1 when out of memory.  *dropped counts
 * the blocks left out as distrusted */
static int pem_blocks(const char *path, char **buf, size_t *len, size_t *cap, int *dropped)
{
    static const char B[] = "-----BEGIN CERTIFICATE-----", E[] = "-----END CERTIFICATE-----";
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char text[16384];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    int k = 0;
    for (char *b = strstr(text, B), *e; b && (e = strstr(b, E)); b = strstr(e, B)) {
        if (distrusted(b, e)) { e += sizeof E - 1; (*dropped)++; continue; }
        e += sizeof E - 1;
        size_t m = (size_t)(e - b) + 1;
        if (*len + m + 1 > *cap) {
            size_t nc = (*cap ? *cap * 2 : 1 << 18) + m;
            char *nb = realloc(*buf, nc);
            if (!nb) return -1;
            *buf = nb; *cap = nc;
        }
        memcpy(*buf + *len, b, m - 1); (*buf)[*len + m - 1] = '\n';
        *len += m; k++;
    }
    return k;
}

const char *https_ca_file(void)
{
    static char path[PATH_MAX]; static int done;
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    const char *env = getenv("HASSMIC_CA_FILE");
    if (env) return env;
    pthread_mutex_lock(&m);
    if (!done) {
        done = 1; path[0] = 0;
        char *buf = NULL, own[PATH_MAX], file[PATH_MAX * 2];
        size_t len = 0, cap = 0;
        int roots = 0, ok = 1, dropped = 0;
        const char *store = getenv("HASSMIC_CA_STORE") ? getenv("HASSMIC_CA_STORE") : STORE;   /* tests */
        DIR *d = opendir(store);
        for (struct dirent *e; d && ok && (e = readdir(d)); ) {
            if (e->d_name[0] == '.') continue;
            snprintf(file, sizeof file, "%s/%s", store, e->d_name);
            int k = pem_blocks(file, &buf, &len, &cap, &dropped);
            if (k < 0) ok = 0; else roots += k;
        }
        if (d) closedir(d);
        ssize_t n = readlink("/proc/self/exe", own, sizeof own - 16);
        if (ok && roots && n > 0) {
            own[n] = 0;
            char *slash = strrchr(own, '/');
            strcpy(slash ? slash + 1 : own, "cacerts.pem");
            int k = pem_blocks(own, &buf, &len, &cap, &dropped);
            if (k < 0) ok = 0;
        }
        if (ok && roots) {
            const char *sd = getenv("HASSMIC_STATE");
            snprintf(path, sizeof path, "%s/cacerts.bundle.pem", sd ? sd : "/data/local/hassmic/state");
            char *old = NULL; size_t ol = 0;             /* the file as it is: rewritten only when it differs */
            FILE *f = fopen(path, "r");
            if (f) {
                if ((old = malloc(len + 2))) ol = fread(old, 1, len + 1, f);
                fclose(f);
            }
            if (!(old && ol == len && !memcmp(old, buf, len)) && statefile_write(path, 0644, buf, len)) {
                fprintf(stderr, "https: cannot write %s; libcurl keeps its own default\n", path);
                path[0] = 0;
            } else fprintf(stderr, "https: %d roots of the system's store and hassmic's own in %s, %d distrusted left out\n",
                           roots, path, dropped);
            free(old);
        }
        free(buf);
    }
    pthread_mutex_unlock(&m);
    return path[0] ? path : NULL;
}

const char *https_ca_path(void)
{
    static const char dir[] = "/system/etc/security/cacerts";
    return access(dir, R_OK) == 0 ? dir : NULL;
}

/* ---------------------------------------------------------------- libcurl, from the firmware */

enum { OPT_WRITEDATA = 10001, OPT_URL = 10002, OPT_USERAGENT = 10018, OPT_LOW_SPEED_LIMIT = 19, OPT_LOW_SPEED_TIME = 20,
       OPT_HEADERDATA = 10029, OPT_FAILONERROR = 45, OPT_FOLLOWLOCATION = 52, OPT_SSL_VERIFYPEER = 64,
       OPT_MAXREDIRS = 68, OPT_CONNECTTIMEOUT = 78, OPT_SSL_VERIFYHOST = 81, OPT_NOSIGNAL = 99, OPT_CAINFO = 10065,
       OPT_CAPATH = 10097, OPT_PROTOCOLS = 181, OPT_REDIR_PROTOCOLS = 182, OPT_RESOLVE = 10203, OPT_WRITEFUNCTION = 20011,
       OPT_HEADERFUNCTION = 20079, OPT_OPENSOCKETFUNCTION = 20163, OPT_OPENSOCKETDATA = 10164,
       INFO_RESPONSE_CODE = 0x200002, PROTO_HTTP_S = 1 | 2,
       CURLE_WRITE_ERROR = 23, CURLE_PEER_FAILED_VERIFICATION = 51, CURLE_SSL_CACERT = 60 };
struct curl_sockaddr { int family, socktype, protocol; unsigned addrlen; };     /* followed by the address; unused here */
struct curl_slist;

static struct {
    void *(*init)(void);
    int (*setopt)(void *, int, ...);
    int (*perform)(void *);
    void (*cleanup)(void *);
    int (*getinfo)(void *, int, ...);
    const char *(*strerror)(int);
    struct curl_slist *(*slist_append)(struct curl_slist *, const char *);
    void (*slist_free_all)(struct curl_slist *);
} curl;
static int curl_ok;

static void curl_load(void)
{
    void *h;
    if (!(h = dlopen("libcurl.so", RTLD_NOW)) && !(h = dlopen("libcurl.so.4", RTLD_NOW))) {    /* Echo; PC */
        fprintf(stderr, "media: no libcurl for https (%s)\n", dlerror()); return;
    }
    curl.init = (void *(*)(void))dlsym(h, "curl_easy_init");
    curl.setopt = (int (*)(void *, int, ...))dlsym(h, "curl_easy_setopt");
    curl.perform = (int (*)(void *))dlsym(h, "curl_easy_perform");
    curl.cleanup = (void (*)(void *))dlsym(h, "curl_easy_cleanup");
    curl.getinfo = (int (*)(void *, int, ...))dlsym(h, "curl_easy_getinfo");
    curl.strerror = (const char *(*)(int))dlsym(h, "curl_easy_strerror");
    curl.slist_append = (struct curl_slist *(*)(struct curl_slist *, const char *))dlsym(h, "curl_slist_append");
    curl.slist_free_all = (void (*)(struct curl_slist *))dlsym(h, "curl_slist_free_all");
    curl_ok = curl.init && curl.setopt && curl.perform && curl.cleanup && curl.getinfo && curl.strerror
              && curl.slist_append && curl.slist_free_all;
}

static int on_socket(void *arg, int purpose, struct curl_sockaddr *a)
{
    (void)arg; (void)purpose;
    return net_socket(a->family, a->socktype, a->protocol);   /* the egress lock lets this group out: an external URL */
}

/* ---------------------------------------------------------------- one transfer */

struct xfer {
    pthread_mutex_t m; pthread_cond_t c;
    int state;                          /* 0 connecting, 1 the body comes (https_open returns), -1 failed */
    int refs;                           /* https_open and the thread: the last one frees it */
    int wfd;                            /* our end of the socketpair */
    void *h; struct curl_slist *resolve;
    char url[1024];
};

static void set_state(struct xfer *x, int s)
{
    pthread_mutex_lock(&x->m);
    if (!x->state) { x->state = s; pthread_cond_signal(&x->c); }
    pthread_mutex_unlock(&x->m);
}

static void put(struct xfer *x)
{
    pthread_mutex_lock(&x->m); int last = !--x->refs; pthread_mutex_unlock(&x->m);
    if (!last) return;
    pthread_mutex_destroy(&x->m); pthread_cond_destroy(&x->c);
    free(x);
}

/* The end of a response's headers: a 2xx one is the body's (redirects have their own; FAILONERROR fails 4xx and up),
 * and https_open may return.  The caller learns of a failure before it has a socket, as with a plain fetch. */
static size_t on_header(char *d, size_t size, size_t n, void *arg)
{
    struct xfer *x = arg; long code = 0;
    if (size * n <= 2 && (d[0] == '\r' || d[0] == '\n')) {
        curl.getinfo(x->h, INFO_RESPONSE_CODE, &code);
        if (code >= 200 && code < 300) set_state(x, 1);
    }
    return size * n;
}

static size_t on_data(char *d, size_t size, size_t n, void *arg)
{
    struct xfer *x = arg; size_t len = size * n, off = 0;
    set_state(x, 1);
    while (off < len) {                 /* blocks while the decoders hold back: the TTS queue is full */
        ssize_t w = send(x->wfd, d + off, len - off, MSG_NOSIGNAL);
        if (w <= 0) return 0;           /* the reader hung up (cancel, or it played what it wanted): stop */
        off += (size_t)w;
    }
    return len;
}

static void *run(void *arg)
{
    struct xfer *x = arg;
    int rc = curl.perform(x->h);
    pthread_mutex_lock(&x->m); int began = x->state == 1; pthread_mutex_unlock(&x->m);
    if (rc && !(rc == CURLE_WRITE_ERROR && began))     /* a write error once the body came: the reader stopped it */
        fprintf(stderr, "media: %s: %s%s\n", x->url, curl.strerror(rc),
                rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT
                    ? " (the settings page's \"Check HTTPS certificates\" off accepts it)" : "");
    set_state(x, -1);
    close(x->wfd);                      /* the reader's EOF */
    curl.cleanup(x->h);
    if (x->resolve) curl.slist_free_all(x->resolve);
    put(x);
    return NULL;
}

/* A ".local" host (homeassistant.local) by our own mDNS lookup, as net_connect does for http://: neither the system
 * resolver nor this libcurl asks mDNS.  The certificate is still checked against the name. */
static struct curl_slist *resolve_local(const char *url)
{
    char host[256], entry[300], ip[INET_ADDRSTRLEN]; const char *p = url + 8, *end = p + strcspn(p, "/?#"), *colon;
    const char *at = memchr(p, '@', (size_t)(end - p));
    if (at) p = at + 1;
    if (*p == '[') return NULL;                         /* an IPv6 address */
    colon = memchr(p, ':', (size_t)(end - p));
    size_t hl = (size_t)((colon ? colon : end) - p);
    if (hl < 7 || hl >= sizeof host || strncasecmp(p + hl - 6, ".local", 6)) return NULL;
    memcpy(host, p, hl); host[hl] = 0;
    unsigned a = mdns_resolve4(host);
    if (!a || !inet_ntop(AF_INET, &a, ip, sizeof ip)) return NULL;     /* curl tries DNS: some servers serve .local */
    snprintf(entry, sizeof entry, "%s:%.*s:%s", host, colon ? (int)(end - colon - 1) : 3, colon ? colon + 1 : "443", ip);
    return curl.slist_append(NULL, entry);
}

int https_open(const char *url)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, curl_load);
    if (!curl_ok) { fprintf(stderr, "media: https is not available: %s\n", url); return -1; }
    int sv[2]; struct xfer *x = calloc(1, sizeof *x); pthread_t t; pthread_attr_t at;
    if (!x) return -1;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv)) { free(x); return -1; }
    if (!(x->h = curl.init())) { close(sv[0]); close(sv[1]); free(x); return -1; }
    pthread_mutex_init(&x->m, NULL); pthread_cond_init(&x->c, NULL);
    x->refs = 2; x->wfd = sv[1];
    snprintf(x->url, sizeof x->url, "%s", url);
    void *h = x->h; int v = https_verify(-1);
    curl.setopt(h, OPT_URL, x->url);
    curl.setopt(h, OPT_USERAGENT, "hassmic/" VERSION);
    curl.setopt(h, OPT_PROTOCOLS, (long)PROTO_HTTP_S); curl.setopt(h, OPT_REDIR_PROTOCOLS, (long)PROTO_HTTP_S);
    curl.setopt(h, OPT_FOLLOWLOCATION, 1L); curl.setopt(h, OPT_MAXREDIRS, 5L);
    curl.setopt(h, OPT_FAILONERROR, 1L); curl.setopt(h, OPT_NOSIGNAL, 1L);
    /* as the plain fetch: 4 s per connect there (TLS adds its handshake here), 15 s without data (TTS may still be
     * rendering); no limit on the whole: play_media may be a long file */
    curl.setopt(h, OPT_CONNECTTIMEOUT, 10L);
    curl.setopt(h, OPT_LOW_SPEED_LIMIT, 1L); curl.setopt(h, OPT_LOW_SPEED_TIME, 15L);
    curl.setopt(h, OPT_SSL_VERIFYPEER, v ? 1L : 0L); curl.setopt(h, OPT_SSL_VERIFYHOST, v ? 2L : 0L);
    const char *cf = https_ca_file(), *cp = https_ca_path();        /* a store that exists (https_ca_file says) */
    if (cf) curl.setopt(h, OPT_CAINFO, cf);
    if (cp) curl.setopt(h, OPT_CAPATH, cp);
    if ((x->resolve = resolve_local(url))) curl.setopt(h, OPT_RESOLVE, x->resolve);
    curl.setopt(h, OPT_HEADERFUNCTION, on_header); curl.setopt(h, OPT_HEADERDATA, x);
    curl.setopt(h, OPT_WRITEFUNCTION, on_data); curl.setopt(h, OPT_WRITEDATA, x);
    curl.setopt(h, OPT_OPENSOCKETFUNCTION, on_socket); curl.setopt(h, OPT_OPENSOCKETDATA, NULL);
    pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &at, run, x)) {
        pthread_attr_destroy(&at); close(sv[0]); close(sv[1]); curl.cleanup(h);
        if (x->resolve) curl.slist_free_all(x->resolve);
        pthread_mutex_destroy(&x->m); pthread_cond_destroy(&x->c); free(x);
        return -1;
    }
    pthread_attr_destroy(&at);
    pthread_mutex_lock(&x->m);
    while (!x->state) pthread_cond_wait(&x->c, &x->m);
    int ok = x->state == 1;
    pthread_mutex_unlock(&x->m);
    put(x);
    if (!ok) { close(sv[0]); return -1; }
    return sv[0];
}
