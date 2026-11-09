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

## USB access — good news: this one may have a real connector, not test pads

Originally assumed (2026-11-09, earlier in this file's history) to need donut-style pad soldering, since sonar looks
like a cylindrical, headless Dot rather than rook/crown's screened devices with an exposed port. **That assumption
was wrong**, corrected the same day by going straight to the source instead of guessing from the case shape: Amazon's
own FCC filing for this device.

FCC ID **2AHUF-6294** is this exact model (confirmed: registered grantee Amazon.com Services LLC, product code 6294,
its own "User Manual" exhibit matches model ZE39KL) — [fccid.io/2AHUF-6294](https://fccid.io/2AHUF-6294). Its
**Internal Photos exhibit** (`EUT Photo_Internal`, a public, unconfidential document — FCC requires board photos be
released even when schematics are held confidential long-term, which they are here) is a genuine teardown: 8 pages of
board photography with a ruler for scale, taken by the test lab (SPORTON International) for the original 2017
certification. Downloaded and rendered to check, 2026-11-09:

**The stack is five boards**, all visible across the 8 photo pages (2026-11-09), which line up with the FCC filing's
three named schematic exhibits ("Main Logic Board_Schematics", "Combo Board_Schematics", "Mic Board_Schematics") plus
two sub-assemblies that don't get their own schematic:

| Board (as found in the photos) | What's on it | Connectors |
|---|---|---|
| **Main Logic Board** ("JAWFISH" silkscreened on its debug header — apparently this board's own internal name; no public hits for that term otherwise) | the SoC and two more BGA packages, an unshielded Wi-Fi/BT module footprint, antenna pads | `J1100` **"JAWFISH DEBUG"** header, `J1101` **"JTAG 2L"** header, `J22` ribbon to the Mic Board (labelled "MICROPHONE" — a different connector from the USB one below, same designator letter by coincidence of per-board numbering), `J21` ribbon labelled "AMPLIFIER" to the Combo Board, `P2` Wi-Fi/BT antenna pad |
| **Combo Board** (power + amplifier) | big 820 µF electrolytic caps, an amplifier IC (`U18`), a small pouch cell/supercap beside it on the bench | **a populated micro-USB-B receptacle, silkscreened `J22`** (this board's own numbering — not the Main Logic Board's `J22` ribbon above), an 8-pin connector `J1102` (→ Main Logic Board's `J21`), an 8-pin header `P1` |
| **Mic Board** (silkscreened `PCB: 30-002335 REV 01`) | a small MCU/codec, mic array routing | a huge array of test points (`TM2`...`TM50`, `TP1`...`TP105`), a jumper `SHRT19`, a button `S2`, ribbon `J21` (→ Main Logic Board's `J22`) |
| **Volume ring / LED ring board** | a visible rotary encoder (a knurled plastic wheel directly on the board — this model has a physical rotating volume control, not just buttons), LED positions around the rim, a small MCU | ribbon `J11`, header `P1` |
| Wi-Fi/BT combo module | the actual radio chip, under a can on the Main Logic Board | n/a (soldered down) |

**The micro-USB connector (`J22`, on the Combo Board — the power/amplifier board, not the one with the SoC) is a
real, soldered-on micro-USB-B receptacle**, not a bare pad: 5 contacts, standard micro-B shell, visible plainly in the
photos next to the bulk capacitors. This is the same kind of connector rook/crown already have exposed through their
case — sonar's is just inside the housing rather than behind an external flap. Whether its D+/D− lines actually reach
the SoC's USB OTG pins (needed for the BootROM download mode the way rook's does) isn't confirmed by a photo alone —
but a populated micro-USB on a board in this stack, at all, is a much better starting point than bare pads.

**A second, possibly better lead**: the Main Logic Board itself — the one with the SoC — has its own **`J1101`
"JTAG 2L" header and `J1100` "JAWFISH DEBUG" header**, soldered connectors (not bare pads either, going by the
silkscreen style matching other populated connectors in the same photo). A populated JTAG header is a direct, well
understood hardware-debug path independent of USB entirely (a JTAG/SWD probe compatible with this SoC, rather than
amonet's USB-based BootROM exploit) — worth investigating alongside, or instead of, the USB route, once a unit is
open enough to see what these headers actually are (header pitch, pin count, whether they're populated with pins or
just pads).

**Cross-checked against [`radar`](../radar/README.md) (Echo 2nd gen), already supported in this repo**: radar's own
README says plainly "no USB socket", with data lines soldered to pads (`TP13`/`TP14`/`TP15`) on its "amplifier/
tweeter board" — on the face of it, a contradiction, since sonar's Combo Board looks like the same kind of board
(power + amplifier) and does have a populated connector. It isn't actually a contradiction once the generations are
compared: radar is Fire OS 6572 / **Android 7.1.2** (`radar_puffin`, donut-style A/B partitioning) — a full
generation newer than sonar's Fire OS 5.5.6.9 / **Android 5.1.1** (confirmed from the unpacked firmware, same
generation as rook, not radar), despite both being announced the same day in 2017 and sharing a similar cylindrical
shape. Different board design, different manufacturing choices — radar's amp board not getting a populated USB
connector doesn't say anything about whether sonar's does, and the FCC photos are direct, model-specific evidence
for sonar that radar's README (accurate for radar) can't override.

**The exploit tool itself may also be more generic than per-device forks**: R0rt1z2 (who did rook's, crown's and
radar's own unlocks) has a separate project, **`amonet-koboreru`** ([github.com/R0rt1z2/amonet-koboreru](https://github.com/R0rt1z2/amonet-koboreru)),
describing a more general "insecure Preloader USBDL mode" exploit for "some MediaTek based Amazon devices" (Echo
Shows named explicitly; Echo Plus/sonar not mentioned, not confirmed supported or tested) — entered, per its own
README, by "holding Volume Down and Mute (Power) while connecting USB during boot" on Echo devices generally (the
exact combo is model-specific elsewhere in this repo: rook needs all three buttons, radar needs the action button).
**Important caveat from that same README**: Amazon patched the underlying Preloader hole around 2022 — a Preloader
built after that is not vulnerable, so this only works against the original/old firmware, same principle
devices/README.md already states ("pin the firmware before it ever reaches Amazon").

No Echo Plus 1st gen/`sonar`-specific unlock thread or tool was found in this search (2026-11-09) — only the general
MT8163 exploit class and tooling that other models here already use.

**What's not yet answered**: exactly how to mechanically reach either board from outside the assembled speaker. The
bottom cap (4 screws, visible in the first internal-photo page) only exposes the speaker driver's ribbon cable, not
the Main Logic or Combo boards — those sit further up inside the cylindrical housing, meaning a fuller teardown
(removing the fabric sleeve and/or the board stack from the shell) is needed, the way any cylindrical Echo/Dot
teardown works (Brian Dorey's public Echo Dot/Echo Input teardowns show the general construction style, though not
this specific model, and no Echo Plus 1st gen-specific teardown write-up was found in a search done 2026-11-09). This
is an ordinary non-destructive disassembly question, not a soldering one.

Open, in order:

1. **Open the case far enough to reach the Combo Board's `J22` and/or the Main Logic Board's `J1100`/`J1101`.**
   Likely: the 4 screws under the bottom cap release the acoustic driver/base assembly; the board stack sits above
   it, reachable once that's out of the way — five boards connected by ribbons and headers (see the table above), so
   expect to work through more than one connector, not just lift a single board out. No teardown guide specific to
   this model is linked here yet; the general Echo-speaker disassembly pattern (iFixit, Brian Dorey's site) is well
   documented even where this exact model isn't, and is the thing worth reading before improvising.
2. **Confirm the board matches `FIRMWARE_BUILD`** before doing anything irreversible: the FCC internal photos are
   from the original 2017 certification (report dated via `EP693034-02`); a later hardware revision could differ
   (the firmware's own `audio_device.xml` already hints at a second "Sonar V2" speaker revision — see board.c). A
   mismatched board doesn't change the USB connector story but could change which stock libraries/offsets apply.
3. If neither `J22` nor the JTAG/debug headers pan out, the FCC schematics exhibits ("Main Logic Board_Schematics",
   "Combo Board_Schematics" — confidential/metadata-only on fccid.io as of 2026-11-09) would have the definitive
   pinout for both if they're ever released; check their status before falling back to pad-tracing by continuity.
4. Once wired: the BROM-entry mechanism itself (hold the right key while plugging in the power adapter, as rook's and
   donut's docs describe) is expected to be the same MT8163-family behaviour documented for every other model here —
   that part is not the open question anymore, finding a connector was.

## What's still open

devices/README.md's porting order, from the top:

1. **Unlock and root** — no known public project; a teardown is needed to reach the Combo Board's `J22` micro-USB
   connector or the Main Logic Board's JTAG/"JAWFISH DEBUG" headers (all confirmed to exist via the FCC internal-photos
   exhibit, see "USB access" above — likely no soldering needed after all, unlike the earlier donut-style assumption),
   but the exact disassembly steps to get there aren't
   written down yet; MT8163 BootROM-exploit class expected, unconfirmed on this unit. Not started on real hardware;
   everything below this line is firmware/public-document analysis, none of it run.
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
