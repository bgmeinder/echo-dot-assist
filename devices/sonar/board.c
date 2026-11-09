/* Echo Plus 1st gen (2017) (sonar), Fire OS 5.5.6.9 (LVY48F).  Same MT8163 generation as rook/crown: Android 5.1,
 * kernel 3.18.19 presumably (not yet confirmed from a running unit, only inferred from the lk/preloader strings).
 *
 * DRAFT — every value below except the identity strings is either copied from rook as a starting guess or left as a
 * literal TODO.  None of it has been checked against a running Echo Plus (no unit unlocked yet, no adb, no rootfs
 * unpacked beyond firmware/sonar/system/build.prop).  Do not install this against a real device: follow
 * devices/README.md's porting steps (getevent -il, find /sys -name "*privacy*", strings of the light sensor HAL,
 * ls -lZ of /dev nodes) and replace every TODO before trusting it. */
#include "board.h"
#include <stddef.h>

/* TODO: earcon_zip. rook's table (SystemUI/KnightSettings/comms app/alarms app paths) is Spot-specific app names and
 * resource entries; sonar's stock apps are not yet listed (devices/sonar/device.conf STOCK_APPS_OUT/ALEXA_PACKAGES
 * are themselves TODO). Leave NULL until `pm list packages` + unzip -l on a rooted Plus name the real paths — a
 * guessed path here would silently find nothing rather than fail loudly, which is worse than admitting it's unknown. */

const struct board board = {
    .model = "Echo Plus (sonar)",
    .project = "hassmic.echo-plus",
    .product = "Echo Plus (hassmic)",
    .default_name = "Echo Plus",                                   /* + MAC, as every model (main.c name_make) */
    .codename = "sonar",

    /* TODO: inputs. rook's keypad/action_combo/privacy_state were all read off a running Spot with getevent -il and
     * find /sys -name "*privacy*" (devices/rook/board.c's own comments say so). The Echo Plus 1st gen has volume
     * keys and a mute button like every Echo; whether it also has an action button (the Dots do, the Spot does not)
     * changes action_combo. Nothing here should be filled in without that output from an actual unit. */
    .keypad = NULL,                 /* TODO */
    .keypad2 = NULL,
    .privacy_state = NULL,          /* TODO: find /sys -name "*privacy*" on a booted, rooted Plus */
    .privacy_input = NULL,
    .action_combo = 0,              /* TODO: 1 only if there truly is no action button (verify against the device, not photos) */
    .grab_keys = 1,                 /* same reasoning as every Android model: EVIOCGRAB so the framework doesn't also act */
    .privacy_latch = 0,             /* TODO: 1 once privacy_state above is a real path */
    .privacy_button = NULL,
    .privacy_settle_ms = 0,

    /* TODO: Bluetooth. Do not assume rook's Broadcom UART part (BCM43569) carries over: the Echo Plus 1st gen's
     * Wi-Fi/BT chip has not been identified from the firmware pulled so far (device.conf WIFI_DRIVER is itself TODO).
     * Needs cat of the controller's init service / bt_vendor.conf, or lsmod + dmesg on a rooted unit. */
    .bt_dev = NULL,                 /* TODO */
    .bt_patch = NULL,
    .bt_baud = 0,
    .a2dp_max_kbps = 0,              /* TODO: measure once Bluetooth is up, as rook's comment describes doing */
    .a2dp_buffer_ms = 300,           /* rook's measured value as a starting default; re-measure on this unit */
    .bt_service = NULL,              /* TODO */
    .bt_mac = NULL,                  /* TODO */

    .wake_id = "alexa",
    .wake_manifest = NULL,           /* TODO: same manifest path question as rook's comment raises (older vs newer
                                        model layout) — needs the unpacked rootfs to find it under /system/local or
                                        wherever sonar keeps it */
    .earcon_dir = "/data/local/hassmic/earcon/",   /* same convention as every model: our own sounds, if any are added, win */
    .earcon_zip = NULL,              /* TODO, see the comment above the struct */
    .thermal_type = NULL,            /* TODO: ls /sys/class/thermal/thermal_zone*/type on a rooted unit (rook used "mtktscpu") */
    .light_sensor = { NULL, NULL, NULL },  /* TODO: strings of the light sensor HAL + cat the lux file that exists */
    .lux_dark = 0,                   /* 0 = the default (3 lux); re-measure once light_sensor is real, as rook's was */
    .jack_state = NULL,              /* TODO: the Echo Plus 1st gen may not have a headphone jack at all — confirm before guessing a path */
    .screen_round = 0,               /* headless (ro.build.configuration=headless, ro.boot.selinux=disable): no screen at all */

    .clock_faces = NULL,             /* no screen: not applicable */
    .clock_faces_pkg = NULL,
    .backlight = NULL,               /* no screen: not applicable */

    .volume_steps = 100,
    /* TODO: ring_leds. The Echo Plus 1st gen has a physical ring of LEDs (like the Dots/radar), not a band (that's
     * rook/crown/checkers' screen-bottom band). Leave 0 (a full ring) as the default rather than guessing a count;
     * confirm the LED count from led-resources / ledcontroller's layer config once the rootfs is unpacked, and
     * whether device.mk should link ring.c (LED ring) rather than ring_band.c for this model — almost certainly
     * ring.c, since there's no screen, but device.mk itself is also a draft (see devices/sonar/device.mk). */
    .ring_leds = 0,

    .volume_curve = NULL,            /* TODO: tools/vol-curve.py measurement, as the porting guide describes; NULL
                                        falls back to the Echo Dot 2's curve meanwhile */

    /* TODO: speaker gain. rook's control name ("HP Driver Gain Volume") is the Dot 2/Echo 2 codec family's; sonar's
     * amplifier/codec is not yet identified. Needs tinymix (or amixer) contents dumped off a rooted unit, cross-checked
     * against audio_device.xml/mixer_paths.xml in the (not yet unpacked) rootfs, per devices/README.md step 3. */
    .speaker_gain = NULL,
    .speaker_gain_stock = 0,
    .speaker_gain_steps = 0,
    .speaker_gain_db = 0,
    .speaker_gain_min_db = 0,
    .speaker_gain_limit = 0,
    .speaker_gain_alt = NULL,
    .speaker_unmute = NULL,          /* TODO: rook needed this because its amplifier boots muted and only stock's
                                        bootsound tool clears it; unknown yet whether sonar's amplifier has the same
                                        behaviour */
};
