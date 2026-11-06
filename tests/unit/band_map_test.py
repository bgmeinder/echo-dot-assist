#!/usr/bin/env python3
"""The Echo Show's band as the settings page draws it (web/ring.js bandColours) against what ring_band.c sends the
screen for the same design (design_draw, then unroll): which LED lands where (band position p = LED p + 5) and the
blend between them, pixel for pixel across 960, through node.  The two are written apart; this keeps them one.  Then the
page's other band code against that same order: the frame strip's thumbnails (frameCss), the designer's hit test
(bandCellAt), Mirror (left to right), Move/Run (one cell right per step, round to the other end) and Rainbow (from the
left end)."""
import json, os, random, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
W = 960
random.seed(26)
designs = [[[random.choice([0, 255, random.randrange(256)]) for _ in range(3)] for _ in range(12)] for _ in range(6)]
designs.append([[255, 0, 0] if i == 10 else [0, 255, 0] if i == 11 else [0, 0, 0] for i in range(12)])   # the talker's pair

# ring_band.c itself, its static functions reached by including it; the core stubbed as tests/unit/ring_band_test.c does
harness = r"""
#include "ring_band.c"
const struct board board = { .volume_steps = 100 };
int core_muted(void) { return 0; }
int core_dnd(int set) { (void)set; return 0; }
int core_led_brightness(int set) { (void)set; return 100; }
void core_spawn(char *const argv[]) { (void)argv; }
int https_open(const char *url) { (void)url; return -1; }   /* ring_band.c's artwork fetch: none here */
float core_lux(void) { return -1; }
int main(void)
{
    int s = slot_of(P_THINK), c; static struct rgb row[%d]; struct rgb led[LEDS];
    struct rd_frame fr = { .ms = 1000 };
    while (1) {
        for (int i = 0; i < LEDS; i++) for (int k = 0; k < 3; k++) { if (scanf("%%d", &c) != 1) return 0; fr.px[i][k] = (unsigned char)c; }
        des[s] = (struct rd_design){ .leds = LEDS, .nframes = 1, .loop = 0, .f = &fr };
        design_draw(s, P_THINK, led, 0, 0);
        unroll(led, row, %d);
        for (int x = 0; x < %d; x++) printf("%%ld %%ld %%ld\n", lroundf(clamp01(row[x].r) * 255), lroundf(clamp01(row[x].g) * 255), lroundf(clamp01(row[x].b) * 255));
    }
}
""" % (W, W, W)
# the round screen's table (send_table, into a file): "W rows n" then "pixel strip" pairs, as the renderer reads them
table_harness = r"""
#include "ring_band.c"
const struct board board = { .volume_steps = 100, .screen_round = 1 };
int core_muted(void) { return 0; }
int core_dnd(int set) { (void)set; return 0; }
int core_led_brightness(int set) { (void)set; return 100; }
void core_spawn(char *const argv[]) { (void)argv; }
int https_open(const char *url) { (void)url; return -1; }   /* ring_band.c's artwork fetch: none here */
float core_lux(void) { return -1; }
int main(int argc, char **argv)
{
    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int W = send_table(fd, atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
    close(fd);
    return W > 0 ? 0 : 1;
}
"""
import struct
SCREEN, ROWS = 480, 10
with tempfile.TemporaryDirectory() as tmp:
    src, exe = os.path.join(tmp, "band_map.c"), os.path.join(tmp, "band_map")
    open(src, "w").write(harness)
    subprocess.run(["cc", "-O2", "-w", f"-I{ROOT}/src/hassmic", src, f"{ROOT}/src/hassmic/ringdesign.c", f"{ROOT}/src/hassmic/statefile.c", f"{ROOT}/src/hassmic/screen_none.c", f"{ROOT}/src/hassmic/clockface.c", f"{ROOT}/src/hassmic/json.c",
                    "-lz", "-lm", "-lpthread", "-o", exe], check=True)
    out = subprocess.run([exe], input=" ".join(str(v) for d in designs for c in d for v in c), capture_output=True, text=True, check=True).stdout
    src2, exe2, tab = os.path.join(tmp, "table.c"), os.path.join(tmp, "table"), os.path.join(tmp, "table.bin")
    open(src2, "w").write(table_harness)
    subprocess.run(["cc", "-O2", "-w", f"-I{ROOT}/src/hassmic", src2, f"{ROOT}/src/hassmic/ringdesign.c", f"{ROOT}/src/hassmic/statefile.c", f"{ROOT}/src/hassmic/screen_none.c", f"{ROOT}/src/hassmic/clockface.c", f"{ROOT}/src/hassmic/json.c",
                    "-lz", "-lm", "-lpthread", "-o", exe2], check=True)
    subprocess.run([exe2, tab, str(SCREEN), str(SCREEN), str(ROWS)], check=True, stderr=subprocess.DEVNULL)
    raw = open(tab, "rb").read()
    c_W, c_rows_n, c_n = struct.unpack("<3I", raw[:12])
    c_table = dict(struct.iter_unpack("<2I", raw[12:12 + 8 * c_n]))        # layer pixel -> strip pixel
