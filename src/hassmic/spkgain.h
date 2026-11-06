/* Speaker gain (issue #19): an analog gain stage in front of the speaker amplifier (board.speaker_gain), raised from the
 * settings page for an Echo that is too quiet at full volume.  Absent where the board names none or card 0 lacks it. */
#ifndef SPKGAIN_H
#define SPKGAIN_H

#define SPKGAIN_MAX 12                  /* dB over stock the page offers at most; see spkgain.c for why no more */

int  spkgain_present(void);             /* probed at the first call */
int  spkgain_max(void);                 /* what this Echo's control has above stock, up to SPKGAIN_MAX; 0: absent */
int  spkgain_min(void);                 /* what it offers below stock (0 or negative: board.speaker_gain_min_db) */
int  spkgain_get(void);                 /* dB over stock, spkgain_min()..spkgain_max() */
void spkgain_set(int db);               /* applied at once, and again whenever the audio HAL puts stock back */
/* board.speaker_unmute: the amplifier's mute switched on and off again, before a player opens (audio_android.c): the
 * Echo Spot's codec takes the GPIO line back to muted on its own after a while idle with the control still reading
 * off, so a plain "off" when found on is not enough.  Nothing where the board names no switch */
void spkgain_unmute_pulse(void);
#endif
