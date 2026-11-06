/* Echo Spot 1st gen 2017 (rook), Fire OS 5.5.6.9 (LVY48F).  crown's SoC (MT8163) a generation earlier: Android 5.1,
 * kernel 3.18.19, Broadcom Wi-Fi and Bluetooth (CYW43569 on bcmdhd and a UART) instead of the MT7668, a round
 * 480 x 480 screen.  Probed over adb on a rooted Spot on 2026-11-04 (the firmware pulled from it: firmware/rook);
 * values marked "verify" are not checked against a running hassmic yet. */
#include "board.h"
#include <stddef.h>

/* No earcon folder in this firmware: stock's sounds are resources of its apps, stored uncompressed (sounds.c reads
 * them out of the APKs).  The same names as crown's, in other apps: the controls' in SystemUI (not KnightSystemUI),
 * Bluetooth's in KnightSettings, the calls' in the comms app (com.amazon.comms.rooktachyon beside it has the
 * reactions only), the timer's and the alarm's in the alarms app.  No wake sound or setup beacon found in any app
 * (verify: as crown, the built-in tones stand in). */
#define SYUI "/system/priv-app/SystemUI/SystemUI.apk"
#define KSET "/system/priv-app/KnightSettings/KnightSettings.apk"
#define COMM "/system/priv-app/com.amazon.comms.multimodaltachyonarm/com.amazon.comms.multimodaltachyonarm.apk"
#define ALRM "/system/priv-app/com.amazon.alarms/com.amazon.alarms.apk"
/* The wake sound: SpeechInteractionManager's resources are shrunk (res/BX.wav), their names only in resources.arsc:
 * system_state_active_start = res/BX.wav (48 kHz mono, 0.65 s, stored), the tone Alexa played when it started listening,
 * and system_state_user_speech_confirmed = res/AJ.wav, its end-of-speech partner (hassmic plays none there).
 * ban_ui_wakesound and ban_ui_endpointing are there too, but 0-byte placeholders (2026-11-05) */
#define SIM  "/system/priv-app/SpeechInteractionManager/SpeechInteractionManager.apk"
static const struct board_sound sounds[] = {
    { "ui_wakesound", SIM, "res/BX.wav" },
    { "ui_wakesound_touch", SIM, "res/BX.wav" },
    { "state_privacy_mode_on", SYUI, "res/raw/kni_controls_privacy_mode_on.mp3" },
    { "state_privacy_mode_off", SYUI, "res/raw/kni_controls_privacy_mode_off.mp3" },
    { "state_volume_adjust_tone", SYUI, "res/raw/kni_controls_volume_adjust.mp3" },
    { "state_bluetooth_connected", KSET, "res/raw/kni_system_bluetooth_bt_connected.mp3" },
    { "state_bluetooth_disconnected", KSET, "res/raw/kni_system_bluetooth_bt_disconnected.mp3" },
    { "comms_drop_in_incoming", COMM, "res/raw/ful_comms_drop_in_incoming.mp3" },
    { "comms_call_connected", COMM, "res/raw/ful_comms_call_connected.mp3" },
    { "comms_call_disconnected", COMM, "res/raw/ful_comms_call_disconnected.mp3" },
    { "comms_call_incoming_ringtone", COMM, "res/raw/ful_comms_call_incoming_ringtone.mp3" },
    { "comms_outbound_ringtone", COMM, "res/raw/ful_comms_outbound_ringtone.mp3" },
    { "system_alerts_melodic_02", ALRM, "res/raw/system_alerts_melodic_02.mp3" },
    { "system_alerts_melodic_01", ALRM, "res/raw/system_alerts_melodic_01.mp3" },
    { NULL, NULL, NULL },
};

