/* Amazon's analog clock faces, read from the firmware's package where it lies (clockface.h says what is taken).
 *
 * The package is a zip: its central directory read once, an entry's data read when wanted, stored or deflated (zlib).
 * Every offset and size is checked against the file: the package is the firmware's, but a damaged one must not take
 * hassmic down.  Templates are read at the first use and kept (a few kB of names); faces and backgrounds are parsed
 * again for every frame they are drawn in, from their small JSON (under 4 kB each), so nothing else is kept. */
#include "clockface.h"
#include "board.h"
#include "json.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#define MAX_ENTRY (8u << 20)           /* the largest entry read (the package's biggest image is ~1 MB) */
#define MAX_TMPL 48
#define MAX_PER 16                     /* faces or backgrounds a template has (the most is 8) */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int loaded;
struct tmpl {
    char id[48];
    const char *styles[MAX_PER], *colors[MAX_PER];      /* malloc'd ids */
    int nstyles, ncolors, def_style, def_color;
};
static struct tmpl tm_[MAX_TMPL];
static int ntm;
static const char *names[MAX_TMPL + 1];

/* the package where the firmware has it, or where the install moved it: scripts/system/sysinstall.sh renames the
 * Alexa apps it takes out of Android's sight to /system/hassmic-removed/<the same path below /system> (the Spot's
 * device.conf lists this one: a package of Amazon's clock UI, which Android need not scan); read there as sounds.c does */
static const char *apk(void)
{
    static char moved[256];
    const char *e = getenv("HASSMIC_CLOCK_FACES"), *p = e ? e : board.clock_faces;
    if (!p || !access(p, R_OK) || strncmp(p, "/system/", 8)) return p;
    snprintf(moved, sizeof moved, "/system/hassmic-removed/%s", p + 8);
    return access(moved, R_OK) ? p : moved;
}
const char *clockface_apk(void) { return apk() ? apk() : ""; }
const char *clockface_package(void)
{
    const char *e = getenv("HASSMIC_CLOCK_FACES_PKG");
    return e ? e : board.clock_faces_pkg ? board.clock_faces_pkg : "";
}

/* ---------------------------------------------------------------- the zip */

static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned le16(const unsigned char *p) { return p[0] | p[1] << 8; }

struct zent { char name[128]; uint32_t at, csize, usize; unsigned method; };
static struct zent *zents;
static int nzents;

/* the central directory: from the end record in the file's last 64 kB.  0, or -1 */
static int zip_open(FILE *f)
{
    unsigned char tail[65536 + 22];
    if (fseek(f, 0, SEEK_END)) return -1;
    long size = ftell(f), from = size > (long)sizeof tail ? size - (long)sizeof tail : 0;
    size_t n = (size_t)(size - from);
    if (fseek(f, from, SEEK_SET) || fread(tail, 1, n, f) != n) return -1;
    long e = -1;
    for (long i = (long)n - 22; i >= 0; i--) if (le32(tail + i) == 0x06054b50) { e = i; break; }
    if (e < 0) return -1;
    uint32_t count = le16(tail + e + 10), cdsize = le32(tail + e + 12), cdat = le32(tail + e + 16);
    if ((long)cdat + (long)cdsize > size || cdsize > (32u << 20)) return -1;
    unsigned char *cd = malloc(cdsize ? cdsize : 1);
    if (!cd || fseek(f, (long)cdat, SEEK_SET) || fread(cd, 1, cdsize, f) != cdsize) { free(cd); return -1; }
    zents = calloc(count ? count : 1, sizeof *zents); nzents = 0;
    for (uint32_t p = 0, i = 0; zents && i < count && p + 46 <= cdsize; i++) {
        if (le32(cd + p) != 0x02014b50) break;
        unsigned nl = le16(cd + p + 28), xl = le16(cd + p + 30), cl = le16(cd + p + 32);
        if (p + 46 + nl > cdsize) break;
        struct zent *z = &zents[nzents];
        z->method = le16(cd + p + 10); z->csize = le32(cd + p + 20); z->usize = le32(cd + p + 24); z->at = le32(cd + p + 42);
        if (nl < sizeof z->name) { memcpy(z->name, cd + p + 46, nl); z->name[nl] = 0; nzents++; }
        p += 46 + nl + xl + cl;
    }
    free(cd);
    return zents ? 0 : -1;
}

