#!/bin/sh
# A static toybox for an Echo whose own toolbox lacks what the device scripts use (rook: Fire OS 5's has no tr, sed,
# head, tail, cut, wc, sort, od, stat, pidof, pkill, timeout, find, xargs, printf, env, md5sum...).  device.mk sets
# TOYBOX := 1 and the Makefile builds $(OUT)/toybox with this; boot.sh and run.sh put its applets first on PATH.
#   tools/mktoybox.sh OUT            -> OUT (stripped, static, armv7)
# Pinned source, fetched once into toolchain/ (git-ignored).  Built with the NDK's API 28 compiler: toybox 0.8.12 wants
# getentropy and getgrgid_r, which bionic has from 28/24, and a static binary carries its own libc, so it runs on
# Android 5.1 all the same (tried on the Spot, 2026-11-04: 69 applets).  Only the applets the scripts need: su,
# passwd and w, which want crypt and utmp, are not among them anyway.
set -e
cd "$(dirname "$0")/.."
OUT=${1:?usage: tools/mktoybox.sh OUT}
VERSION=0.8.12
SHA256=3c529d93923dde67d048e7bcbd5d1bc0dd1ad09362269e2415f5f2eaab349b5b
URL=https://github.com/landley/toybox/archive/refs/tags/$VERSION.tar.gz
NDK=${NDK:-$PWD/toolchain/android-ndk-r21e}
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi28-clang
STRIP=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip
APPLETS="TOYBOX TOYBOX_FLOAT TOYBOX_HELP TOYBOX_HELP_DASHDASH TR PKILL PGREP PIDOF HEAD TAIL SED CUT WC SORT SORT_FLOAT OD
         STAT TIMEOUT FIND XARGS PRINTF ENV MD5SUM SHA256SUM SHA1SUM COMM BASENAME DIRNAME MKTEMP TEE UNAME REALPATH
         READLINK HWCLOCK GREP EGREP FGREP SEQ CMP PASTE EXPR TRUE FALSE YES UNIQ REV TAC STRINGS HEXDUMP XXD BASE64 NL
         NPROC SLEEP USLEEP INSTALL CP MV RM MKDIR LN LS CAT CHMOD CHOWN TOUCH DD KILL KILLALL PS SYNC DU DF WHICH ID DATE"

SRC=toolchain/toybox-$VERSION
if [ ! -f $SRC/Makefile ]; then
    mkdir -p toolchain
    curl -fsSL -o toolchain/toybox-$VERSION.tar.gz "$URL"
    echo "$SHA256  toolchain/toybox-$VERSION.tar.gz" | sha256sum -c - > /dev/null
    rm -rf $SRC; mkdir -p $SRC
    tar -xzf toolchain/toybox-$VERSION.tar.gz -C $SRC --strip-components=1
fi
cd $SRC
make clean > /dev/null 2>&1 || true
make allnoconfig > /dev/null 2>&1
for c in $APPLETS; do sed -i -E "s/^# CONFIG_$c is not set/CONFIG_$c=y/" .config; done
# __ANDROID_API_U__: a name of newer NDKs' headers that toybox's portability.c asks for; r21e has none
make -j"$(nproc)" CC="$CC" CFLAGS="-D__ANDROID_API_U__=34" LDFLAGS="-static" > /dev/null
cd - > /dev/null
mkdir -p "$(dirname "$OUT")"
"$STRIP" -o "$OUT" $SRC/toybox
echo "$OUT: $(stat -c %s "$OUT") bytes, $(grep -c '^USE_' $SRC/generated/newtoys.h 2>/dev/null || echo ?) applets"
