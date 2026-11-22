# Porting the antenna fix to the Fire OS image (rook)

Status: **to do**. Done and verified on the plain-Linux image only (devices/rook/linux/README.md, 2026-11-22). This is
the list for the Fire OS (Android 5.1) image, in case the Linux image is not the route we keep.

## The fix, in short

The Spot's Wi-Fi chip (BCM43569) has two Wi-Fi chains, each with an antenna. Bluetooth shares chain 0's. Amazon's
nvram (`/vendor/firmware/brcm/bcm43569a2.nvm`) says so (`boardflags` with BFL_BTCOEXIST, `txchain=3`, `rxchain=3`), and
the firmware makes Bluetooth and Wi-Fi take turns at that antenna. On 2.4 GHz Wi-Fi, a phone's music then came short:
3-7 dropouts a minute. The fix is three nvram changes:

- `boardflags=0x02000001` -> `boardflags=0x02000000` (no shared antenna)
- `txchain=3` / `rxchain=3` -> `2` / `2` (Wi-Fi on chain 1 alone)
- `btc_mode=0` added (the firmware's coexistence off)

Result on Linux: 0 dropouts in 3 minutes, three times (one from a cold boot, one with no other change), packet gaps
30-35 ms instead of 90-140. Wi-Fi stays 802.11n (one stream on one antenna, -58 dBm against -52 on both).

On Linux, `devices/rook/linux/wifi.sh` makes the edited copy at boot (`/tmp/bcm43569a2-linux.nvm`) and loads the
driver with `nvram_path=`. On Fire OS the Wi-Fi HAL loads the module itself, with no arguments of ours, so the path has
to come from the driver's built-in default or the file it points at.

## To do

1. **Where the edited nvram lives.** `sysinstall.sh` already writes to /system (it installs our `bcmdhd.ko` through
   device.conf `WIFI_DRIVER`). Add a step that makes the edited copy from Amazon's file with the same three `sed`
   edits as `wifi.sh` (no Amazon file in the repo), checks that they took, and writes it as
   `/system/vendor/firmware/brcm/bcm43569a2-hassmic.nvm`. Amazon's file is left as it is, so `--uninstall` and the
   stock driver are unaffected. A device.conf entry (e.g. `WIFI_NVRAM_EDIT`) keeps it to rook.
2. **Point our driver at it.** In `devices/rook/bcmdhd.patch`, change `CONFIG_BCMDHD_NVRAM_PATH` to the new file, and
   have the driver fall back to Amazon's if it is missing (an install without step 1, or the file unreadable). Rebuild
   with `make kernel-tools DEVICE=rook`. The Linux image keeps passing `nvram_path=` and is unaffected.
3. **Check what Android's Wi-Fi stack does to it.** Fire OS's WifiStateMachine sends private driver commands such as
   `BTCOEXMODE` around DHCP and `BTCOEXSCAN-START/STOP` (bcmdhd `wl_cfg_btcoex.c`: they write `btc_params` 41, 66 and
   68 and `btc_flags` 7 to favour Wi-Fi). With `btc_mode 0` they should do nothing, but check with `wlctl` after a
   DHCP renew and a scan that `btc_mode`, `txchain` and `rxchain` are still 0, 2 and 2.
4. **`wlctl` on Fire OS.** It needs CAP_NET_ADMIN: run it from `adb root` (or as root through the satellite's root
   helper). The same static binary works (`devices/rook/linux/wlctl.c`).
5. **Measure.** On a Fire OS Spot on 2.4 GHz Wi-Fi: phone music for 3 minutes, phone set down. Count
   `a2dp: buffer ran dry`, not "ran dry" (the per-minute statistics line says "ran dry N times"). Before and after.
   Also check Wi-Fi itself still works: a join from the settings page, Improv, and reconnecting after a reboot.
6. **Keep the 5 GHz Spots in mind.** The Spots on a 5 GHz network do not share air with Bluetooth, but
   the change applies to them too: check that 5 GHz Wi-Fi throughput and range are fine on one chain.
7. **Record it.** devices/rook/README.md (the Bluetooth section), HANDOFF.md, CHANGELOG.md ("Its Wi-Fi and Bluetooth
   share one antenna: on 2.4 GHz Wi-Fi the music skips" is no longer true once it ships), and board.c's comment on
   `a2dp_buffer_ms`.

## Not needed

- The manufacturing-test firmware beside the production one (`bcm43569a2-firmware-test.bin`, WLTEST, 802.11n off by
  default) also allows Wi-Fi on chain 1, but the production firmware does with the edited nvram.
- hassmic's earlier attempt at the problem (making Wi-Fi sleep more while music plays) was removed again: it is not
  needed once the antennas are separate.