/* an entry's data, malloc'd and NUL-terminated (for the JSON), its length in *len; NULL: none or unreadable */
static unsigned char *zip_read(const char *name, size_t *len)
{
    const char *path = apk();
    if (!path) return NULL;
    struct zent *z = NULL;
    for (int i = 0; i < nzents; i++) if (!strcmp(zents[i].name, name)) { z = &zents[i]; break; }
    if (!z || z->usize > MAX_ENTRY || z->csize > MAX_ENTRY || (z->method != 0 && z->method != 8)) return NULL;
    FILE *f = fopen(path, "rb");
    unsigned char h[30], *c = NULL, *out = NULL;
    if (!f || fseek(f, (long)z->at, SEEK_SET) || fread(h, 1, 30, f) != 30 || le32(h) != 0x04034b50) goto done;
    if (fseek(f, (long)z->at + 30 + le16(h + 26) + le16(h + 28), SEEK_SET)) goto done;
    if (!(c = malloc(z->csize + 1)) || fread(c, 1, z->csize, f) != z->csize) goto done;
    if (!(out = malloc(z->usize + 1))) goto done;
    if (z->method == 0) {
        if (z->csize != z->usize) { free(out); out = NULL; goto done; }
        memcpy(out, c, z->usize);
    } else {
        z_stream s; memset(&s, 0, sizeof s);
        s.next_in = c; s.avail_in = z->csize; s.next_out = out; s.avail_out = z->usize;
        int ok = inflateInit2(&s, -MAX_WBITS) == Z_OK && inflate(&s, Z_FINISH) == Z_STREAM_END && s.total_out == z->usize;
        inflateEnd(&s);
        if (!ok) { free(out); out = NULL; goto done; }
    }
    out[z->usize] = 0; *len = z->usize;
done:
    free(c);
    if (f) fclose(f);
    return out;
}

static int parse(const char *entry, struct json *j, unsigned char **buf)
{
    size_t n = 0;
    memset(j, 0, sizeof *j);                    /* json_parse grows j's token array: it must start empty */
    if (!(*buf = zip_read(entry, &n))) return -1;
    if (json_parse(j, (const char *)*buf, n)) { json_free(j); free(*buf); *buf = NULL; return -1; }
    return 0;
}

/* ---------------------------------------------------------------- templates */

static int by_id(const void *a, const void *b) { return strcmp(((const struct tmpl *)a)->id, ((const struct tmpl *)b)->id); }

/* the analog faces of a face list: kept by id when its own JSON says is_analog */
static int analog(const char *face)
{
    char e[160]; struct json j; unsigned char *b; int r = 0;
    snprintf(e, sizeof e, "assets/raw/clock_face_%s", face);
    if (parse(e, &j, &b)) return 0;
    r = json_bool(&j, json_get(&j, 0, "is_analog"), 0);
    json_free(&j); free(b);
    return r;
}

