#!/usr/bin/env python3
"""Compare a kernel we built with the one running on a device, function by function, by the structure offsets the
code uses: whether a module built against our tree sees the same struct layouts as the device's kernel.

Out-of-tree modules for an Echo (rook's bcmdhd.ko replacement) are built against a vendor tree whose exact config the
device does not carry (no IKCONFIG on rook): a config option that adds a field to sk_buff or net_device shifts every
offset after it, and a module that reads skb->len at the wrong place corrupts memory.  Branch targets and addresses
differ between two builds anyway, so this compares only what layouts decide: the immediate offsets of loads and stores
(ldr/str/ldp/stp... [xN, #imm]) and add/sub immediates, in order, per function.

  tools/kabi_compare.py VMLINUX IMAGE KALLSYMS [FUNCTION...]

VMLINUX: our build's ELF.  IMAGE: the device's arm64 Image (decompressed, flat from _text).  KALLSYMS: the device's
/proc/kallsyms (kptr_restrict 0).  Without FUNCTION, every function both have whose name says it touches what a Wi-Fi
driver uses (net, skb, cfg80211, wiphy, usb, wake lock...).  Prints per function same / differs, and a summary.
Needs pyelftools and capstone.
"""
import re
import sys

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs
from elftools.elf.elffile import ELFFile

PATTERN = re.compile(r'^(__)?(skb_|netif_|netdev_|dev_|alloc_netdev|free_netdev|register_netdev|unregister_netdev|'
                     r'cfg80211_|wiphy_|ieee80211_|regulatory_|nl80211_|usb_|wake_lock|wake_unlock|'
                     r'kfree_skb|consume_skb|__alloc_skb|__netdev_alloc_skb|eth_|ether_|rtnl_|'
                     r'sk_|sock_|napi_|wireless_|cfg_|rdev_)')


def kallsyms(path):
    syms = {}
    for line in open(path):
        parts = line.split()
        if len(parts) >= 3 and parts[1] in 'tT' and len(parts) == 3:       # the kernel's own, not modules'
            syms.setdefault(parts[2], int(parts[0], 16))
    return syms


def ours(path):
    f = ELFFile(open(path, 'rb'))
    text = f.get_section_by_name('.text')
    syms = {}
    for s in f.get_section_by_name('.symtab').iter_symbols():
        if s['st_info']['type'] == 'STT_FUNC' and s['st_size']:
            syms.setdefault(s.name, (s['st_value'], s['st_size']))
    # file data of the whole loadable image, by address
    segs = [(p['p_vaddr'], p.data()) for p in f.iter_segments() if p['p_type'] == 'PT_LOAD']
    def read(addr, n):
        for base, data in segs:
            if base <= addr < base + len(data):
                return data[addr - base:addr - base + n]
        return b''
    return syms, read, text


def shape(code, addr):
    """Offsets from registers that hold pointers into structures; what adrp (a global's page) feeds is left out: those
    offsets are where the linker put things, different in every build"""
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    out = []
    glob = set()                                    # registers holding a global's address (adrp, then add lo12)
    for i in md.disasm(code, addr):
        m = i.mnemonic
        ops = [o.strip() for o in i.op_str.split(',')]
        dst = ops[0].replace('w', 'x', 1) if ops and ops[0][:1] in 'wx' else None
        if m == 'adrp':
            glob.add(dst); continue
        if m.startswith(('ldr', 'str', 'ldp', 'stp', 'ldur', 'stur', 'ldx', 'stx', 'ldar', 'stlr', 'prfm')) and '[' in i.op_str:
            base = i.op_str.split('[')[1].split(',')[0].split(']')[0].strip()
            imm = re.search(r'\[\w+(?:, #(-?0x[0-9a-f]+|-?\d+))?\]', i.op_str)
            if imm and base not in ('sp', 'x29') and base not in glob:
                out.append((m, int(imm.group(1), 0) if imm.group(1) else 0))
            if m.startswith('ld') and dst:
                glob.discard(dst)
                if m.startswith('ldp') and len(ops) > 1: glob.discard(ops[1].replace('w', 'x', 1))
            continue
        if m in ('add', 'sub') and '#' in i.op_str and len(ops) >= 3:
            src = ops[1]
            if src in glob:
                glob.add(dst)                       # the global's address, completed
            elif src not in ('sp', 'x29'):
                out.append((m, ops[-1].lstrip('#')))
                glob.discard(dst)
            continue
        if m == 'ret':
            out.append(('ret', 0))
        if dst:
            glob.discard(dst)
    return out


def main():
    vmlinux, image, ksyms = sys.argv[1:4]
    want = sys.argv[4:]
    dev = kallsyms(ksyms)
    img = open(image, 'rb').read()
    syms, read, _ = ours(vmlinux)
    # The Image starts at _text, which kallsyms may not list (rook's does not): _stext is as far into the Image as it
    # is into ours (head text, the same in every build of one tree)
    f = ELFFile(open(vmlinux, 'rb'))
    allsyms = {s.name: s['st_value'] for s in f.get_section_by_name('.symtab').iter_symbols()}
    dev_text = dev['_text'] if '_text' in dev else dev['_stext'] - (allsyms['_stext'] - allsyms['_text'])
    names = want or sorted(n for n in syms if n in dev and PATTERN.match(n))
    order = sorted(dev.values())
    same = differ = 0
    bad = []
    for n in names:
        if n not in syms or n not in dev:
            print(f'{n}: missing ({"ours" if n not in syms else "device"})')
            continue
        a, size = syms[n]
        mine = shape(read(a, size), a)
        # the device's size: up to the next symbol
        da = dev[n]
        nxt = next((x for x in order if x > da), da + size)
        off = da - dev_text
        theirs = shape(img[off:off + min(nxt - da, size * 2)], da)
        theirs = theirs[:len(mine)] if len(theirs) > len(mine) and mine and mine[-1] == ('ret', 0) else theirs
        # Offsets as a set, not a sequence: another snapshot of GCC 4.9 schedules the same loads in another order,
        # and inlines a little differently.  A structure laid out differently shows as offsets one side never uses.
        # Only the loads and stores: add/sub immediates are arithmetic as often as field addresses.
        mo = {o for m, o in mine if m not in ('add', 'sub', 'ret') and isinstance(o, int)}
        to = {o for m, o in theirs if m not in ('add', 'sub', 'ret') and isinstance(o, int)}
        if mo == to or not (mo - to):
            same += 1
        else:
            differ += 1
            bad.append(n)
            print(f'{n}: ours only {sorted(mo - to)[:8]} device only {sorted(to - mo)[:8]}')
    print(f'\n{same} same, {differ} differ of {same + differ}')
    return 0 if not differ else 1


if __name__ == '__main__':
    sys.exit(main())
