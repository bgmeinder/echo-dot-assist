# Echo Plus 1st gen, 2017 (`sonar`)

**Not started** (2026-11-09): nothing has been unlocked, rooted or run yet. This directory is a draft scoped out from
the stock firmware alone — `device.conf`, `board.c` and `device.mk` here are placeholders with `TODO` on every value
that needs a rooted unit to check; do not install or build against them yet. Porting guide:
[devices/README.md](../README.md).

Model ZE39KL, MediaTek MT8163 — the same SoC as [`rook`](../rook/README.md) (Echo Spot 1st gen) and
[`crown`](../crown/README.md) (Echo Show 8 1st gen), a generation ahead of `checkers`/`cronos`. Headless
(`ro.build.configuration=headless`): no screen, a physical LED ring like the Dots. Fire OS 5.5.6.9, build 690918120
(`ro.build.display.id` LVY48F), Android 5.1.1 (API 22) — the same Fire OS/Android generation as rook, just a different
build number (`256.6.9.0_user_690918120` vs rook's `304.6.9.0_user_690918120`), so rook's Android-5.1-specific work
(`src/hassmic/compat22.c`, the static toybox, `hassmic.rc` in the boot ramdisk) is expected to carry over; confirm
each item rather than assuming.

## Firmware

Pinned (hand-fetched 2026-11-09, **not yet cross-checked against FTVDB**): `update-kindle-csm_sonar-256.6.9.0_user_690918120.bin`.
Put it into `firmware/sonar/` (not in git) before trusting anything derived from it. TODO: find and link this build's
FTVDB record page (`https://ftvdb.com/echo/firmware/com.amazon.sonar.android.os/`), confirm the sha256 there, and put
the confirmed value into `device.conf` `FIRMWARE_SHA256` (currently `TODO`).

Only `boot.img` and three images under `images/` (`lk.bin`, `preloader.img`, `tz.img`) plus `system/build.prop` have
been pulled out of the `.bin` so far — the system partition itself (`system.new.dat`/`system.img`) is not unpacked
yet, so `PROBE_FILES`, the stock service names, the Alexa package list and the daemon's groups are all still `TODO`
in `device.conf` (devices/README.md step 2: `tools/payload_dump.py`/`tools/sdat2img.py`).

> [!CAUTION]
> Same rule as every model here: never flash the stock `.bin` whole. Its updater script writes `lk`, the preloader and
> `tz` along with `boot`/`system`; only `boot` and `system` should ever be written once there is an unlock path.

## What the firmware says about the unlock (no exploit tried yet)

Read directly out of `firmware/sonar/images/{lk.bin,preloader.img}`, nothing further:

- **Platform: MT8163** (`platform/mt8163` in `lk.bin`; `preloader_sonar`, `MTK_BLOADER_INFO_v16` in `preloader.img`).
  Same chip generation rook was unlocked on (amonet-rook, a BootROM-level exploit). This is the reason a port looks
  plausible at all — it is **not** confirmation that the exploit works on this specific unit/BootROM revision; that
  is only answerable by actually putting a unit into BROM download mode and trying a handshake.
- **Fastboot unlock is Amazon-signed**, the same scheme as rook: `lk.bin` has `amzn_verify_limited_unlock`, an
  `unlock_code` tied to `mac_sec`/serial, and a `seccfg` partition (the lock-state partition amonet rewrites).
  `fastboot oem unlock` is expected to be refused here too, same as rook.
- **The stock fstab has `verify`** on `/system` (dm-verity, non-A/B, one system partition) — the same `INSTALL=boot`
  shape as rook: a patched boot image (verity stripped, root adb, `hassmic.rc` in the ramdisk) flashed once there is
  an unlock, then files onto `/system` from the running OS.
- **`ro.boot.selinux=disable`** is already in the stock cmdline/`build.prop` — unlike rook, which needed an enforcing
  v26 policy patched with Magisk's arm64 `magiskpolicy` under qemu. If that holds on a booted unit (confirm with
  `getenforce`; it can still be forced back on by `init`), `sepolicy.rules` may end up close to empty for this model.
- Anti-rollback markers (`antirback_lk`) are present in `lk.bin`, same as every modern MTK bootloader; not a blocker
  for an exploit-based unlock, just a reason to never flash an older whole `.bin` over a newer one.

**No public unlock project for `sonar`/Echo Plus 1st gen is linked here yet** (unlike rook's amonet-rook, crown's
amonet-crown, etc.) — this needs searching for one, or building an amonet-style config against this specific unit's
partition layout (`seccfg`/`boot` offsets from its GPT) if none exists. That is the gating step before anything else
in this directory can move from draft to checked.

## What's still open

Everything (devices/README.md's porting order, from the top):

1. Unlock and root — no known public project; MT8163 BootROM-exploit class expected, unconfirmed on this unit.
2. Unpack the system partition on the PC (`firmware/sonar/rootfs`).
3. Fill in every `TODO` in `device.conf`/`board.c`/`device.mk` against the unpacked firmware and, once rooted, a
   running unit (`getevent -il`, `find /sys -name "*privacy*"`, `ls -lZ`, `tinymix`/`amixer`, the stock service names
   in `init.sonar.rc`).
4. `probe.sh`/`deploy.sh` without installing.
5. Install + `setup.sh` (not written yet).
6. `tools/mkstubs.sh sonar`, add to CI's build matrix.
7. Update `devices/MODELS.md`, root `README.md`, `CHANGELOG.md`.
