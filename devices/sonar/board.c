/* Echo Plus 1st gen (2017) (sonar), Fire OS 5.5.6.9 (LVY48F).  Same MT8163 generation as rook/crown: Android 5.1,
 * kernel 3.18.19 presumably (not yet confirmed from a running unit, only inferred from the lk/preloader strings).
 *
 * DRAFT — updated 2026-11-09 against the fully unpacked rootfs (firmware/sonar/rootfs, firmware/sonar/rootfs_ramdisk:
 * system.img rdumped, boot ramdisk cpio extracted), which confirms a good deal of this without a live unit.  What is
 * still marked TODO genuinely needs a running, rooted Echo Plus (getevent -il for the exact keypad/mute event
 * mapping, cat of whichever sysfs files exist, tinymix/amixer, ls -lZ): no unit has been unlocked yet (see
 * devices/sonar/README.md "USB access"). Do not install this against a real device. */
#include "board.h"
#include <stddef.h>

/* No earcon_zip needed at all, unlike rook: this firmware has a real earcon folder, confirmed 2026-11-09
 * (firmware/sonar/rootfs/system/local/share/earcon/base/, 768 files across base + per-locale folders de/en/en-AU/
 * en-CA/en-GB/en-IN/es-US/fr-CA/hi/ja), with the exact names sounds.c's board_sound convention expects as files:
 * ui_wakesound.wav, ui_wakesound_touch.wav, ui_endpointing.wav, ui_endpointing_touch.wav, state_privacy_mode_on.wav,
 * state_privacy_mode_off.wav, state_bluetooth_connected.mp3/_disconnected.mp3, comms_*.mp3, system_alerts_*.wav
 * (many alarm/notification tones, mp3 and wav mixed) — sounds.c's earcon_dir lookup (board.earcon_dir below) should
 * just work once pointed at that folder; earcon_zip is for firmware with no such folder (rook had none). */

