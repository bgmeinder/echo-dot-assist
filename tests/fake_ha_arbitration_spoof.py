#!/usr/bin/env python3
"""Wake word arbitration (tests/arbecho.py) against a device on the LAN that answers or replays what it hears.
A member's recorded packet, replayed later from another address (the test's ULA ranks above the IPv4 the member is
heard at), does not move the member's address; the other-family copy of a fresh packet still does (arb.c peer_addr).
Pairing: the Echo that asks takes a key only if one Echo alone hands it one; a forger answering too ends the pairing
with neither (arb.c pair_offer), and the member alone still pairs it, its IPv6 and IPv4 copies one giver."""
import asyncio, hashlib, os, socket, struct
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from arbecho import ARB, BCAST, GROUP, Echo, HA, Browser, check, until, start, run, arb_iface, arb_sender, own_addr

NID, KEY = 2, os.urandom(32)                        # the kitchen's network, which the test holds too (a member's)
ADDR_COPY = 1.0                                     # arb.c ADDR_COPY_MS: the two copies of one packet come within it
a, b = Echo("Echo Kitchen", 16961, 500, net=(NID, KEY)), Echo("Echo Living Room", 16962, 900)


def raw(k): return k.public_bytes(Encoding.Raw, PublicFormat.Raw)
def blake(data, key, n): return hashlib.blake2b(data, key=key, digest_size=n).digest()
def node(s): return bytes([len(s)]) + s.encode()


def hchacha20(key, n16):
    """HChaCha20 (XChaCha20's subkey), as Monocypher's crypto_chacha20_h"""
    M = 0xffffffff; s = [0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, *struct.unpack("<8I", key), *struct.unpack("<4I", n16)]
    rot = lambda v, n: ((v << n) | (v >> (32 - n))) & M
    def qr(a_, b_, c_, d_):
        s[a_] = (s[a_] + s[b_]) & M; s[d_] = rot(s[d_] ^ s[a_], 16)
        s[c_] = (s[c_] + s[d_]) & M; s[b_] = rot(s[b_] ^ s[c_], 12)
        s[a_] = (s[a_] + s[b_]) & M; s[d_] = rot(s[d_] ^ s[a_], 8)
        s[c_] = (s[c_] + s[d_]) & M; s[b_] = rot(s[b_] ^ s[c_], 7)
    for _ in range(10):
        qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15)
        qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14)
    return struct.pack("<8I", *s[0:4], *s[12:16])


def give(x, to, net, k):
    """a "give" (T_GIVE) from identity x: network net's key k sealed to the Echo with public key to, as arb.c seal()"""
    xp = raw(x.public_key())
    shared = x.exchange(X25519PublicKey.from_public_bytes(to))
    wrap = blake(b"hassmic arb key\0" + xp + to + struct.pack("<Q", net), shared, 32)
    nonce = os.urandom(24)
    ct = ChaCha20Poly1305(hchacha20(wrap, nonce[:16])).encrypt(b"\0" * 4 + nonce[16:], k, xp)    # XChaCha20-Poly1305
    return b"HMA1\x06" + to + struct.pack("<Q", net) + xp + nonce + ct[32:] + ct[:32]


async def main():
    ha = HA([a, b]); ha.tags_on = False
    a.allowed = b.allowed = False                   # no tags, no actions: the living room joins by pairing alone
    await start((a, b), ha, login=False)
    a.page = Browser(a.web); check(a.page.login_with_button(a.proc), "the kitchen's settings page logged in")

    # 1. a member the test plays (it holds the network's key), heard over IPv4 at first
    mac = blake(b"hassmic arbitration mac", KEY, 32)
    ppub, pnode = os.urandom(32), "echo-fake"
    def beacon(c):
        p = b"HMA1\x01" + ppub + struct.pack("<Q", NID) + node(pnode) + struct.pack("<Q", c)
        return p + blake(p, mac, 16)
    # to everyone, as the Echos send (a unicast would reach only one of the two Echos on the port)
    s4 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s4.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s6 = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM); s6.bind((own_addr(), 0))
    idx = socket.if_nametoindex(arb_iface()); s6.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF, idx)
    v4 = lambda p: s4.sendto(p, (BCAST, ARB))
    ula = lambda p: s6.sendto(p, (GROUP, ARB, 0, idx))      # from the test's ULA: a better address than IPv4's
    def ip(): return next((m["ip"] for m in a.page.state()["arbitration"]["members"] if m["node"] == pnode), None)
    v4(beacon(1))
    check(await until(lambda: ip() == "::ffff:127.0.0.1", 3), f"a member heard over IPv4: kept at its IPv4 address ({ip()})")
    v4(beacon(2)); await asyncio.sleep(ADDR_COPY * 1.5)
    ula(beacon(1)); await asyncio.sleep(0.5)
    check(ip() == "::ffff:127.0.0.1", f"an older beacon of the member's replayed from a ULA: the member's address stays ({ip()})")
    ula(beacon(2)); await asyncio.sleep(0.5)
    check(ip() == "::ffff:127.0.0.1", f"its last beacon replayed from a ULA after the copy window: the address stays ({ip()})")
    v4(beacon(3)); ula(beacon(3))
    check(await until(lambda: ip() == own_addr(), 2), f"a fresh beacon and its IPv6 copy at once: the member moves to the ULA ({ip()})")

    # 2. pairing: a forger alone answers the living room's request: it joins (one giver: as before)
    _, send = arb_sender()
    x, xnet, xkey = X25519PrivateKey.generate(), 1, os.urandom(32)
    b.pair(); await asyncio.sleep(0.3)
    for _ in range(2): send(give(x, b.pub(), xnet, xkey))
    ok = await until(lambda: (b.net() or "").startswith(f"{xnet:016x}"), 4)
    check(ok and "handed to us, taken in" in b.text() and "(key from pairing)" in b.text(),
          f"one Echo alone hands a key: held, then taken ({b.net() and b.net()[:16]})")
    await b.reset()

    # 3. the forger answers first and the member hands its network too: the living room joins neither
    mark = len(b.text())
    b.pair(); await asyncio.sleep(0.3)
    for _ in range(2): send(give(x, b.pub(), xnet, xkey))
    a.pair()
    await until(lambda: "pairing refused" in b.text()[mark:], 4)
    await asyncio.sleep(2)                          # the member's three copies of its "give", each both ways
    theirs = lambda: (b.net() or "")[:16] in (f"{xnet:016x}", f"{NID:016x}")
    check("pairing refused: 2 Echos handed a network at once" in b.text()[mark:] and "pairing: handing network" in a.text()
          and not theirs(), f"two Echos hand a key in one pairing: refused, in neither's network ({b.net() and b.net()[:16]})")

    # 4. the member alone: its copies (three, over IPv6 and IPv4) are one giver, and the retry joins
    mark = len(b.text())
    b.pair(); await asyncio.sleep(0.3); a.pair()
    ok = await until(lambda: b.net() == a.net(), 8)
    check(ok and "(key from pairing)" in b.text()[mark:] and "pairing refused" not in b.text()[mark:],
          "the member alone hands its network: joined, its IPv6 and IPv4 copies one giver")

run(main, (a, b))
