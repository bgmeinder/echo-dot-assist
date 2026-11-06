/* Analog clock faces from the firmware's own clock-face package (board.clock_faces: the Echo Spot's
 * com.amazon.selector.clock.resources.apk), drawn on a round screen in place of the digital clock (ring_band.c).  The
 * package holds Amazon's faces as data: per template (assets/raw/clock_template_<id>) a list of backgrounds
 * (clock_background_<id>: a day and a night image) and faces (clock_face_<id>: layers of images, tinted by day and by
 * night, and rotating hands); the images are the package's drawables, which the renderer draws by name through
 * Android's own Resources (layer-lists, tints and all).  Nothing of the package is copied: it is read where it is.
 * Only the analog faces are offered (the digital ones stay hassmic's own clock); each template's dials (its analog faces)
 * and backgrounds can be picked (settings clock_dial, clock_background), and its date window shown (clock_date: the
 * window's image and the day of the month in it); notification complications and Alexa's alerts are left out. */
#ifndef CLOCKFACE_H
#define CLOCKFACE_H
#include <stddef.h>
#include <time.h>

/* one thing for the renderer to draw: drawable `name` of the package, rotated by rot (hundredths of a degree,
 * clockwise, about the screen's centre), tinted (0xAARRGGBB, 0: as it is); centred on (cx, cy) of the 480 x 480 face
 * at its own size, or filling the face when cx is -1; mask[0] set: shown only where drawable `mask` (filling the face,
 * not rotated) is opaque (stellar's sky disc through its window).  Or, text[0] set: that text (the date window's day)
 * centred on cx with its baseline at cy, size px high, in colour tint */
struct cf_op { char name[96]; int rot; unsigned tint; int cx, cy; char text[8]; int size; char mask[96]; };

int clockface_count(void);                     /* templates with an analog face (0: no package, or none) */
const char *clockface_package(void);           /* the package's name, for the renderer's Resources ("" none) */
const char *clockface_apk(void);               /* its path */
/* the setting's choices: "digital" and the templates' ids, in a stable order (sorted); count in *n */
const char *const *clockface_names(int *n);
/* a template's (by index into clockface_names - 1) analog faces and backgrounds, by id */
const char *const *clockface_styles(int tmpl, int *n);
const char *const *clockface_colors(int tmpl, int *n);
int clockface_default_style(int tmpl);
int clockface_default_color(int tmpl);
/* the same lists as choices people read and settings keep: each id without its template's "<id>_" (and a dial's
 * "analog_"): beach's "beach_analog_tapered" is "tapered", "beach_ocean" "ocean"; in the same order */
const char *const *clockface_dial_names(int tmpl, int *n);
const char *const *clockface_background_names(int tmpl, int *n);
/* what to draw for template tmpl's face `style` on background `color` (-1: the template's default) at time tm (night:
 * the night images and colours; date: the date window with the day in it, where the face has one): the number of ops
 * written (at most max), -1 when there is no such face */
int clockface_ops(int tmpl, int style, int color, int date, const struct tm *tm, int night, struct cf_op *ops, int max);
/* the template's thumbnail as Amazon's selector showed it (PNG), malloc'd; NULL: none */
unsigned char *clockface_thumb(int tmpl, size_t *len);
/* background `color`'s small picture (its styled_background, the image Amazon's home screen took for it), malloc'd */
unsigned char *clockface_background_thumb(int tmpl, int color, size_t *len);
#endif