c_rows = [list(map(int, l.split())) for l in out.split("\n") if l]

js = r"""
const vm = require('vm'), fs = require('fs');
const ctx = vm.createContext({ console });
vm.runInContext(fs.readFileSync(process.argv[1], 'utf8'), ctx, { filename: 'ring.js' });
vm.runInContext('ring.status = { band: true, band_on: true }', ctx);
const t = JSON.parse(fs.readFileSync(0, 'utf8'));
const fn = (name) => vm.runInContext(name, ctx);           // a const of ring.js's: not a property of the context
const rows = t.designs.map((px) => ctx.bandColours(px, t.w).map((c) => c.map((v) => Math.round(v))));
// the rest in the band's order: what shows at the middle of cell p, for a design and what the designer makes of it
const at = (px, p) => ctx.bandColours(px, t.w)[Math.floor((p + 0.5) * t.w / 12)].map((v) => Math.round(v));
const px = t.designs[0], n = 12, B = vm.runInContext('BAND_BOX', ctx), cell = (B.x1 - B.x0) / n;
const css = ctx.frameCss(px, true), stops = [...css.matchAll(/(rgb\((\d+),(\d+),(\d+)\)|#07090a) [\d.]+%/g)].map((m) => (m[2] ? [+m[2], +m[3], +m[4]] : [0, 0, 0]));
const lit = (c) => c[0] || c[1] || c[2];
const extra = {
  thumbs: Array.from({ length: n }, (_, p) => { const c = px[(p + 5) % n]; return JSON.stringify(stops[2 * p]) === JSON.stringify(lit(c) ? c : [0, 0, 0]); }).every(Boolean),
  hit: Array.from({ length: n }, (_, p) => ctx.bandCellAt(B.x0 + (p + 0.5) * cell, (B.y0 + B.y1) / 2, n) === (p + 5) % n).every(Boolean)
       && ctx.bandCellAt(0.5, 0.3, n) === -1,
  mirror: Array.from({ length: n }, (_, p) => JSON.stringify(at(fn("mirrorPx")(px), p)) === JSON.stringify(at(px, n - 1 - p))).every(Boolean),
  move: Array.from({ length: n }, (_, p) => JSON.stringify(at(fn("rotPx")(px, 1), p)) === JSON.stringify(at(px, (p + n - 1) % n))).every(Boolean),
  rainbow: (() => { const w = ctx.wheelPx(n, true); return JSON.stringify(w[(0 + 5) % n]) === '[255,0,0]' && JSON.stringify(w[(2 + 5) % n]) === '[255,255,0]'; })(),
};
// the round screen: every pixel's strip pixel by the page's roundMap, as a flat list [pixel, row * W + col, ...]
const round = [];
for (let y = 0; y < t.screen; y++) for (let x = 0; x < t.screen; x++) { const m = ctx.roundMap(x, y, t.screen, t.screen, t.rows); if (m) round.push(y * t.screen + x, m[0] * m[2] + m[1], m[2]); }
// the round hit test: cell p's middle, clockwise from the top at the rim, paints LED p + 5; the centre nothing
const rhit = Array.from({ length: n }, (_, p) => { const a = (p + 0.5) * 2 * Math.PI / n; return ctx.roundCellAt(0.5 + 0.47 * Math.sin(a), 0.5 - 0.47 * Math.cos(a), n) === (p + 5) % n; }).every(Boolean)
  && ctx.roundCellAt(0.5, 0.5, n) === -1;
const rcss = ctx.frameCss(px, true, true);
console.log(JSON.stringify({ rows, extra, round, rhit, rconic: rcss.startsWith('conic-gradient(') }));
"""
r = subprocess.run(["node", "-e", js, f"{ROOT}/web/ring.js"], input=json.dumps({"designs": designs, "w": W, "screen": SCREEN, "rows": ROWS}), capture_output=True, text=True)
if r.returncode: sys.exit(f"node: {r.stderr.strip()}")
res = json.loads(r.stdout)
js_rows = [px for d in res["rows"] for px in d]

