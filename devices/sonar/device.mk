# Echo Plus 1st gen (2017) (sonar), Fire OS 5.5.6.9 (LVY48F): same MT8163 generation as rook/crown, Android 5.1.1
# (API 22, src/hassmic/compat22.c), full Android framework (device.conf ANDROID=1).
#
# DRAFT, from the firmware alone — this has never been built or linked against sonar's own stock libraries (we have
# only firmware/sonar/system/build.prop so far, not an unpacked rootfs with /system/lib in it). Everything below is
# either copied from rook as a starting guess (same SoC family) or left TODO; confirm each one against
# firmware/sonar/rootfs/system/lib once the system partition is unpacked (devices/README.md step 2).
TARGET  := armv7a-linux-androideabi22    # same bionic generation as rook (ro.build.version.sdk=22)

# TODO: AUDIO/WAKE backends. rook's reasoning ("the same SoC as crown, no Amazon mixer daemon: libasp runs inside
# the audio HAL, audio goes through AudioRecord/AudioTrack") is an MT8163-family argument that likely holds here too,
# and libpryon.so being the wake word engine is true of every Fire OS Echo so far. Neither is confirmed for sonar:
# `probe.sh`/`mixcap`/`pryon_test` need to actually agree once there is firmware/sonar/rootfs to link against
# (devices/README.md step 3: "If the model has Amazon's mixer and libpryon.so, the existing backends should work").
AUDIO   := src/hassmic/audio_android.c
WAKE    := src/hassmic/wake_pryon.c
LIBS    := libpryon.so libopus.so libz.so

# No screen (ro.build.configuration=headless): the Echo Plus 1st gen has a physical LED ring, not a band. ring.c
# (board.c's ring_leds, currently 0 = full ring as a placeholder — see devices/sonar/board.c) rather than rook's
# ring_band.c, which only applies to models with a screen.
RING    := src/hassmic/ring.c

SYSLIBS := -lOpenSLES                     # same as rook, if audio_android.c is in fact the right backend (see above)

# TODO: toybox. Every MT8163 Fire OS 5 build looked at so far (rook) is missing the same toolbox applets; assuming
# the same here is cheap (tools/mktoybox.sh costs nothing to ship) but unconfirmed for sonar specifically.
TOYBOX  := 1

# TODO: Wi-Fi/Bluetooth chip and driver. Not yet identified from the firmware pulled so far (nothing in
# firmware/sonar/{boot.img,images/*} names a Wi-Fi chip the way rook's init scripts or build.prop did once unpacked).
# Do not assume rook's Broadcom bcmdhd: that was specific to rook's hardware, not the SoC. Needs the unpacked rootfs's
# init.sonar.rc / wifi.ko module name, or lsmod on a rooted unit.
# WIFIDRV        := TODO
# WIFIDRV_URL    := TODO
# WIFIDRV_COMMIT := TODO
# WIFIDRV_DIR    := TODO
# WIFIDRV_ARGS   := TODO

# TODO: kernel version/arch/toolchain for the Wi-Fi-motion kmod (if the eventual Wi-Fi chip is one hassmic has a
# module for) and for any other out-of-tree building. rook's 3.18.19/arm64/aarch64-linux-android-4.9 is itself only
# known because rook's firmware was unpacked and probed; sonar's kernel version is not yet confirmed (the preloader
# strings alone don't say it — only the SoC, MT8163).
# KVER    := TODO
# KARCH   := TODO
# KSRC    := TODO
# KCROSS  := TODO
