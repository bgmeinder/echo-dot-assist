/* whisper_voice.c on synthetic speech: "voiced" is a glottal pulse train (100..220 Hz, with jitter and a slow glide)
 * through two resonances, as a vowel; "whispered" is white noise through the same resonances, the breath a whisper is.
 * Both with 0.6 s of quiet room first (the wake word's tail the detector skips) and after, at 30 dB over the room.
 * Then what it must not score: a request too short to tell, and one with nothing over the floor. */
#include "whisper_voice.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define RATE 16000
static int fails;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

static unsigned rng = 12345;
static float noise(void) { rng = rng * 1103515245u + 12345u; return ((rng >> 8) & 0xffff) / 32768.f - 1.f; }

/* two-pole resonator */
struct res { float a1, a2, y1, y2; };
static void res_init(struct res *r, float f, float bw)
{
    float rr = expf(-(float)M_PI * bw / RATE), th = 2 * (float)M_PI * f / RATE;
    r->a1 = 2 * rr * cosf(th); r->a2 = -rr * rr; r->y1 = r->y2 = 0;
}
static float res_run(struct res *r, float x) { float y = x + r->a1 * r->y1 + r->a2 * r->y2; r->y2 = r->y1; r->y1 = y; return y; }

/* secs of "speech" (voiced or whispered), lead and tail of room noise; the speech's level about 30 dB over the room */
static int16_t *make(int voiced, float secs, float lead, float f0, size_t *n)
{
    size_t len = (size_t)((lead + secs + 0.4f) * RATE), sp0 = (size_t)(lead * RATE), sp1 = sp0 + (size_t)(secs * RATE);
    int16_t *x = malloc(len * sizeof *x);
    struct res r1, r2; res_init(&r1, 700, 130); res_init(&r2, 1200, 150);
    float phase = 0;
    for (size_t i = 0; i < len; i++) {
        float room = 60 * noise(), s = 0;
        if (i >= sp0 && i < sp1) {
            float t = (float)(i - sp0) / RATE, f = f0 * (1 + 0.15f * sinf(2 * (float)M_PI * 0.7f * t)) * (1 + 0.01f * noise());
            float ex;
            if (voiced) { phase += f / RATE; ex = 0; if (phase >= 1) { phase -= 1; ex = 1; } ex = ex * 40 + 0.02f * noise(); }
            else ex = noise();
            float env = 0.6f + 0.4f * sinf(2 * (float)M_PI * 3.5f * t);   /* syllables */
            s = res_run(&r2, res_run(&r1, ex)) * env * (voiced ? 40 : 300);
        }
        float v = room + s;
        x[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    *n = len;
    return x;
}

static int score(int16_t *x, size_t n, int *sp, int *vo)
{
    wv_begin();
    for (size_t i = 0; i < n; i += 320) wv_feed(x + i, n - i < 320 ? n - i : 320);   /* 20 ms blocks, as the capture */
    int c = wv_end();
    wv_counts(sp, vo);
    return c;
}

int main(void)
{
    char what[160]; int sp, vo, c;
    static const float f0s[] = { 100, 140, 220 };
    for (int k = 0; k < 3; k++) {
        size_t n; int16_t *x = make(1, 1.5f, 0.6f, f0s[k], &n);
        c = score(x, n, &sp, &vo); free(x);
        snprintf(what, sizeof what, "voiced at %.0f Hz: not whispered (confidence %d, %d of %d speech frames voiced)", f0s[k], c, vo, sp);
        check(c >= 0 && c <= WV_THRESHOLD && sp >= 50, what);
    }
    for (int k = 0; k < 3; k++) {
        size_t n; int16_t *x = make(0, 1.5f, 0.6f, f0s[k], &n);
        c = score(x, n, &sp, &vo); free(x);
        snprintf(what, sizeof what, "whispered: whispered (confidence %d, %d of %d speech frames voiced)", c, vo, sp);
        check(c > WV_THRESHOLD && sp >= 50, what);
    }
    { size_t n; int16_t *x = make(1, 0.08f, 0.6f, 140, &n);
      c = score(x, n, &sp, &vo); free(x);
      snprintf(what, sizeof what, "80 ms of speech: too little to tell (%d, %d frames)", c, sp);
      check(c == -1, what); }
    { size_t n; int16_t *x = make(0, 0, 0.6f, 140, &n);
      c = score(x, n, &sp, &vo); free(x);
      snprintf(what, sizeof what, "the room alone: nothing to tell (%d, %d frames)", c, sp);
      check(c == -1, what); }
    { /* the skip: a loud voiced wake word in the first 0.5 s, then a whisper: still whispered */
      size_t nv, nw; int16_t *v = make(1, 0.45f, 0, 140, &nv), *w = make(0, 1.5f, 0.1f, 140, &nw);
      wv_begin(); wv_feed(v, 8000 < nv ? 8000 : nv); wv_feed(w, nw);
      c = wv_end(); wv_counts(&sp, &vo); free(v); free(w);
      snprintf(what, sizeof what, "a voiced wake word in the first 0.5 s, then a whisper: whispered (%d)", c);
      check(c > WV_THRESHOLD, what); }
    return fails != 0;
}