bad = [(i // W, i % W, a, b) for i, (a, b) in enumerate(zip(c_rows, js_rows)) if max(abs(x - y) for x, y in zip(a, b)) > 1]
ok = len(c_rows) == len(js_rows) == W * len(designs) and not bad
print(f"{'ok  ' if ok else 'FAIL'} the page's band and ring_band.c's: {len(designs)} designs, {W} pixels each, within 1 of 255"
      + ("" if ok else f" ({len(bad)} pixels apart, first: design {bad[0][0]} x {bad[0][1]}: C {bad[0][2]} page {bad[0][3]})" if bad else f" (rows {len(c_rows)} / {len(js_rows)})"))
mid = js_rows[-W + 440], js_rows[-W + 520]
print(f"{'ok  ' if mid == ([255, 0, 0], [0, 255, 0]) else 'FAIL'} LED 10 left of the middle, 11 right of it ({mid})")
words = {"thumbs": "the frame strip's thumbnails: cell p shows LED p + 5", "hit": "the designer's hit test: cell p paints LED p + 5",
         "mirror": "Mirror: left to right on the band", "move": "Move right / Run: one cell to the right, round to the other end",
         "rainbow": "Rainbow on the band: red at the left end, yellow two cells on"}
for k, v in res["extra"].items(): print(f"{'ok  ' if v else 'FAIL'} {words[k]}")
# the round screen: the page's roundMap against send_table's table, pixel for pixel (the C in float, the page in double:
# a pixel on a boundary may land one strip pixel over, so those count apart only beyond one)
js_table = {res["round"][i]: res["round"][i + 1] for i in range(0, len(res["round"]), 3)}
js_W = res["round"][2] if res["round"] else 0
off = [(p, a, js_table.get(p)) for p, a in c_table.items() if js_table.get(p) is None or (abs(js_table[p] - a) > 1 and abs(js_table[p] - a) not in (c_W - 1, c_W))]
extra_js = len(set(js_table) - set(c_table))
rok = c_W == js_W == 1477 and len(c_table) == len(js_table) and not off and not extra_js
print(f"{'ok  ' if rok else 'FAIL'} the round screen: the page's ring and send_table's, {len(c_table)} pixels of {SCREEN}x{SCREEN}, "
      f"the strip {c_W} wide" + ("" if rok else f" (page W {js_W}, {len(js_table)} pixels, {len(off)} apart, {extra_js} extra; first {off[:2]})"))
print(f"{'ok  ' if res['rhit'] else 'FAIL'} the round hit test: cell p from the top clockwise paints LED p + 5, the centre nothing")
print(f"{'ok  ' if res['rconic'] else 'FAIL'} the round screen's thumbnails: a conic gradient")
sys.exit(0 if ok and mid == ([255, 0, 0], [0, 255, 0]) and all(res["extra"].values()) and rok and res["rhit"] and res["rconic"] else 1)
