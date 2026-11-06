#!/usr/bin/env python3
"""Runs scripts/device/ringbar.dex on the PC with tests/unit/dexrun.py's Dalvik interpreter, against fakes of
what its reflection reaches: the window manager's binder (overscan), DisplayManagerGlobal's DisplayInfo, and
SurfaceSession/SurfaceControl/Surface/Canvas.  What it checks is what hassmic relies on: argv[1] = rows, the
overscan on the natural side that is at the bottom, a layer of width x rows at the bottom above every window,
"ready <w> <rows>" (flushed) before stdin is read, every whole frame of stdin drawn as it came (little-endian
0xAARRGGBB, rows top to bottom), however the pipe splits it, and the end at EOF; "0" resets the overscan and
says "off".  The reflected names are tools/mkringbar.py's lists, which --framework checks against 8149."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dexrun                                                  # noqa: E402
from dexrun import ROOT, i32                                   # noqa: E402
import mkringbar as mk                                         # noqa: E402  (dexrun put tools/ on the path)

DEX = os.path.join(ROOT, "scripts", "device", "ringbar.dex")


class Fake:
    env = None                                                 # the JavaEnv of the run, set by it

    def log(self, *what):
        self.env.log.append(what)


class WindowManager(Fake):                                     # IWindowManager's binder proxy
    JAVA = "android.view.IWindowManager$Stub$Proxy"
    AS = ("Landroid/view/IWindowManager;",)

    def setOverscan(self, display, left, top, right, bottom):
        self.log("overscan", display, left, top, right, bottom)


class DisplayInfo(Fake):
    JAVA = "android.view.DisplayInfo"

    def __init__(self, w, h, rot):
        self.logicalWidth, self.logicalHeight, self.rotation = w, h, rot


class DisplayManagerGlobal(Fake):
    JAVA = "android.hardware.display.DisplayManagerGlobal"

    def getDisplayInfo(self, display):
        assert display == 0, display
        return DisplayInfo(*self.env.display)


class SurfaceSession(Fake):
    JAVA = "android.view.SurfaceSession"

    def __init__(self):
        self.log("session")


class SurfaceControl(Fake):
    JAVA = "android.view.SurfaceControl"

    def __init__(self, session, name, w, h, fmt, flags, built=False):
        assert built or not self.env.api9, "Android 9 has no SurfaceControl(session, name, w, h, format, flags)"
        assert isinstance(session, SurfaceSession)
        self.env.sc = self
        self.log("surfacecontrol", name, w, h, fmt, flags)

    def in_tx(self, *what):
        assert self.env.tx, f"{what[0]} outside a transaction"
        self.log(*what)

    def setLayer(self, z): self.in_tx("layer", z)
    def setPosition(self, x, y): self.in_tx("position", x, y)
    def show(self): self.in_tx("show")


class SurfaceControlBuilder(Fake):                             # Android 8+: the only way to make a layer on Android 9
    JAVA = "android.view.SurfaceControl$Builder"

    def __init__(self, session):
        assert self.env.api9, "SurfaceControl.Builder was asked for on a platform that has the constructor"
        assert isinstance(session, SurfaceSession)
        self.name = self.size = self.fmt = self.flags = None

    def setName(self, name): self.name = name; return self
    def setSize(self, w, h): self.size = (w, h); return self
    def setFormat(self, fmt): self.fmt = fmt; return self
    def setFlags(self, flags): self.flags = flags; return self

    def build(self):
        assert None not in (self.name, self.size, self.fmt, self.flags), "built before every setter was called"
        return SurfaceControl(SurfaceSession(), self.name, *self.size, self.fmt, self.flags, built=True)


class Bitmap(Fake):                                            # the ring mode's text bitmap (Bitmap.createBitmap)
    JAVA = "android.graphics.Bitmap"

    def __init__(self, w, h):
        self.w, self.h, self.px = w, h, [0] * (w * h)
        self.log("bitmap", w, h)

    def eraseColor(self, c):
        self.px = [c] * (self.w * self.h); self.log("erase", c)

    def getPixels(self, out, off, stride, x, y, w, h):
        assert (off, stride, x, y, w, h) == (0, self.w, 0, 0, self.w, self.h), (off, stride, x, y, w, h)
        out[:] = self.px; self.log("getPixels")


class BitmapCanvas(Fake):                                      # new Canvas(bitmap): drawText puts one pixel of the paint's colour at (x, y)
    JAVA = "android.graphics.Canvas"

    def __init__(self, bmp):
        assert isinstance(bmp, Bitmap)
        self.bmp = bmp; self.log("canvas")

    def drawBitmap(self, bm, src, dst, paint):               # the image: its first byte as the colour of its top-left pixel
        assert isinstance(bm, Decoded) and src is None and isinstance(dst, Rect) and isinstance(paint, Paint)
        l, t, r, b = dst.box
        self.bmp.px[t * self.bmp.w + l] = i32(0xff000000 | bm.data[0])
        self.log("image", bytes(bm.data), dst.box, paint.flags)

    def drawText(self, s, x, y, paint):
        assert isinstance(paint, Paint) and paint.size is not None and paint.color is not None
        self.bmp.px[int(y) * self.bmp.w + int(x)] = paint.color
        self.log("text", s, x, y, paint.size, paint.color, paint.align)


class Rect(Fake):
    JAVA = "android.graphics.Rect"

    def __init__(self, l, t, r, b):
        self.box = (l, t, r, b)


class Decoded(Fake):                                           # what BitmapFactory.decodeByteArray returns
    JAVA = "android.graphics.Bitmap"
    AS = ("Landroid/graphics/Bitmap;",)

    def __init__(self, data):
        self.data = data


class Paint(Fake):
    JAVA = "android.graphics.Paint"

    def __init__(self, flags):
        self.flags, self.size, self.color, self.align = flags, None, None, None
        self.log("paint", flags)

    def setTextAlign(self, a): self.align = a
    def setTextSize(self, s): self.size = s
    def setColor(self, c): self.color = c


class Canvas(Fake):                                            # what lockCanvas returns: Surface's own subclass
    JAVA = "android.view.Surface$CompatibleCanvas"
    AS = ("Landroid/graphics/Canvas;",)

    def drawBitmap(self, colors, offset, stride, x, y, w, h, alpha, paint):
        assert self.env.locked is self, "draws on a canvas not locked"
        self.log("draw", list(colors), offset, stride, x, y, w, h, alpha, paint)

    def drawColor(self, colour, mode):
        assert self.env.locked is self, "wipes a canvas not locked"
        self.log("wipe", colour, mode)


class Surface(Fake):
    JAVA = "android.view.Surface"

    def __init__(self):
        self.of = None

    def copyFrom(self, sc):
        assert isinstance(sc, SurfaceControl) and not self.env.tx
        self.of = sc
        self.log("copyFrom")

    def lockCanvas(self, rect):
        assert self.of is self.env.sc and self.env.locked is None, "lockCanvas: not set up, or locked twice"
        self.env.locked = Canvas()
        self.log("lock", rect)
        return self.env.locked

    def unlockCanvasAndPost(self, c):
        assert c is self.env.locked, "posts a canvas it did not lock"
        self.env.locked = None
        self.log("post")


class JavaEnv(dexrun.JavaEnv):
    CLASSES = (WindowManager, DisplayInfo, DisplayManagerGlobal, SurfaceSession, SurfaceControl, SurfaceControlBuilder,
               Surface, Canvas, Bitmap, BitmapCanvas, Paint, Rect)
    NAMES = ("android.os.ServiceManager", "android.view.IWindowManager$Stub", "android.os.IBinder",
             "android.graphics.Rect", "android.graphics.Paint", "[I", "android.graphics.Bitmap$Config",
             "android.graphics.Paint$Align", "java.lang.System", "java.lang.Object", "android.graphics.PorterDuff$Mode",
             "android.graphics.BitmapFactory", "[B")

    def static_field(self, cls, name):
        assert (cls, name) in (("android.graphics.Bitmap$Config", "ARGB_8888"), ("android.graphics.Paint$Align", "CENTER"),
                               ("android.graphics.PorterDuff$Mode", "CLEAR")), (cls, name)
        return name

    def __init__(self, display, stdin=(), api9=False):
        super().__init__(mk.PROG, stdin)
        Fake.env = self
        self.api9 = api9
        self.display, self.log, self.tx, self.sc, self.locked = display, [], False, None, None
        self.wm, self.dmg = WindowManager(), DisplayManagerGlobal()

    def static_call(self, cls, name, args):
        if (cls, name) == ("android.os.ServiceManager", "getService"):
            assert args == ["window"], args; return "binder:window"
        if (cls, name) == ("android.view.IWindowManager$Stub", "asInterface"):
            assert args == ["binder:window"], args; return self.wm
        if (cls, name) == ("android.hardware.display.DisplayManagerGlobal", "getInstance"):
            return self.dmg
        if cls == "android.view.SurfaceControl" and name in ("openTransaction", "closeTransaction"):
            assert self.tx == (name == "closeTransaction"), f"{name} twice"
            self.tx = name == "openTransaction"
            self.log.append((name,)); return None
        if (cls, name) == ("android.graphics.Bitmap", "createBitmap"):
            assert args[2] == "ARGB_8888", args
            return Bitmap(args[0], args[1])
        if (cls, name) == ("android.graphics.BitmapFactory", "decodeByteArray"):
            data, off, n = args
            data = [b & 0xff for b in data[off:off + n]]
            return Decoded(data) if data[:1] != [0] else None           # a first byte of 0: does not decode
        if (cls, name) == ("java.lang.System", "arraycopy"):
            src, sp, dst, dp, n = args
            dst[dp:dp + n] = src[sp:sp + n]; return None
        return super().static_call(cls, name, args)



def frame(w, h, seed):
    """a frame as hassmic writes it, and the ints Canvas must get: 0xAARRGGBB, signed as Java has them"""
    px = [(0xff000000 | (i * 2654435761 + seed * 40503) & 0xffffff) if i % 7 else (i * seed) & 0xffffffff
          for i in range(w * h)]                               # some with alpha below 0xff, and 0 too
    return struct.pack("<%dI" % len(px), *px), [i32(p) for p in px]


def main():
    dex = dexrun.Dex(open(DEX, "rb").read(), mk.PROG)
    check = dexrun.Checks()

    def run(argv, display, stdin=(), api9=False):
        env = JavaEnv(display, stdin, api9)
        dex.run(env, "main", [list(argv)])
        return env

    # the Echo Show 5: a 480x960 panel shown as 960x480, rotation 1
    f1, px1 = frame(960, 10, 1)
    f2, px2 = frame(960, 10, 2)
    env = run(["10"], (960, 480, 1), [f1[:1000], f1[1000:], f2])
    log = env.log
    check(log[0] == ("overscan", 0, 10, 0, 0, 0), "rotation 1: setOverscan(0, 10, 0, 0, 0), the natural left edge")
    check(("surfacecontrol", "hassmic-band", 960, 10, -1, 4) in log,
          "a 960x10 layer, PixelFormat.OPAQUE, created HIDDEN")
    setup = [e for e in log if e[0] in ("openTransaction", "layer", "position", "show", "closeTransaction", "copyFrom")]
    check(setup == [("openTransaction",), ("layer", 2000000000), ("position", 0.0, 470.0), ("show",),
                    ("closeTransaction",), ("copyFrom",)],
          "in one transaction: layer 2000000000, at (0, 470), shown; then the Surface made from it")
    check(env.out == ["ready 960 10"] and env.flushed == 1, "prints \"ready 960 10\", flushed before stdin is read")
    draws = [e for e in log if e[0] == "draw"]
    want = lambda px: ("draw", px, 0, 960, 0, 0, 960, 10, False, None)
    check(len(draws) == 2 and draws[0] == want(px1), "frame 1 (two short reads): exactly its pixels, stride 960, "
          "960x10 at 0,0, no alpha, no Paint")
    check(len(draws) == 2 and draws[1] == want(px2), "frame 2: exactly its pixels")
    frames = [e[0] for e in log if e[0] in ("lock", "draw", "post")]
    check(frames == ["lock", "draw", "post"] * 2 and all(e[1] is None for e in log if e[0] == "lock"),
          "each frame: lockCanvas(null), drawBitmap on that canvas, unlockCanvasAndPost of it")
    check(env.stdin.reads == 4 and env.locked is None, "returns at the end of stdin, nothing left locked")

    env = run(["10"], (960, 480, 1), [f1, f2[:5000]])
    check(len([e for e in env.log if e[0] == "draw"]) == 1, "a frame cut short by the end of stdin is not drawn")

    env = run(["0"], (960, 480, 1), [f1])
    check(env.log == [("overscan", 0, 0, 0, 0, 0)] and env.out == ["off"],
          "\"0\": overscan reset to nothing, prints \"off\" (no band)")
    check(env.stdin.reads == 0 and env.sc is None, "\"0\": no session, no layer, stdin left alone")

    # the ring (the Echo Spot's round 480x480 screen): the constructor, "ring" after it; no overscan, a clear layer over the
    # whole screen, "ready <w> <h>" of the screen, then the table (3 ints, pairs) and frames of the strip's size, each
    # drawn as the layer with the strip's pixels where the table puts them and the rest left as it was
    # and its messages after the table: 5 ints (kind, a, b, c, n) then n bytes.  0: a strip frame; 1: the text cleared; 2: text
    # (the payload) centred at x = w / 2, baseline a, size b, colour c, into a bitmap under the ring (the fake Canvas
    # "draws" one pixel of the colour at that place)
    sw, sr, pairs = 5, 2, [(7, 3), (480 * 479 + 1, 9), (0, 0), (480 * 240 + 240, 5)]
    table = struct.pack("<3I", sw, sr, len(pairs)) + b"".join(struct.pack("<2I", a, b) for a, b in pairs)
    msg = lambda kind, a=0, b=0, c=0, payload=b"": struct.pack("<5i", kind, a, b, c, len(payload)) + payload
    s1, sp1 = frame(sw, sr, 3)
    s2, sp2 = frame(sw, sr, 4)
    colour = i32(0xFFF2F2F2)
    text = msg(2, 278, 144, colour, "12:34".encode())
    stream = table + msg(0, payload=s1) + text + msg(0, payload=s2) + msg(1)
    env = run(["10", "ctor", "ring"], (480, 480, 0), [stream[:7], stream[7:60], stream[60:]])
    log = env.log
    check(log[0] == ("overscan", 0, 0, 0, 0, 0), "ring: the overscan set to nothing (a band's taken back)")
    check(("surfacecontrol", "hassmic-band", 480, 480, -3, 4) in log,
          "ring: a 480x480 layer, PixelFormat.TRANSLUCENT, created HIDDEN")
    setup = [e for e in log if e[0] in ("layer", "position")]
    check(setup == [("layer", 2000000000), ("position", 0.0, 0.0)], "ring: the layer over the whole screen, at (0, 0)")
    check(env.out == ["ready 480 480"], "ring: prints \"ready 480 480\", the screen's size")
    check(("bitmap", 480, 480) in log and ("paint", 3) in log and ("canvas",) in log, "ring: a 480x480 text bitmap, its canvas, a paint antialiased and filtering (3)")
    draws = [e for e in log if e[0] == "draw"]
    want = [0] * (480 * 480)
    for a, b in pairs: want[a] = sp1[b]
    shape = lambda px: ("draw", px, 0, 480, 0, 0, 480, 480, True, None)
    check(len(draws) == 4 and draws[0] == shape(want),
          "ring frame 1 (the table and the frame split across reads): the strip's pixels where the table says, the rest 0, "
          "480x480 with alpha")
    check(("text", "12:34", 240.0, 278.0, 144.0, colour, "CENTER") in log, "the text: centred at x = 240, baseline 278, size 144, the colour, Paint.Align.CENTER")
    want[278 * 480 + 240] = colour
    check(len(draws) == 4 and draws[1] == shape(want), "after the text: the bitmap's pixels under the ring, the ring on top")
    for a, b in pairs: want[a] = sp2[b]
    check(len(draws) == 4 and draws[2] == shape(want), "ring frame 2 over the text")
    want[278 * 480 + 240] = 0
    check(len(draws) == 4 and draws[3] == shape(want), "the text cleared: the layer clear but for the ring")
    seq = [e[0] for e in log if e[0] in ("lock", "wipe", "draw", "post")]
    check(seq == ["lock", "wipe", "draw", "post"] * 4 and all(e[1:] == (0, "CLEAR") for e in log if e[0] == "wipe"),
          "ring: every frame lockCanvas, drawColor(0, PorterDuff.Mode.CLEAR), drawBitmap, unlockCanvasAndPost")
    check(env.locked is None, "ring: returns at the end of stdin, nothing left locked")

    # an image (kind 3): decoded by BitmapFactory, drawn as a square of b px with its top at a, centred, through the paint
    # (ANTI_ALIAS | FILTER_BITMAP); one that does not decode is left out, and the program goes on
    img, bad = bytes([0x2a]) + b"rest of a picture", bytes([0]) + b"not a picture"
    stream = table + msg(3, 60, 210, 0, img) + msg(3, 60, 210, 0, bad) + msg(0, payload=s1)
    env = run(["10", "ctor", "ring"], (480, 480, 0), [stream])
    shown = [e for e in env.log if e[0] == "image"]
    check(shown == [("image", img, (135, 60, 345, 270), 3)], f"an image: decoded, 210 px square at (135, 60), filtered paint ({shown})")
    draws = [e for e in env.log if e[0] == "draw"]
    want = [0] * (480 * 480)
    want[60 * 480 + 135] = i32(0xff00002a)
    for a, b in pairs: want[a] = sp1[b]
    check(len(draws) == 3 and draws[-1] == shape(want), "the image under the ring; one that does not decode: nothing drawn, no stop")

    env = run(["10", "ctor", "ring"], (480, 480, 0), [table[:10]])
    check(env.out == ["ready 480 480"] and not [e for e in env.log if e[0] == "draw"],
          "ring: stdin ending inside the table: nothing drawn, the program ends")
    env = run(["10", "ctor", "ring"], (480, 480, 0), [table + msg(0, payload=s1)[:30]])
    check(not [e for e in env.log if e[0] == "draw"], "ring: stdin ending inside a frame: nothing drawn")

    env = run(["10", "builder"], (960, 480, 1), [f1], api9=True)
    check(("surfacecontrol", "hassmic-band", 960, 10, -1, 4) in env.log and env.out == ["ready 960 10"],
          "\"builder\" without \"ring\": the band as before, through SurfaceControl.Builder")

    env = run(["10"], (480, 960, 0), [])
    check(env.log[0] == ("overscan", 0, 0, 0, 0, 10), "rotation 0: the band in the bottom overscan")
    check(("surfacecontrol", "hassmic-band", 480, 10, -1, 4) in env.log and ("position", 0.0, 950.0) in env.log
          and env.out == ["ready 480 10"], "rotation 0: 480x10 at (0, 950)")
    for rot, side in ((2, (0, 24, 0, 0)), (3, (0, 0, 24, 0))):     # left, top, right, bottom
        env = run(["24"], (960, 480, rot), [])
        check(env.log[0] == ("overscan", 0) + side, f"rotation {rot}: overscan {side}")

    # Android 9 (athena): hassmic passes "builder" as a second argument and the layer comes out of SurfaceControl.Builder,
    # everything else as above
    env = run(["10", "builder"], (1280, 800, 0), [f1[:0] + frame(1280, 10, 3)[0]], api9=True)
    check(env.log[0] == ("overscan", 0, 0, 0, 0, 10), "builder: the same overscan")
    check(("surfacecontrol", "hassmic-band", 1280, 10, -1, 4) in env.log and ("position", 0.0, 790.0) in env.log
          and env.out == ["ready 1280 10"], "builder: 1280x10, OPAQUE, HIDDEN, at (0, 790)")
    check([e[0] for e in env.log if e[0] in ("lock", "draw", "post")] == ["lock", "draw", "post"],
          "builder: the frame is drawn")
    env = run(["0", "builder"], (1280, 800, 0), [], api9=True)
    check(env.log == [("overscan", 0, 0, 0, 0, 0)] and env.out == ["off"], "builder: \"0\" resets the overscan, no layer")

    check.done("ringbar.dex: overscan, layer and frames as expected")


if __name__ == "__main__":
    main()
