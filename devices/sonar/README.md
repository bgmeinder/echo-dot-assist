# Echo Plus 1st gen, 2017 (`sonar`)

**Not started on a real unit** (2026-11-09): nothing has been unlocked, rooted or run. The firmware itself, though, is
now fully unpacked and read on the PC (`firmware/sonar/rootfs`, `rootfs_ramdisk`) — most of `device.conf`/`board.c`/
`device.mk` is filled in from that alone, each value sourced in its own comment; what's left genuinely needs a rooted
unit (exact input event mapping, sysfs paths, `tinymix` values) and stays marked `TODO`. Do not install or build
against this directory yet. Porting guide: [devices/README.md](../README.md).

Model ZE39KL, MediaTek MT8163 — the same SoC as [`rook`](../rook/README.md) (Echo Spot 1st gen) and
[`crown`](../crown/README.md) (Echo Show 8 1st gen), a generation ahead of `checkers`/`cronos`. Headless
(`ro.build.configuration=headless`): no screen, a physical LED ring like the Dots (`led-resources`/`ledcontroller`
confirmed in the firmware). Fire OS 5.5.6.9, build 690918120 (`ro.build.display.id` LVY48F), Android 5.1.1 (API 22) —
the same Fire OS/Android generation as rook, just a different build number (`256.6.9.0_user_690918120` vs rook's
`304.6.9.0_user_690918120`) and a different, older app-naming scheme ("csm_sonar", not rook's "Knight"-era names: the
package lists in `device.conf` do not overlap much with rook's). rook's Android-5.1-specific work
(`src/hassmic/compat22.c`, the static toybox, `hassmic.rc` in the boot ramdisk) carries straight over; confirmed by
the unpacked firmware in most cases, see each file's own comments for what's actually checked vs. still assumed.

Same Broadcom Wi-Fi/Bluetooth part as rook, too: BCM43569A2 on a UART (`/dev/ttyMT1`), confirmed from
`system/etc/bluetooth/bt_vendor.conf` and the kernel module/firmware filenames under `system/lib/modules/bcmdhd.ko`
and `system/vendor/firmware/brcm/` — a different BT patch revision (`…0167.0216.hcd` vs rook's `…0167.0215.hcd`), same
chip. The speaker amp control is the same family too (`HP Driver Gain Volume`), stock value 6 here vs rook's 9 — and
`system/etc/audio_device.xml` has a comment calling out a second, older "Sonar V2" speaker path explicitly marked
"DO NOT EDIT", suggesting at least two hardware revisions of this product exist with different speaker tuning.

## Firmware

Pinned (hand-fetched 2026-11-09, **not yet cross-checked against FTVDB**): `update-kindle-csm_sonar-256.6.9.0_user_690918120.bin`,
sha256 `a3b828bd…42e93f` (full value in `device.conf` `FIRMWARE_SHA256`). Put it into `firmware/sonar/` (not in git)
before trusting anything derived from it. TODO: find and link this build's FTVDB record page
(`https://ftvdb.com/echo/firmware/com.amazon.sonar.android.os/`) and confirm the sha256 there — this one is only
self-consistent (the system partition built from it matches what's analysed below), not yet cross-checked externally.

Fully unpacked now (2026-11-09, via the remote devenv container for `debugfs`/`cpio`, since this was done from
Windows): `firmware/sonar/images/system.img` (`tools/sdat2img.py` from the OTA's `system.new.dat`; `system.patch.dat`
is empty, so there were no bsdiff patches to apply, only new-data blocks), sha256 `63090a03…3c20a0`, 1073676288 bytes,
`debugfs -R rdump`'d into `firmware/sonar/rootfs/system`; the boot ramdisk cpio (`scripts/mkbootroot.py --cpio`) into
`firmware/sonar/rootfs_ramdisk`. `PROBE_FILES`, the stock service names, the Alexa package list and `CLOUD_SERVICES`
are all filled in `device.conf` now, sourced against this unpacked tree (see that file's own comments for exactly
what was checked and how).

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
- **`ro.boot.selinux=disable` in `build.prop` is probably not telling the truth about runtime SELinux**, on closer
  look (2026-11-09): `ro.boot.*` properties are normally generated at boot from `androidboot.*` kernel cmdline
  parameters, not baked statically into `build.prop` — but that's exactly where it sits here, in both
  `system/build.prop` and the OTA's own `target.build.prop`, suggesting a copied template value rather than a real
  reading. The boot image's own static cmdline (`bootopt=64S3,32N2,64N2 firmware_class.path=...`) carries no
  `androidboot.selinux` at all; however `images/lk.bin` (the bootloader) has the literal string
  `"%s androidboot.selinux=permissive"`, meaning **`lk` appends this parameter to the cmdline itself at boot time,
  conditionally** (format string, so some condition picks "permissive" or nothing) — not what `build.prop` claims
  ("disable"), and not confirmable without booting a unit and reading `/proc/cmdline` or running `getenforce`.
  **Do not assume SELinux is off here.** Treat this the same as rook until proven otherwise: an enforcing v26 policy
  that needs `sepolicy.rules` patched with Magisk's arm64 `magiskpolicy` under qemu (same reasoning as rook: this
  device's 5.1 linker almost certainly also lacks `DT_HASH`, ruling out donut's 32-bit `magiskpolicy32` the same way).
- Anti-rollback markers (`antirback_lk`) are present in `lk.bin`, same as every modern MTK bootloader; not a blocker
  for an exploit-based unlock, just a reason to never flash an older whole `.bin` over a newer one.

**No public unlock project for `sonar`/Echo Plus 1st gen is linked here yet** (unlike rook's amonet-rook, crown's
amonet-crown, etc.) — this needs searching for one, or building an amonet-style config against this specific unit's
partition layout (`seccfg`/`boot` offsets from its GPT) if none exists. That is the gating step before anything else
in this directory can move from draft to checked.

## USB access (likely needed before anything else)

rook and crown both have a screen and an exposed micro-USB port (behind a plastic cover), so amonet's BootROM exploit
on those runs by holding a button combo and plugging straight into it — no disassembly. `sonar` is shaped like
`donut` instead: a cylindrical speaker, headless, no screen, and (same as every Dot-shaped Echo so far) no exposed
USB socket on the outside.

`donut`'s own README documents exactly this situation for the Echo Dot 3: *"The Echo Dot 3 has no USB socket; its USB
data lines end on test pads on the main board. The case has to be opened and a USB cable soldered to those pads"*
(`TM3`/`TM4`/`TM5` = GND/D−/D+, VCC left unconnected — the Echo runs off its own power adapter, never USB power).
`sonar` is expected to need the same kind of access, **but not necessarily the same pads**: it is a different main
board (2017 Echo Plus 1st gen vs. 2018 Echo Dot 3), almost certainly its own silkscreen numbering and physical
layout, even though both are MT8163 (rook, crown) / MT8516 (donut) family parts with a BROM-level USB download mode.
Donut's `TM3`/`TM4`/`TM5` numbers should **not** be assumed to carry over or even to exist on sonar's board.

Open, in order:

1. **Look for a public teardown or unlock project first.** The fast path here — the way donut's `kamakiri-donut` and
   rook's `amonet-rook` XDA threads already did the pad-finding and exploit work — is finding out whether someone
   has already mapped the Echo Plus 1st gen's board (XDA, a teardown blog, an existing amonet config for `sonar`).
   Not confirmed to exist yet; worth a dedicated search before opening a unit.
2. **If nothing exists, open the case and find the pads by hand**: same process as whoever reverse-engineered
   donut's — open the case (teardown guide if one turns up), trace the SoC's USB D+/D− lines out (continuity from a
   known reference point, e.g. a component with a public pinout, back to candidate test pads), confirm GND.
3. **Confirm the board matches `FIRMWARE_BUILD`** before soldering anything irreversible: a board revision mismatch
   (the Plus 1st gen shipped across a few hardware years) could mean the pad layout, or even the SoC generation,
   differs from what the firmware alone suggests.
4. Only once wired: the BROM-entry mechanism itself (hold the right key while plugging in the power adapter, as
   donut's and rook's docs describe) is expected to be the same MT8163/MT8516-family behaviour documented for every
   other model here — that part is not the open question, the pads are.

## What's still open

devices/README.md's porting order, from the top:

1. **Unlock and root** — no known public project; USB access needs opening the case and finding test pads (see "USB
   access" above, unlike rook/crown's exposed port); MT8163 BootROM-exploit class expected, unconfirmed on this unit.
   Not started at all; everything below this line is firmware-only analysis, none of it run.
2. ~~Unpack the system partition on the PC~~ — done 2026-11-09 (`firmware/sonar/rootfs`, `rootfs_ramdisk`).
3. **Fill in `device.conf`/`board.c`/`device.mk` TODOs** — well along from the unpacked firmware alone (PROBE_FILES,
   Wi-Fi/BT chip and patch file, wake word manifest, earcon folder, LED ring, speaker gain control, thermal zone,
   package lists, partition device paths, adbd, toybox, the SELinux correction above). Still open, and genuinely
   needing a rooted unit rather than more firmware reading: the exact keypad/mute input event mapping (`getevent -il`),
   confirming `/sys/devices/platform/gpio-privacy/state` actually exists and behaves like a latch, whether a light
   sensor exists under some path not grepped for, whether the headphone jack is physically populated, `tinymix`
   values and gain steps, `ps -Z`/`ls -lZ` for the daemon's exact groups, and live confirmation of the SELinux state
   (enforcing vs. permissive vs. actually disabled — see the correction above, do not trust `build.prop` alone).
4. `probe.sh`/`deploy.sh` without installing — blocked on step 1 (needs a rooted, adb-reachable unit).
5. Install + `setup.sh` (not written yet).
6. `tools/mkstubs.sh sonar`, add to CI's build matrix.
7. Update `devices/MODELS.md`, root `README.md`, `CHANGELOG.md`.