static void load(void)
{
    if (loaded) return;
    loaded = 1;
    names[0] = "digital";                       /* the choice there always is, package or not */
    const char *path = apk();
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return;
    int ok = !zip_open(f);
    fclose(f);
    if (!ok) { fprintf(stderr, "clock faces: %s is not a package that can be read\n", path); return; }
    static const char pre[] = "assets/raw/clock_template_";
    for (int i = 0; i < nzents && ntm < MAX_TMPL; i++) {
        if (strncmp(zents[i].name, pre, sizeof pre - 1)) continue;
        struct json j; unsigned char *b;
        if (parse(zents[i].name, &j, &b)) continue;
        struct tmpl *t = &tm_[ntm]; memset(t, 0, sizeof *t);
        json_str(&j, json_get(&j, 0, "id"), t->id, sizeof t->id);
        char def_face[96] = "", def_bg[96] = "", v[96];
        int d = json_get(&j, 0, "default");
        json_str(&j, json_get(&j, d, "face"), def_face, sizeof def_face);
        json_str(&j, json_get(&j, d, "background"), def_bg, sizeof def_bg);
        JSON_EACH(&j, json_get(&j, 0, "face_ids"), k) {
            if (t->nstyles < MAX_PER && json_str(&j, k, v, sizeof v) && analog(v)) {
                if (!strcmp(v, def_face)) t->def_style = t->nstyles;
                t->styles[t->nstyles++] = strdup(v);
            }
        }
        JSON_EACH(&j, json_get(&j, 0, "background_ids"), k) {
            if (t->ncolors < MAX_PER && json_str(&j, k, v, sizeof v)) {
                if (!strcmp(v, def_bg)) t->def_color = t->ncolors;
                t->colors[t->ncolors++] = strdup(v);
            }
        }
        /* a photo_provider's backgrounds are pictures from Amazon's services (Photos: ZORDON, the wallpapers: CURATIONS_*),
         * not in the package: none of those here, hassmic stays local */
        int online = json_get(&j, 0, "photo_provider") >= 0;
        json_free(&j); free(b);
        if (t->id[0] && t->nstyles && t->ncolors && !online) ntm++;
        else { for (int k = 0; k < t->nstyles; k++) free((void *)t->styles[k]); for (int k = 0; k < t->ncolors; k++) free((void *)t->colors[k]); }
    }
    qsort(tm_, (size_t)ntm, sizeof *tm_, by_id);
    for (int i = 0; i < ntm; i++) names[i + 1] = tm_[i].id;
    fprintf(stderr, "clock faces: %d templates with an analog face in %s\n", ntm, path);
}

int clockface_count(void) { pthread_mutex_lock(&lock); load(); int n = ntm; pthread_mutex_unlock(&lock); return n; }
const char *const *clockface_names(int *n) { pthread_mutex_lock(&lock); load(); *n = ntm + 1; pthread_mutex_unlock(&lock); return names; }

static struct tmpl *tmpl_at(int i) { load(); return i >= 0 && i < ntm ? &tm_[i] : NULL; }
const char *const *clockface_styles(int i, int *n)
{
    pthread_mutex_lock(&lock); struct tmpl *t = tmpl_at(i); *n = t ? t->nstyles : 0; pthread_mutex_unlock(&lock);
    return t ? t->styles : NULL;
}
const char *const *clockface_colors(int i, int *n)
{
    pthread_mutex_lock(&lock); struct tmpl *t = tmpl_at(i); *n = t ? t->ncolors : 0; pthread_mutex_unlock(&lock);
    return t ? t->colors : NULL;
}
int clockface_default_style(int i) { pthread_mutex_lock(&lock); struct tmpl *t = tmpl_at(i); int r = t ? t->def_style : 0; pthread_mutex_unlock(&lock); return r; }
int clockface_default_color(int i) { pthread_mutex_lock(&lock); struct tmpl *t = tmpl_at(i); int r = t ? t->def_color : 0; pthread_mutex_unlock(&lock); return r; }

/* ---------------------------------------------------------------- what to draw */

/* "#RRGGBB", "#AARRGGBB", or a few names; anything else (a resource's name: Alexa's alert colours) none */
static unsigned colour(const char *s)
{
    if (!strcmp(s, "black")) return 0xFF000000u;
    if (!strcmp(s, "white")) return 0xFFFFFFFFu;
    if (s[0] != '#') return 0;
    size_t n = strlen(s + 1); char *e;
    unsigned long v = strtoul(s + 1, &e, 16);
    if (*e || (n != 6 && n != 8)) return 0;
    return n == 6 ? 0xFF000000u | (unsigned)v : (unsigned)v;
}