const struct board board = {
    .model = "Echo Spot (rook)",
    .project = "hassmic.echo-spot",
    .product = "Echo Spot (hassmic)",
    .default_name = "Echo Spot",                                   /* + MAC: "Echo Spot 5695c4"; slug = the project's model */
    .codename = "rook",

    /* Three buttons on top: volume down, volume up, mute.  The volume keys come from gpio-keys, named "keys" here;
     * the mute button is KEY_POWER of MediaTek's matrix keypad "mtk-kpd" (getevent on a Spot, 2026-11-04: presses of
     * mute gave KEY_POWER on event1, volume down KEY_VOLUMEDOWN on event4; stock: a press is privacy mode, a hold the
     * shutdown dialog, 10010000.keypad's shutdown_dialog_state).  No action button: the volume keys together stand in */
    .keypad = "name:keys",
    .action_combo = 1,                                              /* the settings page's login, adb, Improv */
    .grab_keys = 1,                                                 /* else PhoneWindowManager turns its stream volume too */
    /* The mics' mute is the keypad driver's: /sys/devices/soc/10010000.keypad/privacy_state (0444) flipped at each
     * press of the mute button (the capture above: 1, 0, 1, 0 within the second), the mic GPIO by mediaserver
     * (mt_gpio, init.rook.rc).  privacy_trigger (0664 root:system, "Allows KnightKeyPolicyManager to trigger privacy
     * mode") went 1 at the first press and stayed.  Not a hardware latch as the Dots': software can clear it.  The
     * state is read once a second (privacy_latch) rather than the key taken from the framework: mtk-kpd is left
     * alone, so stock's key policy keeps flipping the state and the GPIO (verify: whether the kernel flips it by
     * itself with the key grabbed, and whether the mics are muted in the HAL without the framework's help) */
    .privacy_state = "/sys/devices/soc/10010000.keypad/privacy_state",
    .privacy_latch = 1,

    /* Broadcom BCM43569 on a UART (bt_vendor.conf: UartPort /dev/ttyMT1, bluetooth:radio 0660, hence radio in
     * DAEMON_GROUPS), not the Dots' /dev/stpbt: bt_uart.c loads its patch where libbt-vendor took it and switches to
     * 3 Mbaud, libbt-vendor's default target; Android's Bluetooth app is not on the image, the line is free.  Up with
     * 307 patch records, and again after hassmic restarts (the chip stays patched at 3 Mbaud, 2026-11-05) */
    .bt_dev = "uart:/dev/ttyMT1",
    .bt_patch = "/system/vendor/firmware/BCM43569A2_001.003.004.0167.0215.hcd",
    .bt_baud = 3000000,
    /* every codec: with the Spot's Wi-Fi on 5 GHz a Pixel's aptX HD (576 kbit/s) came whole and in time, 0 dry-outs a
     * minute; on 2.4 GHz plain aptX already skipped, its Wi-Fi taking the antenna (2026-11-06) */
    .a2dp_max_kbps = 0,
    /* a Pixel's aptX arrives whole but in bursts, gaps up to 140 ms (2026-11-06): 150 ms of buffer had no margin */
    .a2dp_buffer_ms = 300,
    .bt_service = NULL,
    .bt_mac = "/proc/idme/bt_mac_addr",

    .wake_id = "alexa",
    /* the older model layout (pryon.config beside the decoders, a playback model too); the same ModelSetManifest */
    .wake_manifest = "/system/local/models/keyword/en-US/ALEXA/pryon.manifest",
    .earcon_dir = "/data/local/hassmic/earcon/",                    /* none of stock's: own sounds here win over the apps' */
    .earcon_zip = sounds,
    .thermal_type = "mtktscpu",
    /* an ALS behind MediaTek's alsps framework (m_alsps_input), no libacehal_ambientLightSensor and no lux file under
     * /sys/bus/platform/drivers/als_ps on this kernel: none until one is found (verify) */
    .light_sensor = { NULL },
    .jack_state = "/sys/class/switch/h2w/state",                 /* 0 out, 1 headset, 2 headphones (2026-11-05) */
    .screen_round = 1,                                              /* the ring drawn around the rim of the round screen (ring_band.c) */
    .volume_steps = 100,                                            /* the light band (ring_band.c) shows the level in % */
    /* The Dot 2's codec family: the HAL's speaker path (audio_device.xml, "Right Channel Only" on) sets
     * "HP Driver Gain Volume" 9, the stock Spot reads 9 9 from tinymix (2026-11-04); verify the control is the
     * speaker's and that nothing writes it later */
    .speaker_gain = "HP Driver Gain Volume",
    .speaker_gain_stock = 9,
    /* The amplifier is muted from boot by the codec's MFP GPIO ("MFP Gpio Mute" On; dmesg's aic32x4_apply_mute
     * amp_mute=1 while a stream played, in silence, 2026-11-04).  Stock's boot sound tool (bootsound, the binary that
     * names Ext_Speaker_Amp_Switch and MFP Gpio Mute) switches it off once at boot and nothing else touches it; the
     * satellite leaves the boot sound out, so hassmic switches it off itself */
    .speaker_unmute = "MFP Gpio Mute",
};
