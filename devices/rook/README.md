# Echo Spot 1st gen, 2017 (`rook`)

**In progress** (2026-11-04): unlocked, rooted and installed on one Echo Spot; the satellite starts from init (wake
word, mic, the light band on the screen, the settings page, the firewall; "What works and what is open" below). Not
tried with Home Assistant yet. It is the Echo Show 8 1st gen's SoC (`crown`: MT8163) a generation earlier, on Fire OS 5
(Android 5.1.1, API 22, kernel 3.18): the build targets API 22 (`src/hassmic/compat22.c` stands in for what that
bionic lacks), a static toybox ships with it (Fire OS 5's toolbox has no `tr`, `sed`, `head`, `tail`, `pidof`...),
and Android 5.1's init reads no `/system/etc/init/`, so `hassmic.rc` goes into the boot image's ramdisk. Porting guide:
[devices/README.md](../README.md).

Model VN94DQ (the label; `ro.product.model` AEORK), MediaTek MT8163, round 2.5" touchscreen (480 x 480), 4 microphones
(`/proc/idme/miccal.0-3`), camera, volume keys, mute button (no action button). Broadcom CYW43569: Wi-Fi on `bcmdhd`,
Bluetooth on a UART (`/dev/ttyMT1`, not the Dots' and Shows' `/dev/stpbt`: hassmic loads the chip's patch itself,
`bt_uart.c`). Amazon's front end (`libasp`) runs inside the audio HAL, Pryon 2.41 with the stock models under
`/system/local/models/keyword`.

## Firmware

Pinned: `update-kindle-rook-304.6.9.0_user_690918120.bin`, Fire OS 5.5.6.9 (build 690918120, `ro.build.display.id`
LVY48F), from [FTVDB](https://ftvdb.com/echo/firmware/com.amazon.rook.android.os/), sha256 `616db16d…cb5aa` (full value in
`device.conf`). The system partition must be this build's: the boot image is made from its `boot.img` and hassmic links
against its libraries. Put the `.bin` into `firmware/rook/` (not in git).

> [!CAUTION]
> Never flash that `.bin` whole in TWRP: its updater script writes `lk`, `tee1` and the preloader, which undoes the unlock
> and can brick the Spot for good. Only `boot` and `system` from it are ever written (the install does that).

## Unlock (public: amonet-rook)

R0rt1z2's [XDA thread][xda] (`amonet-rook-v2.0.0.zip` from its first post). Works from Windows (`fastbrick.bat`,
2026-11-04) and Linux (`fastbrick.sh`). Files into `firmware/rook/`:

| File | Source | sha256 |
|---|---|---|
| `amonet-rook-v2.0.0.zip` | the XDA thread, post 1 | *verify* |
| `update-kindle-rook-304.6.9.0_user_690918120.bin` | Amazon, via FTVDB | `616db16d…cb5aa` |
| `boot-root.zip` (rook's) | the XDA thread, post 2; only its `sbin/adbd` is used (`ADBD` in `device.conf`) | `85bffd7a5907977ba9ec316337b0e3f90dbfc62d6d431642ef65af0b411877b8` |
| `magiskpolicy64` | `lib/arm64-v8a/libmagiskpolicy.so` of Magisk v31.1's `app-debug.apk` ([topjohnwu/Magisk](https://github.com/topjohnwu/Magisk/releases/tag/v31.1), apk sha256 `b983c975…0778`): donut's `magiskpolicy32` runs nowhere here (the Spot's linker wants DT_HASH, its TWRP is 64-bit) | `51fc92275f0c464c124a2d8e607e8b601e6ea4c44437f1e871cf175773fd75ce` |
| `twrp-bionic/` | `python3 tools/twrp_bionic.py amonet/bin/twrp.img firmware/rook/twrp-bionic`: TWRP's `linker64` and libraries, what `magiskpolicy64` runs against under `qemu-aarch64` | from the unlock zip |

1. With the Spot on its power supply, hold all three buttons until `=> FASTBOOT mode...` shows, connect micro-USB (the
   port is behind a plastic cover on the back), run `fastbrick.bat` / `./fastbrick.sh` from the unzipped `amonet/`, type
   `YES`. **After the 10 s grace period nothing may interrupt it** (up to 5 min): that bricks it for good. It ends in TWRP.
2. TWRP asks "Keep System Read only?": tap **Keep Read Only**.
3. From then on: **Volume Up** held while the power goes in = TWRP, **Volume Down** = hacked fastboot. The keys count only
   at a cold power-on (unplug, wait for the screen to go dark, hold, plug in).

**Never write lk, preloader, tee1/tee2 (tz) or any other bootloader partition.** An install for this model touches
`system` and `boot` only.

## Root

The XDA `boot-root.zip` on its own leaves adb off on this firmware: stock's `/system/etc/init.fosflags.sh` asks
`adb_cd.sh`, which takes a `release-keys` build for a closed device and strips `adb` from `persist.sys.usb.config` at
every boot (and `persist.` properties are stored in `/data/property/`, so one boot of the XDA image leaves adb off
under any image until `persist.sys.usb.config` is set to `mtp,adb` again, from TWRP). The boot image this project
builds (`scripts/mkbootroot.py` from the firmware's own `boot.img`: the XDA `adbd`, `ro.secure=0`,
`ro.build.tags=test-keys` so that script leaves adb alone, `hassmic.rc` in the ramdisk, the policy patched by
`sepolicy.rules`) has adb root over USB without authentication. By hand, from TWRP:

```sh
python3 scripts/mkbootroot.py --extract firmware/rook/images/boot.img sepolicy build/rook/sepolicy.stock
qemu-aarch64 -E LD_LIBRARY_PATH=/sbin -L firmware/rook/twrp-bionic firmware/rook/magiskpolicy64 \
    --load build/rook/sepolicy.stock --apply devices/rook/sepolicy.rules --save build/rook/sepolicy.hassmic
python3 scripts/mkbootroot.py firmware/rook/images/boot.img build/rook/boot-root.img --sepolicy build/rook/sepolicy.hassmic \
    --adbd firmware/rook/adbd --no-verity --prop ro.build.tags=test-keys --add hassmic.rc=devices/rook/hassmic.rc --import hassmic.rc
adb push build/rook/boot-root.img /tmp/ && adb shell dd if=/tmp/boot-root.img of=/dev/block/platform/mtk-msdc.0/by-name/boot
```

(`scripts/install-boot.sh --boot-only` does all of it.) The stock fstab has `verify`: once `/system` has been written, only a
boot image without it boots (the XDA image has it out too).

(`fastboot flash boot` in the hacked fastboot does the same.) With the XDA image instead, adb needs
`/data/property/persist.sys.usb.config` set to `mtp,adb` from TWRP as well, and comes back off after the next update.

## Install

`INSTALL=boot` as crown's: `DEVICE=rook scripts/install-boot.sh` (what `scripts/install-system.sh` hands over to) builds
the boot image (part 1: `boot_image`, with `magiskpolicy64` under `qemu-aarch64`), flashes it in the hacked fastboot
and, booted with root adb, puts the files onto `/system` (part 2) and writes `hassmic.conf`. Installed that way on one
Spot (2026-11-04; the two parts run separately, below). By hand:

1. **Unpack the firmware** (see below), build: `make kernel-tools DEVICE=rook` once (Amazon's 3.18 kernel tree, GCC 4.9
   and the Wi-Fi driver's source, about 1 GB, into `toolchain/`), then `make DEVICE=rook` (`tools/mktoybox.sh` runs as
   part of it; without the kernel tools the Wi-Fi driver is left out and Amazon's stays)
2. **System partition**: must be build 690918120's (`adb shell getprop ro.build.version.incremental` =
   `304.6.9.0_user_690918120`). Else write `firmware/rook/images/system.img` (sha256 `f14d0493…da67`, 1744723968 bytes)
   to `/dev/block/platform/mtk-msdc.0/by-name/system` from TWRP, and read it back.
3. **Root boot image**: `scripts/install-boot.sh --boot-only` ("Root" above has the commands it runs).
4. **Lock down, join Wi-Fi, install**: `scripts/wifi-join.sh` after the lockdown (`scripts/device/lockdown.sh`; do not
   join before it: Fire OS would reach Amazon), then `DEVICE=rook scripts/install-boot.sh`.

On a PC without the NDK and qemu (Windows: Git Bash with adb on PATH and `MSYS_NO_PATHCONV=1`), the build and the boot
image come from the build container (`tools/devenv/devenv.sh`, `fetch rook`), the boot image is written from the
running OS (`dd`, "Root" above) and the files go on with `DEVICE=rook sh scripts/install-boot.sh --files-only`.

Updating later: `scripts/ota-push.sh <echo-ip>` (`DEVICE=rook`). `scripts/install-boot.sh --uninstall` turns Alexa's apps
back on and takes hassmic off `/system`; all the way back is `boot` and `system` from the firmware `.bin` (never the whole
zip: see the caution above), or amonet's `restore-stock-rook`.

Trial runs without installing, on a rooted Spot: `scripts/deploy.sh`, then `adb shell sh /data/local/hassmic/run.sh`.

## What works and what is open

Installed and started from init on one Spot (2026-11-04): Pryon loads the stock ALEXA model and detects, OpenSL ES
capture at 16 kHz, the light ring drawn around the rim of the round screen (`ringbar.dex`'s ring mode, 2026-11-05: a band
along the bottom was a sliver under the bezel; `board.screen_round`),
hassmic's home screen in front (its APK with `minSdkVersion` 22 now), `wifictl.dex` with Android 5.1's
`setWifiEnabled(boolean)`, the settings page, ESPHome on 26053, the firewall (rule by rule: no `iptables-restore` on
Fire OS 5), Alexa's apps off, self test passed.
The clock: hassmic's digital clock or one of the firmware's analog faces (setting clock_face, read from the clock-face
package Alexa's clock used, now under /system/hassmic-removed; 2026-11-06).
The speaker: muted from boot by the codec's "MFP Gpio Mute", which only stock's boot sound tool switched off
(`board.speaker_unmute`, spkgain.c switches it off and keeps it so); Android 5.1's audio service transaction numbers
(4 sets the music stream, 15 reads its maximum) probed on the Spot and in audio_android.c.
Home Assistant (2026-11-04): adopted as an ESPHome device (by IP; HA's first connection dropped once and recovered),
wake word, pipeline, transcript and reply all run; HA's "Wake word" select had defaulted to "no wake word", set to
Alexa.  The reply's audio failed at first (`media: ... Peer certificate cannot be authenticated with given CA
certificates`): not a missing root (the store has ISRG Root X1) but the firmware's libcurl 7.50.1 built with a Linux CA
file as its default, `/etc/ssl/certs/ca-certificates.crt`, which the Echo has not. hassmic now names the store
(`CAPATH`) and its own `cacerts.pem` (Let's Encrypt's roots, `CAINFO`) on every model (https.c); the check stays on.
The speaker, settled on 2026-11-05 after a day on the wrong trail: the media server init starts at boot never carries
hassmic's playback to the primary output (`dumpsys media.audio_flinger` while hassmic "speaks": the output thread in
standby, 0 tracks, 0 writes, whatever the kernel, codec registers, amplifier GPIO and mixer say, and they were all
checked), one started afresh does, with the mic still working.  `main.sh` restarts it at satellite start, before
hassmic starts (device.conf `MEDIA_RESTART`); from a cold boot HA's announcements then play, on the firmware's own
kernel (heard twice, the track and writes in dumpsys).  Why the boot-time instance behaves so is not known: the only
state that differs is what Amazon's speech service attaches to the output at boot.  Traps met on the way, for the
next model: mediaserver keeps `/dev/snd/pcmC0D23p` open once anything played through it, so a `tinyplay` after that
blocks in `snd_pcm_open` and passes for silence (kill mediaserver first); the codec driver's `aic32x4_apply_mute`
at every stream start re-applies the amplifier's MFP mute state (hassmic's unmute pulse at open lands before it);
HA's volume for a new Echo is 20 % (stream index 6 of 30), quiet; and a kernel comparison that uses the boot-time
media server compares nothing (R0rt1z2's XDA kernel was blamed for an afternoon; the two kernels are the same source
`g004652e`).  At 100 % the codec is at stock's own speaker settings (`audio_device.xml`: `PCM Playback Volume` 127,
`HP Driver Gain Volume` 9); the amplifier's `Ext_Amp_Gain` (6 dB, up to 24) is the headroom above stock.
**Open**: whether the 3.5 mm jack carries audio is untested.  Once (the first start after an install) hassmic was found stopped
(every thread in `do_signal_stop`, no tombstone) 0.4 s into its start, and faulted when continued by hand after a
`debuggerd -b` on it; the watchdog's restart ran fine: watch the next boots; the mute
button (`KEY_POWER` of `mtk-kpd` flips `/sys/devices/soc/10010000.keypad/privacy_state`, which hassmic reads once a
second; whether the mics are muted in the HAL without stock's apps: verify); Android's `settings` table has no screen
rows on Fire OS 5 (Amazon keeps them elsewhere), so `ANDROID_SETTINGS` and the band's screen timeout may not reach the
screen; `service call` on Android 5.1 has no
`i64`, so the music stream's maximum is not read (`audio: no maximum for the music stream`); no attestation HAL
(`libacehal_dha.so`): Amazon downloads need the binder route; the light sensor (MediaTek's
alsps, no lux file found).

## Files here

| File | State |
|---|---|
| `device.mk` | API 22, OpenSL ES, Pryon, light band, `TOYBOX := 1`; the Wi-Fi driver (`WIFIDRV`: Broadcom's bcmdhd 2025 against Amazon's 3.18.19 tree); no Wi-Fi motion module |
| `bcmdhd.patch`, `kconfig` | our changes to the driver (power save, the Spot's MAC, reproducible) and the Spot's kernel config on `douglas_defconfig` |
| `board.c` | probed on the Spot: keys, mute state file, sounds in SystemUI / KnightSettings / comms / alarms, speaker gain "HP Driver Gain Volume" 9 |
| `device.conf` | identity, the pinned firmware and its checksums, `INSTALL=boot`, `ANDROID=1`, `TOYBOX=1`, Alexa's packages (crown's list less what rook lacks) |
| `hassmic.rc` | crown's services, for the ramdisk (`mkbootroot.py --add/--import`) |
| `sepolicy.rules` | crown's, not applied yet |
| `probe.md5`, `stubs/` | from the pulled firmware (`tools/mkstubs.sh rook`: byte-identical) |
| `setup.sh` | crown's copy, not adapted: the guided setup does not support rook yet |

## Unpack the firmware

```sh
unzip firmware/rook/update-kindle-rook-*.bin -d firmware/rook/ boot.img system.transfer.list system.new.dat
mkdir -p firmware/rook/images firmware/rook/rootfs/system
mv firmware/rook/boot.img firmware/rook/images/
python3 tools/sdat2img.py firmware/rook/system.transfer.list firmware/rook/system.new.dat firmware/rook/images/system.img
debugfs -R "rdump / firmware/rook/rootfs/system" firmware/rook/images/system.img   # ownership errors are harmless
python3 scripts/mkbootroot.py --cpio firmware/rook/images/boot.img firmware/rook/ramdisk.cpio
(cd firmware/rook/rootfs && cpio -id < ../ramdisk.cpio)
```

[xda]: https://xdaforums.com/t/unlock-root-twrp-unbrick-amazon-echo-spot-2017-rook.4754878/