/* one image layer (a background's or a face's): day_src/night_src or src, day_color/night_color, center_x/center_y */
static int image_op(struct json *j, int l, int night, struct cf_op *op)
{
    char v[96] = "";
    if (!json_str(j, json_get(j, l, night ? "night_src" : "day_src"), op->name, sizeof op->name) &&
        !json_str(j, json_get(j, l, "src"), op->name, sizeof op->name)) return 0;
    op->tint = json_str(j, json_get(j, l, night ? "night_color" : "day_color"), v, sizeof v) ? colour(v) : 0;
    int cx = json_get(j, l, "center_x"), cy = json_get(j, l, "center_y");
    op->cx = cx >= 0 && cy >= 0 ? (int)json_int(j, cx, -1) : -1;
    op->cy = cx >= 0 && cy >= 0 ? (int)json_int(j, cy, -1) : -1;
    op->rot = 0;
    return 1;
}

/* a layer for the date only (SHOWS_DATE): left out, as the date window is */
static int date_only(struct json *j, int l)
{
    JSON_EACH(j, json_get(j, l, "layer_properties"), k) if (json_streq(j, k, "SHOWS_DATE")) return 1;
    return 0;
}

int clockface_ops(int ti, int style, int color, const struct tm *tm, int night, struct cf_op *ops, int max)
{
    char face[96], bg[96], e[160];
    pthread_mutex_lock(&lock);
    struct tmpl *t = tmpl_at(ti);
    if (!t) { pthread_mutex_unlock(&lock); return -1; }
    snprintf(face, sizeof face, "%s", t->styles[style >= 0 && style < t->nstyles ? style : t->def_style]);
    snprintf(bg, sizeof bg, "%s", t->colors[color >= 0 && color < t->ncolors ? color : t->def_color]);
    pthread_mutex_unlock(&lock);
    int n = 0; struct json j; unsigned char *b;
    snprintf(e, sizeof e, "assets/raw/clock_background_%s", bg);
    if (!parse(e, &j, &b)) {
        JSON_EACH(&j, json_get(&j, 0, "layers"), l)
            if (n < max && json_streq(&j, json_get(&j, l, "type"), "ImageLayer") && image_op(&j, l, night, &ops[n])) n++;
        json_free(&j); free(b);
    }
    snprintf(e, sizeof e, "assets/raw/clock_face_%s", face);
    if (parse(e, &j, &b)) return n ? n : -1;
    double s = tm->tm_sec, m = tm->tm_min + s / 60, h = tm->tm_hour % 12 + m / 60;
    JSON_EACH(&j, json_get(&j, 0, "layers"), l) {
        if (n >= max || date_only(&j, l)) continue;
        int type = json_get(&j, l, "type");
        if (json_streq(&j, type, "ImageLayer")) { if (image_op(&j, l, night, &ops[n])) n++; continue; }
        if (!json_streq(&j, type, "RotatingHandLayer") && !json_streq(&j, type, "MaskedRotatingHandLayer")) continue;
        if (!image_op(&j, l, night, &ops[n])) continue;
        int r = json_get(&j, l, "hand_rotation");
        double deg = json_streq(&j, r, "HOUR_12") ? h * 30 : json_streq(&j, r, "HOUR_24") ? (tm->tm_hour + m / 60) * 15
                   : json_streq(&j, r, "MINUTE") ? m * 6 : json_streq(&j, r, "SECOND") ? s * 6 : -1;
        if (deg < 0) continue;
        ops[n].rot = (int)(deg * 100 + 0.5) % 36000; ops[n].cx = ops[n].cy = -1;
        n++;
    }
    json_free(&j); free(b);
    return n;
}

unsigned char *clockface_thumb(int ti, size_t *len)
{
    char e[160];
    pthread_mutex_lock(&lock); struct tmpl *t = tmpl_at(ti);
    if (t) snprintf(e, sizeof e, "res/drawable/img_list_clockface_%s.png", t->id);
    pthread_mutex_unlock(&lock);
    return t ? zip_read(e, len) : NULL;
}
