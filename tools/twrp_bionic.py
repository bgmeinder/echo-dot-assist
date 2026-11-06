#!/usr/bin/env python3
"""twrp_bionic: a bionic runtime for qemu-aarch64 out of a TWRP image, in the standard library only.

  twrp_bionic.py TWRP.img OUTDIR        OUTDIR/system/bin/linker64 and OUTDIR/sbin/{linker64,*.so} from the ramdisk

Where to run magiskpolicy for an Echo whose own linker cannot (rook: Android 5.1's wants DT_HASH, which Magisk's
builds lack, and the Spot's TWRP is 64-bit, so the 32-bit magiskpolicy32 of donut's boot-root has nothing to run on
there either).  Magisk's arm64 libmagiskpolicy.so (dynamically linked, interpreter /system/bin/linker64, needing
libc, libm, libdl) runs under qemu-aarch64 against this directory:

  qemu-aarch64 -E LD_LIBRARY_PATH=/sbin -L OUTDIR magiskpolicy64 --load sepolicy --apply rules --save out

amonet's TWRP (amonet-rook-v2.0.0: bin/twrp.img) is an Android boot image whose ramdisk is LZMA (not gzip), with
TWRP's whole userland flat in /sbin: linker64 and the libraries beside it, hence LD_LIBRARY_PATH=/sbin.  Only the
loader and the shared objects are taken; the libraries are 64-bit (a 32-bit linker sits beside them without libraries
of its own, so it is left out)."""
import lzma, os, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))
import mkbootroot  # noqa: E402


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__)
    boot = mkbootroot.Boot(open(argv[0], "rb").read())
    raw = boot.ramdisk
    cpio = lzma.decompress(raw) if raw[:2] == b"\x5d\x00" or raw[:6] == b"\xfd7zXZ\x00" else mkbootroot.gunzip_mem(raw)
    out, n = argv[1], 0
    for name, _, data in mkbootroot.entries(cpio):
        name = name.decode()
        if name == "sbin/linker64" or (name.startswith("sbin/") and name.endswith(".so") and "/" not in name[5:]):
            for p in ([out + "/system/bin/linker64", out + "/sbin/linker64"] if name == "sbin/linker64" else [out + "/" + name]):
                os.makedirs(os.path.dirname(p), exist_ok=True)
                open(p, "wb").write(data)
                os.chmod(p, 0o755)
            n += 1
    assert os.path.exists(out + "/sbin/linker64") and os.path.exists(out + "/sbin/libc.so"), "no linker64 and libc.so in that TWRP"
    print("%s: %d files from %s" % (out, n, argv[0]))


if __name__ == "__main__":
    main(sys.argv[1:])