const struct board board = {
    .model = "Echo Plus (sonar)",
    .project = "hassmic.echo-plus",
    .product = "Echo Plus (hassmic)",
    .default_name = "Echo Plus",                                   /* + MAC, as every model (main.c name_make) */
    .codename = "sonar",

    /* Keypad: system/usr/keylayout/mtk-kpd.kl exists (confirmed 2026-11-09) and, unlike rook's, maps MUTE (keycode
     * 113) and POWER (116) to separate Linux key codes rather than overloading KEY_POWER for mute — a real, distinct
     * mute key, not rook's hack. TODO: still needs getevent -il to confirm the input device's name is really "keys"
     * or "mtk-kpd" itself, and whether there's a separate action button (the Echo Plus 1st gen's photos show no
     * action button, same as the Spot, but device.conf's own notes elsewhere warn not to trust photos over a unit).
     *
     * Mute latch: likely the real gpio-privacy driver (the Dots' kind), NOT rook's KEY_POWER/mtk-kpd hack — found via
     * firmware/sonar/rootfs_ramdisk/init.csm2puffin.rc (an OTA-migration script preserving state across a platform
     * upgrade): "copy /sys/devices/platform/gpio-privacy/state /data/csm2puffin/original/gpio-privacy". That is
     * strong indirect evidence the node exists on this hardware (the whole point of that migration step is carrying
     * its *value* across an OS upgrade), but the exact path hasn't been `cat`-confirmed on a live unit yet. */
    .keypad = "name:keys",          /* TODO: confirm input device name with getevent -il */
    .keypad2 = NULL,
    .privacy_state = "/sys/devices/platform/gpio-privacy/state",   /* TODO: confirm by cat on a live unit */
    .privacy_input = NULL,
    .action_combo = 1,              /* TODO: no action button visible on this model, same reasoning as rook — verify */
    .grab_keys = 1,                 /* same reasoning as every Android model: EVIOCGRAB so the framework doesn't also act */
    .privacy_latch = 1,             /* TODO: 1 assuming gpio-privacy above is real and donut-like (a true latch) */
    .privacy_button = NULL,
    .privacy_settle_ms = 0,

    /* Bluetooth: confirmed 2026-11-09 from the unpacked rootfs — the exact same Broadcom part as rook (BCM43569A2),
     * on a UART again (system/etc/bluetooth/bt_vendor.conf: "UartPort = /dev/ttyMT1", identical to rook). The patch
     * file is a different revision of the same chip: BCM43569A2_001.003.004.0167.0216.hcd (rook's is …0215.hcd).
     * bt_baud/a2dp_* below are still rook's own measured values, not sonar's — this model's antenna/case/Wi-Fi
     * coexistence situation could measure differently; TODO re-measure once Bluetooth is actually up on a unit. */
    .bt_dev = "uart:/dev/ttyMT1",
    .bt_patch = "/system/vendor/firmware/BCM43569A2_001.003.004.0167.0216.hcd",
    .bt_baud = 3000000,              /* TODO: rook's value, libbt-vendor's default target — re-measure, don't assume */
    .a2dp_max_kbps = 0,              /* TODO: measure once Bluetooth is up, as rook's comment describes doing */
    .a2dp_buffer_ms = 300,           /* rook's measured value as a starting default; re-measure on this unit */
    .bt_service = NULL,
    .bt_mac = "/proc/idme/bt_mac_addr",   /* TODO: rook's path, same idme convention expected but unconfirmed here */

    .wake_id = "alexa",
    /* Confirmed 2026-11-09: system/local/models/keyword/en-US/ALEXA/pryon.manifest exists, with pryon.config
     * alongside the decoders (alexa-wakeworddecoder/, dvaddecoder/, etc.) — the exact same "older model layout"
     * rook's comment describes, at the identical path. */
    .wake_manifest = "/system/local/models/keyword/en-US/ALEXA/pryon.manifest",
    .earcon_dir = "/system/local/share/earcon/base/",   /* confirmed 2026-11-09, see the comment above the struct;
                                                            our own sounds in /data/local/hassmic/earcon/ still win
                                                            if sounds.c checks that first the same way on every model
                                                            — TODO: confirm sounds.c's own precedence order here */
    .earcon_zip = NULL,              /* not needed: see the comment above the struct */
    .thermal_type = "mtktscpu",      /* confirmed 2026-11-09 indirectly: the ramdisk's compiled sepolicy references
                                        "/driver/thermal/mtktscpu/mtktscpu_dtm_setting" as a type path; not yet
                                        `cat`-confirmed against /sys/class/thermal/thermal_zone*\/type on a live unit */
    .light_sensor = { NULL, NULL, NULL },  /* confirmed ABSENT 2026-11-09: no libacehal_ambientLightSensor.so, no
                                              "alsps" string, nothing light/lux-named anywhere in system/lib{,64} or
                                              system/vendor — unlike rook, this model may simply have no light sensor
                                              at all (plausible: cylindrical speaker, no screen to dim). Leave NULL
                                              unless a live unit's /sys turns one up under a name not grepped for. */
    .lux_dark = 0,
    /* Ambiguous: system/etc/audio_device.xml has a real "ext_headphone_output" mixer path (HP Driver Gain Volume 11,
     * Ignore Ramp Up On) and keylayouts include ACCDET.kl (MediaTek's jack-detect driver) — evidence a 3.5mm line-out
     * exists on this reference design, but TODO: that's MTK reference-design boilerplate and not proof this specific
     * product populated the jack on its board; find the actual switch node (rook/donut convention is
     * /sys/class/switch/h2w/state, not found by that name in this rootfs) on a live unit before trusting it exists. */
    .jack_state = NULL,
    .screen_round = 0,               /* headless (ro.build.configuration=headless): no screen at all, confirmed by the
                                        complete absence of any screen-related app/HAL in the unpacked rootfs */

    .clock_faces = NULL,             /* no screen: not applicable */
    .clock_faces_pkg = NULL,
    .backlight = NULL,               /* no screen: not applicable */

    .volume_steps = 100,
    /* Confirmed 2026-11-09: system/etc/led-resources/layer_config_common.json exists (5 priority layers,
     * volume_step-01.. animations among them) and system/bin has both ledcontroller and ledctrl — a real LED ring,
     * same mechanism as every other model's (not a band: no screen). ring_leds stays 0 (a full ring) since the exact
     * LED count isn't in the layer config itself (it names animations, not a pixel count) — TODO: confirm against a
     * live unit or by counting frames the way hassmic's own `-G` tooling would. */
    .ring_leds = 0,

    .volume_curve = NULL,            /* TODO: tools/vol-curve.py measurement, as the porting guide describes; NULL
                                        falls back to the Echo Dot 2's curve meanwhile */

    /* Confirmed 2026-11-09 from system/etc/audio_device.xml: the same Dot 2/Echo 2 codec family control name as
     * rook ("HP Driver Gain Volume"), but the stock value is 6 here, not rook's 9 (the active "ext_speaker_output"
     * path; an "ext_speaker_output_v2" path also exists, explicitly commented "Sonar V2 ... This is the old speaker
     * config. DO NOT EDIT" — evidence of at least two hardware revisions of this product with different speaker
     * tuning; confirm which one a given unit actually uses before trusting "6" universally). steps/db/min_db/limit
     * are still unmeasured (rook's own steps=2/db=1 pattern may not hold — TODO, same as rook's board.c says to do
     * with tinymix once there's a live unit). */
    .speaker_gain = "HP Driver Gain Volume",
    .speaker_gain_stock = 6,
    .speaker_gain_steps = 0,          /* TODO: unmeasured; 0 = "1 per dB" as a starting guess */
    .speaker_gain_db = 0,
    .speaker_gain_min_db = 0,
    .speaker_gain_limit = 0,
    .speaker_gain_alt = NULL,
    /* Confirmed present: audio_device.xml has "Ext_Speaker_Amp_Switch" (Off by default, On in the turnon path) —
     * the same kind of amplifier-enable switch as rook's "MFP Gpio Mute", though not necessarily identical semantics
     * (rook's starts muted and needs an explicit unmute pulse; this one's default-off/turnon-on pattern could mean
     * the HAL itself already switches it per-stream, in which case board.speaker_unmute might not be needed at all).
     * TODO: needs tracing on a live unit (does it stay off after boot with no app playing, the way rook's did?)
     * before deciding whether to set this. */
    .speaker_unmute = NULL,
};
