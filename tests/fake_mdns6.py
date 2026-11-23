#!/usr/bin/env python3
"""".local" names, IPv6 first (net.c mdns_resolve6, then mdns_resolve4): Home Assistant's media server reached by its
mDNS name.  This test answers mDNS itself: on ff02::fb (the namespace's t0 under tests/run.py) with the network's ULA,
and on 224.0.0.251 (loopback) with 127.0.0.1.  ha-test.local has both addresses, ha-v4.local only an IPv4 one; the
media server takes both families.  Checked: for a name with both, the AAAA question comes first and the media is
fetched over IPv6 from the Echo's ULA, never IPv4; for a name with IPv4 alone, the A question after the AAAA one, the
media fetched over IPv4; and the Echo plays both.  Each of those answers is preceded by one with another address and
hop limit / TTL 64, as a forged answer from off the link arrives (RFC 6762 11: on-link responders send 255): ignored."""
import asyncio, os, socket, struct, sys, threading, time
from http.server import ThreadingHTTPServer
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from esphome_common import Handler, check, until, sync, home_assistant, run

PORTS = (16993, 16994, 16995, 16996, 16997)
MEDIA = 16998
BOTH, V4 = "ha-test.local", "ha-v4.local"


def iface():
    names = [n for _, n in socket.if_nameindex()]
    return "t0" if "t0" in names else next(n for n in names if n != "lo")


def ula():
    """the network's address there, as the Echo's: a ULA, else a global one"""
    best, rank = None, 0
    for l in open("/proc/net/if_inet6"):
        h, _, _, scope, _, dev = l.split()
        if dev != iface(): continue
        r = 3 if int(h[:2], 16) & 0xfe == 0xfc else 1 if scope == "20" else 2
        if r > rank: best, rank = socket.inet_ntop(socket.AF_INET6, bytes.fromhex(h)), r
    return best


def qname(m, off):
    parts = []
    while m[off]:
        parts.append(m[off + 1:off + 1 + m[off]].decode()); off += 1 + m[off]
    return ".".join(parts), off + 1


def hops(s, family, n):
    """the hop limit / TTL of what s sends by unicast: 255 is what a responder on the link sends (RFC 6762 11)"""
    if family == 6: s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_UNICAST_HOPS, n)
    else: s.setsockopt(socket.IPPROTO_IP, socket.IP_TTL, n)


asked = []                                  # (name, qtype, unicast bit, family) of each question
forged = []                                 # (name, qtype, family) of each answer sent as if from beyond a router
def answer(s, m, src, family, records, far):
    """one question; records: {(name, qtype): rdata}.  Answered by unicast, as asked; first, for a name in far, a
    wrong address with hop limit 64, as one forged from off the link arrives: the Echo must not take it"""
    if len(m) < 12 or m[2] & 0x80: return
    name, off = qname(m, 12)
    qtype, qclass = struct.unpack(">HH", m[off:off + 4])
    if name.lower() not in (BOTH, V4): return
    asked.append((name.lower(), qtype, qclass >> 15, family))
    rd = records.get((name.lower(), qtype))
    if rd is None: return
    for r, h in ((far.get((name.lower(), qtype)), 64), (rd, 255)):
        if r is None: continue
        ans = m[12:off] + struct.pack(">HHIH", qtype, 1, 120, len(r)) + r
        pkt = m[:2] + b"\x84\x00" + struct.pack(">HHHH", 1, 1, 0, 0) + m[12:off + 4] + ans
        if h != 255 and family == 6:
            # one forged from off the link comes with a routable source (no router forwards a link-local one): from
            # the namespace's ULA, not this socket's link-local
            f = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM); f.bind((ula(), 0)); hops(f, 6, h)
            f.sendto(pkt, src); f.close()
        else:
            hops(s, family, h); s.sendto(pkt, src)
        if h != 255: forged.append((name.lower(), qtype, family))


def responder6(stop):
    s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1); s.bind(("::", 5353))
    s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, socket.inet_pton(socket.AF_INET6, "ff02::fb") + struct.pack("@I", socket.if_nametoindex(iface())))
    s.settimeout(0.2)
    rec = {(BOTH, 28): socket.inet_pton(socket.AF_INET6, ula())}
    far = {(BOTH, 28): socket.inet_pton(socket.AF_INET6, "fd00:dead::1")}       # a ULA too: it would rank as high
    while not stop.is_set():
        try: m, src = s.recvfrom(1500)
        except socket.timeout: continue
        answer(s, m, src, 6, rec, far)


def responder4(stop):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", 5353))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton("224.0.0.251") + socket.inet_aton("127.0.0.1"))
    s.settimeout(0.2)
    lo = socket.inet_aton("127.0.0.1")
    rec = {(BOTH, 1): lo, (V4, 1): lo}
    far = {(V4, 1): socket.inet_aton("192.0.2.1")}                              # TEST-NET-1: nothing answers there
    while not stop.is_set():
        try: m, src = s.recvfrom(1500)
        except socket.timeout: continue
        answer(s, m, src, 4, rec, far)


class Dual(ThreadingHTTPServer):
    address_family = socket.AF_INET6
    def server_bind(self):
        self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0); super().server_bind()


got = []                                    # the media server's clients
class Media(Handler):
    def do_GET(self):
        got.append(self.client_address[0]); super().do_GET()


async def main(e):
    stop = threading.Event()
    for r in (responder6, responder4): threading.Thread(target=r, args=(stop,), daemon=True).start()
    httpd = Dual(("::", MEDIA), Media); threading.Thread(target=httpd.serve_forever, daemon=True).start()
    try:
        ha = await home_assistant(e)
        size0 = os.path.getsize(e.play)
        ha.cli.media_player_command(ha.mp[0].key, media_url=f"http://{BOTH}:{MEDIA}/a.wav"); await sync(ha.cli)
        await until(lambda: got and os.path.getsize(e.play) > size0 + 20000, 10)
        q = [(t, u, f) for n, t, u, f in asked if n == BOTH]
        check(q and q[0] == (28, 1, 6) and not any(t == 1 for t, _, _ in q), f"a name with both: AAAA asked first (unicast answer), A never: {q}")
        check((BOTH, 28, 6) in forged and got == [ula()], f"the media fetched over IPv6, from the Echo's ULA, not from the address answered first with hop limit 64: {got}")
        check(os.path.getsize(e.play) > size0 + 20000, f"and played ({os.path.getsize(e.play) - size0} bytes)")

        await asyncio.sleep(1); got.clear(); size1 = os.path.getsize(e.play)
        ha.cli.media_player_command(ha.mp[0].key, media_url=f"http://{V4}:{MEDIA}/a.wav"); await sync(ha.cli)
        await until(lambda: got and os.path.getsize(e.play) > size1 + 20000, 15)
        q = [(t, f) for n, t, u, f in asked if n == V4]
        check(q and q[0] == (28, 6) and (1, 4) in q and q.index((1, 4)) > 0, f"a name with IPv4 alone: AAAA first, then A over IPv4: {q}")
        check((V4, 1, 4) in forged and got and got[0] in ("::ffff:127.0.0.1", "127.0.0.1"), f"the media fetched over IPv4, not from the address answered first with TTL 64: {got}")
        check(os.path.getsize(e.play) > size1 + 20000, f"and played ({os.path.getsize(e.play) - size1} bytes)")
        await ha.cli.disconnect()
    finally:
        stop.set(); httpd.shutdown()

run(main, PORTS)
