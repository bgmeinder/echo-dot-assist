# Echo Plus 1st gen (2017) (sonar), Fire OS 5.5.6.9 (LVY48F): same MT8163 generation as rook/crown, Android 5.1.1
# (API 22, src/hassmic/compat22.c), full Android framework (device.conf ANDROID=1).
#
# DRAFT — updated 2026-11-09 against the fully unpacked rootfs (firmware/sonar/rootfs/system/lib{,64}). This has
# still never actually been built or linked (no NDK/toolchain run against it yet, no unlocked unit to test on): what's
# below is either confirmed present in the rootfs (see each block's own comment) or still an unconfirmed guess copied
# from rook, marked TODO.
TARGET  := armv7a-linux-androideabi22    # same bionic generation as rook (ro.build.version.sdk=22)

# Confirmed 2026-11-09 against the unpacked rootfs: no libmixerAPI.so anywhere on the partition (grepped the whole
# tree), and lib/hw/audio.primary.mt8163.so + lib/libasp.so + lib/libaspclient.so are all present at rook's exact
# paths — audio really is AudioFlinger's here too, not AIPC's. libpryon.so, libwakewordserver_jni.so present.
# `probe.sh`/`mixcap`/`pryon_test` still need to actually run and agree once there's an unlocked unit to try them on.
AUDIO   := src/hassmic/audio_android.c
WAKE    := src/hassmic/wake_pryon.c
LIBS    := libpryon.so libopus.so libz.so

# No screen (ro.build.configuration=headless): the Echo Plus 1st gen has a physical LED ring, not a band. ring.c
# (board.c's ring_leds, currently 0 = full ring as a placeholder — see devices/sonar/board.c) rather than rook's
# ring_band.c, which only applies to models with a screen.
RING    := src/hassmic/ring.c

SYSLIBS := -lOpenSLES                     # same as rook, if audio_android.c is in fact the right backend (see above)

# Confirmed 2026-11-09: system/bin/toolbox here is a single plain binary too, same as rook (see device.conf's own
# TOYBOX comment for the detail); tools/mktoybox.sh's static toybox ships alongside regardless, cheap insurance.
TOYBOX  := 1

# Confirmed 2026-11-09 against the unpacked rootfs: the same Wi-Fi/BT chip as rook, Broadcom BCM43569A2 —
# system/etc/init.wifi.rc even says "Bcom Wi-Fi related services" in a comment; system/lib/modules/bcmdhd.ko is the
# kernel module (same name, same path as rook, under system/lib not system/vendor); system/vendor/firmware/brcm/ has
# bcm43569a2-firmware.bin + bcm43569a2.nvm + a BT patch (device.conf's bt_patch comment has the exact filename).
# WIFIDRV_URL/_COMMIT/_ARGS below are still rook's own values, unconfirmed for sonar's exact chip revision/firmware —
# the module name and general driver family (amazon-oss's bcmdhd fork) are a reasonable starting guess, not proven.
WIFIDRV        := bcmdhd
WIFIDRV_URL    := https://github.com/amazon-oss/android_kernel_amazon_amzn-bcmdhd.git
WIFIDRV_COMMIT := 937ab17580dd7fc19b6f76d14e05f70e8b2d0f91   # TODO: rook's pinned commit; sonar's own may differ
WIFIDRV_DIR    := bcmdhd
WIFIDRV_ARGS   := CONFIG_BCMDHD=m CONFIG_BCMDHD_USB=y   # TODO: rook's CONFIG_ROOK=y is rook-specific; sonar needs its
                                                          # own CONFIG_SONAR=y-equivalent board flag, if the driver's
                                                          # Kconfig for this board exists at all in that fork

# TODO: kernel version/arch/toolchain. rook's 3.18.19/arm64/aarch64-linux-android-4.9 is itself only known because
# rook's firmware was unpacked and probed; sonar's kernel version is not yet confirmed the same way (the preloader
# strings only say the SoC, MT8163, not the kernel version — and unlike rook's, this kernel has not been pulled/probed
# at all; a 2017 MT8163 Echo is plausibly the same 3.18.19 generation, but that is a guess, not a finding).
# KVER    := TODO
# KARCH   := TODO
# KSRC    := TODO
# KCROSS  := TODO
