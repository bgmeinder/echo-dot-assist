/* hassmic's own whisper detector (whisper_voice.c): whether a request was whispered, from how much of its speech has a
 * pitch.  No model; the fallback where Amazon's whisper model is not there (whisper_pryon.c), which is every Echo
 * whose device type Amazon does not hand it to (the Echo Spot, the Echo Show 5).  Requests are scored one at a time:
 * wv_begin, wv_feed (16 kHz mono), wv_end. */
#ifndef WHISPER_VOICE_H
#define WHISPER_VOICE_H
#include <stddef.h>
#include <stdint.h>

#define WV_THRESHOLD 640                 /* confidence above it: whispered (1000 x the unvoiced share; 0.36 voiced) */

void wv_begin(void);
void wv_feed(const int16_t *samples, size_t count);
/* the confidence (0..1000: the share of speech frames without a pitch), or -1 when there was too little speech to say
 * (fewer than WV_MIN_SPEECH frames over the floor) */
int  wv_end(void);
/* for tests: the number of speech frames and of voiced ones the last wv_end counted */
void wv_counts(int *speech, int *voiced);
#endif
