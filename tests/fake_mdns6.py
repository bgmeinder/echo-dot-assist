#!/usr/bin/env python3
"""A ".local" name over IPv6 (net.c mdns_resolve6): Home Assistant's media server reached by its mDNS name on a network
without IPv4.  This test answers mDNS on ff02::fb (port 5353, the namespace's t0 under tests/run.py) for ha-test.local
with the network's ULA, serves the media on IPv6 only, and has Home Assistant play http://ha-test.local:<port>/a.wav.
Checked: the Echo asked an AAAA question (not A), the media came over IPv6 from the Echo's ULA, and the Echo played it."""
import asyncio, os, socket, struct, sys, threading, time
from http.server import ThreadingHTTPServer
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from esphome_common import Handler, check, until, sync, home_assistant, run

PORTS = (16993, 16994, 16995, 16996, 16997)
MEDIA = 16998
NAME = "ha-test.local"


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


asked = []                                  # (qtype, unicast bit) of each question for NAME
def responder(stop):
    s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("::", 5353))
    s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, socket.inet_pton(socket.AF_INET6, "ff02::fb") + struct.pack("@I", socket.if_nametoindex(iface())))
    s.settimeout(0.2)
    me = socket.inet_pton(socket.AF_INET6, ula())
    while not stop.is_set():
        try: m, src = s.recvfrom(1500)
        except socket.timeout: continue
        if len(m) < 12 or m[2] & 0x80: continue
        name, off = qname(m, 12)
        qtype, qclass = struct.unpack(">HH", m[off:off + 4])
        if name.lower() != NAME: continue
        asked.append((qtype, qclass >> 15))
        if qtype != 28: continue
        q = m[12:off + 4]
        name_wire = m[12:off]
        ans = name_wire + struct.pack(">HHIH", 28, 1, 120, 16) + me
        s.sendto(m[:2] + b"\x84\x00" + struct.pack(">HHHH", 1, 1, 0, 0) + q + ans, src)     # unicast, as asked


class V6(ThreadingHTTPServer):
    address_family = socket.AF_INET6


got = []                                    # the media server's clients
class Media(Handler):
    def do_GET(self):
        got.append(self.client_address[0]); super().do_GET()


async def main(e):
    stop = threading.Event(); threading.Thread(target=responder, args=(stop,), daemon=True).start()
    httpd = V6(("::", MEDIA), Media); threading.Thread(target=httpd.serve_forever, daemon=True).start()
    try:
        ha = await home_assistant(e)
        size0 = os.path.getsize(e.play)
        ha.cli.media_player_command(ha.mp[0].key, media_url=f"http://{NAME}:{MEDIA}/a.wav"); await sync(ha.cli)
        await until(lambda: got and os.path.getsize(e.play) > size0 + 20000, 10)
        check(asked and all(t == 28 for t, _ in asked) and any(u for _, u in asked), f"asked for AAAA, a unicast answer wanted: {asked}")
        check(got == [ula()], f"the media fetched over IPv6, from the Echo's ULA: {got}")
        check(os.path.getsize(e.play) > size0 + 20000, f"and played ({os.path.getsize(e.play) - size0} bytes)")
        await ha.cli.disconnect()
    finally:
        stop.set(); httpd.shutdown()

run(main, PORTS)
