/* dha_test: what the Echo's device attestation HAL (libacehal_dha.so -> /system/lib/hw/amzn_dha.<soc>.so, Amazon's
 * keymaster) gives: the public key, the fields MAP puts into its registration token, and a signature over a test hash.
 * MAP (libace_map.so map_registration_create_dha_jwt) signs /auth/register's device_authentication_token with it; see
 * src/hassmic/davs.c.  The private key never leaves the HAL.
 *   dha_test                      through libacehal_dha.so (the Dots)
 *   dha_test hw [id] [mod|dev]    without it (rook, Fire OS 5): Android's own loader, libhardware's
 *                                 hw_get_module_by_class("amzn_dha"), as the firmware's amzn_dha_tool does, then the
 *                                 module's open(id) (default "amzn_dha"); the amznkey_* calls get the module (mod, the
 *                                 default) or the device open returned (dev)
 * Calls (from libacehal_dha.so and its caller in libace_map.so):
 *   int aceDhaHal_open(void)                                    0 = ok, reference counted
 *   int aceDhaHal_getPublicKey(uint8_t **der, uint32_t *len)    malloc'd SubjectPublicKeyInfo
 *   int aceDhaHal_getField(int id, char **val, uint32_t *len)   malloc'd; 1 = cpuid, 0x204 = "mac" of the JWT header
 *   int aceDhaHal_signData(const uint8_t *data, uint32_t len, uint8_t **sig, uint32_t *siglen)
 *   int aceDhaHal_close(void) */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(const char *what, const uint8_t *p, uint32_t n)
{
    printf("%s (%u):", what, n);
    for (uint32_t i = 0; i < n && i < 600; i++) printf(i % 32 ? "%02x" : "\n  %02x", p[i]);
    int text = n > 0;
    for (uint32_t i = 0; i < n; i++) if ((p[i] < 32 || p[i] > 126) && !(i == n - 1 && !p[i])) text = 0;
    if (text) printf("\n  \"%.*s\"", (int)n, (const char *)p);
    printf("\n");
}

/* the module's own functions (exported under their C++ names), each with the module first */
static const void *arg0;
static int (*k_pub)(const void *, uint8_t **, uint32_t *);
static int (*k_field)(const void *, int, void **, uint32_t *);
static int (*k_sign)(const void *, const uint8_t *, uint32_t, uint8_t **, uint32_t *);
static int hw_open(void) { return 0; }
static int hw_pubkey(uint8_t **der, uint32_t *len) { return k_pub(arg0, der, len); }
static int hw_field(int id, uint8_t **val, uint32_t *len) { return k_field(arg0, id, (void **)val, len); }
static int hw_sign(const uint8_t *d, uint32_t n, uint8_t **sig, uint32_t *sl) { return k_sign(arg0, d, n, sig, sl); }

/* hw_module_t's start, 32-bit: tag, two versions, id, name, author, methods */
struct hw_module_head { uint32_t tag; uint16_t mv, hv; const char *id, *name, *author;
                        struct { int (*open)(const void *, const char *, void **); } *methods; };

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);       /* every line out before a crash */
    int (*op)(void); int (*pk)(uint8_t **, uint32_t *); int (*gf)(int, uint8_t **, uint32_t *);
    int (*sg)(const uint8_t *, uint32_t, uint8_t **, uint32_t *);
    if (argc > 1 && !strcmp(argv[1], "hw")) {
        const char *id = argc > 2 ? argv[2] : "amzn_dha";
        int use_dev = argc > 3 && !strcmp(argv[3], "dev");
        void *lh = dlopen("libhardware.so", RTLD_NOW);
        int (*get)(const char *, const char *, const void **) = lh ? dlsym(lh, "hw_get_module_by_class") : NULL;
        if (!get) { fprintf(stderr, "libhardware: %s\n", dlerror()); return 1; }
        const void *mod = NULL;
        int r = get("amzn_dha", NULL, &mod);
        printf("hw_get_module_by_class(amzn_dha): %d, module %p\n", r, mod);
        if (r || !mod) return 1;
        const struct hw_module_head *m = mod;
        printf("module id \"%s\" name \"%s\" author \"%s\" versions %#x %#x\n", m->id, m->name, m->author, m->mv, m->hv);
        void *dev = NULL;
        r = m->methods && m->methods->open ? m->methods->open(mod, id, &dev) : -1;
        printf("open(\"%s\"): %d, device %p\n", id, r, dev);
        void *self = dlopen("/system/lib/hw/amzn_dha.mt8163.so", RTLD_NOW | RTLD_NOLOAD);
        if (!self) { fprintf(stderr, "the module's handle: %s\n", dlerror()); return 1; }
        k_pub = (int (*)(const void *, uint8_t **, uint32_t *))dlsym(self, "_Z26amznkey_get_dha_key_publicPK17amzn_dha_module_tPPhPj");
        k_field = (int (*)(const void *, int, void **, uint32_t *))dlsym(self, "_Z17amznkey_get_fieldPK17amzn_dha_module_tiPPvPj");
        k_sign = (int (*)(const void *, const uint8_t *, uint32_t, uint8_t **, uint32_t *))dlsym(self, "_Z17amznkey_sign_dataPK17amzn_dha_module_tPKhjPPhPj");
        if (!k_pub || !k_field || !k_sign) { fprintf(stderr, "amznkey_* missing\n"); return 1; }
        arg0 = use_dev ? dev : mod;
        printf("calls with the %s (%p)\n", use_dev ? "device" : "module", arg0);
        op = hw_open; pk = hw_pubkey; gf = hw_field; sg = hw_sign;
    } else {
        void *h = dlopen("libacehal_dha.so", RTLD_NOW);
        if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
        op = dlsym(h, "aceDhaHal_open");
        pk = dlsym(h, "aceDhaHal_getPublicKey");
        gf = dlsym(h, "aceDhaHal_getField");
        sg = dlsym(h, "aceDhaHal_signData");
        if (!op || !pk || !gf || !sg) { fprintf(stderr, "dlsym failed\n"); return 1; }
    }
    int r = op();
    printf("open: %d\n", r);
    if (r) return 1;
    uint8_t *p = NULL; uint32_t n = 0;
    r = pk(&p, &n);
    printf("getPublicKey: %d\n", r);
    if (!r && p) { dump("public key DER", p, n); free(p); }
    static const int ids[] = { 1, 0x101, 0x201, 0x202, 0x203, 0x204 };
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) {
        p = NULL; n = 0;
        r = gf(ids[i], &p, &n);
        char what[32]; snprintf(what, sizeof what, "field %#x", ids[i]);
        if (r || !p) printf("%s: %d\n", what, r); else { dump(what, p, n); free(p); }
    }
    uint8_t hash[32];
    for (int i = 0; i < 32; i++) hash[i] = (uint8_t)i;
    p = NULL; n = 0;
    r = sg(hash, sizeof hash, &p, &n);
    printf("signData: %d\n", r);
    if (!r && p) { dump("signature", p, n); free(p); }
    return 0;
}
