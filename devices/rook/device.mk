# Echo Spot 1st gen 2017 (rook), Fire OS 5.5.6.9 (LVY48F): armv7, Android 5.1.1 bionic (API 22: the NDK's oldest
# bionic this code builds for; getifaddrs and open_memstream came with API 24/23, src/hassmic/compat22.c stands in),
# full Android framework.  The same SoC as the Echo Show 8 1st gen (crown: MT8163) and no Amazon mixer daemon: the
# front end (libasp) runs inside the audio HAL (audio.primary.mt8163.so links it), so audio goes through Android's
# AudioRecord/AudioTrack via OpenSL ES (audio_android.c).  libpryon.so is the older decoder API (PryonDecoder_*, as
# the Dots'); every symbol hassmic needs is in it (checked against the pulled firmware, 2026-11-04).
TARGET  := armv7a-linux-androideabi22
# Backends (src/hassmic/audio.h, wake.h) and the stock libraries they link against, from $(STOCK).
AUDIO   := src/hassmic/audio_android.c
WAKE    := src/hassmic/wake_pryon.c
LIBS    := libpryon.so libopus.so libz.so
# No LED ring: the ring as a band along the bottom of the round 480 x 480 screen, drawn by scripts/device/ringbar.dex
# (ring.h; mkringbar.py --framework=firmware/rook/rootfs: every reference found on Android 5.1)
RING    := src/hassmic/ring_band.c
# NDK system libraries the backend needs (not stock: no stubs for them)
SYSLIBS := -lOpenSLES
# Fire OS 5's toolbox has no tr, sed, head, tail, pidof, pkill, timeout...: a static toybox ships with the build
# (tools/mktoybox.sh), first on the scripts' PATH (boot.sh, run.sh; device.conf TOYBOX=1 ships it)
TOYBOX  := 1

# Wi-Fi motion: none yet.  The Wi-Fi driver is Broadcom's bcmdhd (CYW43569), not MediaTek's: neither kernel module
# of src/kmod/ applies, and the kernel is 3.18.19 (arm64).
#
# The Wi-Fi driver itself, of our own build (Makefile "Wi-Fi driver"): Broadcom's bcmdhd 101.10.591.95.46 (2025-02) as
# amazon-oss ported it to rook, for Amazon's 1.363.110.17 (2017), which refuses Wi-Fi power save (LAB126_DISABLE_PS):
# the chip's Wi-Fi then kept the antenna its Bluetooth shares and a phone's music skipped (2026-11-05/06).  Built
# against Amazon's own 3.18.19 tree for MT8163 (Fire HD 8 2017's GPL release, the Spot's is not public) with the Spot's
# config as a fragment on douglas_defconfig (devices/rook/kconfig: struct offsets checked against the Spot's kernel by
# tools/kabi_compare.py), by the GCC 4.9 donut's module uses; our changes in devices/rook/bcmdhd.patch
WIFIDRV        := bcmdhd
WIFIDRV_URL    := https://github.com/amazon-oss/android_kernel_amazon_amzn-bcmdhd.git
WIFIDRV_COMMIT := 937ab17580dd7fc19b6f76d14e05f70e8b2d0f91
WIFIDRV_DIR    := bcmdhd
WIFIDRV_ARGS   := CONFIG_BCMDHD=m CONFIG_BCMDHD_USB=y CONFIG_ROOK=y
KVER    := 3.18.19
KARCH   := arm64
KSRC    := $(CURDIR)/toolchain/linux-3.18.19-amazon-mt8163
KCROSS  := $(CURDIR)/toolchain/aarch64-linux-android-4.9/bin/aarch64-linux-android-
# a git repository and a commit (kernel-tools.sh fetches that commit alone): hexdump0815's import of the tarball
KSRC_URL    := https://github.com/hexdump0815/linux-amazon-mediatek-mt8163-kernel-source.git
KSRC_SHA256 := 617fb5441f05151d2ffc89bd1d9d595b217691df
KCC_URL     := https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/+archive/961622e926a1b21382dba4dd9fe0e5fb3ee5ab7c.tar.gz
KCC_DIGEST  := 6465e57d85ac645a5ebe5fe0537dc3f868f85ea6bbd8d7e1d0de06b349168067
KDEFCONFIG  := douglas_defconfig
KFRAG       := devices/rook/kconfig
