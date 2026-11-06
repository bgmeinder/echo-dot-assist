/* Whisper detection without a model: whispered speech has no pitch.  The vocal folds do not vibrate, the sound is
 * breath noise shaped by the mouth, so the periodicity voiced speech has from 80 to 400 Hz is missing.  Per frame of
 * 32 ms (hop 16 ms): the level, and the highest normalised autocorrelation of the signal low-passed at 1 kHz over the
 * lags of 400..80 Hz.  At the end of the request: the floor is the 10th percentile of the frames' levels, speech is
 * the frames 12 dB over it, voiced the speech frames whose peak is over 0.60; a request with fewer than 36 % of its
 * speech frames voiced was whispered.
 *
 * Measured on the speech corpus (testdata/corpus, 2026-11-05): 30 voices, three of them whispering (OmniVoice's
 * "whisper" designs: de-f, de-m, en-us-f), 15 commands each, with white noise at 30 dB and at 15 dB SNR.  Voiced share
 * of the whispered: 0.06 .. 0.35; of the normal voices 0.25 .. 1.00, medians 0.60 .. 1.00, the lowest an elderly man's
 * gravelly voice.  At 0.36: every whispered command found, 0.37 % of normal ones called whispered.  A miss either way
 * costs little: the reply is played quieter, or not.  The knobs were swept (voicing 0.45 .. 0.65, the speech margin
 * 8 .. 16 dB); tests/unit/whisper_voice_ref.py is this file in numpy, tests/unit/whisper_voice_test.c holds the two
 * together.  As whisper_pryon.c does with Amazon's model, the first 0.5 s of a request are not scored: the wake word's
 * tail, spoken normally, and the wake sound. */
#include "whisper_voice.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FR          512                 /* samples a frame: 32 ms */
#define HOP         256
#define LAG_LO      40                  /* 400 Hz */
#define LAG_HI      200                 /* 80 Hz */
#define SKIP        8000                /* 0.5 s */
#define MAX_FRAMES  2048                /* 33 s: more is not looked at */
#define SPEECH_DB   12.f
#define VOICED_R    0.60f
#define WV_MIN_SPEECH 10                /* 160 ms of speech at least */

static float buf[FR + LAG_HI], lp[FR + LAG_HI];  /* the frame and the lags after it: raw (DC removed) and low-passed */
static int have;                                 /* samples in buf */
static float hp_x, hp_y, s1, s2;                 /* DC high-pass, the two low-pass stages */
static float lev[MAX_FRAMES], peak[MAX_FRAMES];
static int nfr, skipped, last_speech, last_voiced;

void wv_begin(void)
{
    have = 0; nfr = 0; skipped = 0;
    hp_x = hp_y = s1 = s2 = 0;
}

static void frame(void)
{
    if (nfr >= MAX_FRAMES) return;
    double e = 0, e0 = 0;
    for (int i = 0; i < FR; i++) { e += (double)buf[i] * buf[i]; e0 += (double)lp[i] * lp[i]; }
    float best = 0;
    double eb = 0;                                /* the energy of the lagged window, slid one sample at a time */
    for (int i = LAG_LO; i < LAG_LO + FR; i++) eb += (double)lp[i] * lp[i];
    for (int t = LAG_LO; t <= LAG_HI; t++) {
        double c = 0;
        for (int i = 0; i < FR; i++) c += (double)lp[i] * lp[i + t];
        double den = sqrt(e0 * eb) + 1e-9;
        float r = (float)(c / den);
        if (r > best) best = r;
        if (t < LAG_HI) eb += (double)lp[t + FR] * lp[t + FR] - (double)lp[t] * lp[t];
    }
    lev[nfr] = 10.f * log10f((float)(e / FR) + 1e-3f);
    peak[nfr] = best;
    nfr++;
}

void wv_feed(const int16_t *samples, size_t count)
{
    static const float a = 0.67523f;              /* exp(-2 pi 1000 / 16000): one pole at 1 kHz */
    for (size_t k = 0; k < count; k++) {
        if (skipped < SKIP) { skipped++; continue; }
        float x = samples[k];
        float y = x - hp_x + 0.995f * hp_y;       /* DC away */
        hp_x = x; hp_y = y;
        s1 = a * s1 + (1 - a) * y; s2 = a * s2 + (1 - a) * s1;
        buf[have] = y; lp[have] = s2; have++;
        if (have == FR + LAG_HI) {
            frame();
            memmove(buf, buf + HOP, (size_t)(have - HOP) * sizeof *buf);
            memmove(lp, lp + HOP, (size_t)(have - HOP) * sizeof *lp);
            have -= HOP;
        }
    }
}

static int cmp(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }

int wv_end(void)
{
    last_speech = last_voiced = 0;
    if (nfr < WV_MIN_SPEECH) return -1;
    static float sorted[MAX_FRAMES];
    memcpy(sorted, lev, (size_t)nfr * sizeof *lev);
    qsort(sorted, (size_t)nfr, sizeof *sorted, cmp);
    float floor_ = sorted[(nfr - 1) / 10];        /* the 10th percentile */
    for (int i = 0; i < nfr; i++)
        if (lev[i] > floor_ + SPEECH_DB) { last_speech++; if (peak[i] > VOICED_R) last_voiced++; }
    if (last_speech < WV_MIN_SPEECH) return -1;
    return (int)lroundf(1000.f * (float)(last_speech - last_voiced) / (float)last_speech);
}

void wv_counts(int *speech, int *voiced) { *speech = last_speech; *voiced = last_voiced; }
