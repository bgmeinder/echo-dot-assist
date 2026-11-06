#!/usr/bin/env python3
"""Assembles scripts/device/ringbar.dex, checked into the repository, from nothing but this file and
tools/dexasm.py: the light ring of the Echo Show (checkers), shown as a band along the bottom of the screen.
The Echo Show has no ring of LEDs; hassmic draws the ring "linearised" (LED by LED, left to right) and streams
the frames to this program, which only puts them on the screen: every animation is hassmic's.  It runs, as
hassmic's own child (uid system, SELinux su), as

    CLASSPATH=/data/local/hassmic/ringbar.dex app_process / Ringbar <band rows>

How it gets the band to itself:
  - the window manager's overscan (IWindowManager.setOverscan, what `wm overscan` does): every app, the home
    screen included, is laid out above the band, so nothing of theirs is under it.  Overscan is given in the
    panel's natural orientation; PhoneWindowManager.beginLayoutLw (AOSP 7) turns it by the rotation, so the side that is
    at the bottom now is: rotation 0 bottom, 1 left, 2 top, 3 right.  The Echo Show 5 has a 480x960 panel shown
    as 960x480, rotation 1: its natural left edge is the bottom.  The window manager keeps the setting in
    /data/system/display_settings.xml, so it outlives the process: `Ringbar 0` puts it back to nothing.
  - a SurfaceFlinger layer of its own (SurfaceControl, as the boot animation and the window manager make
    theirs), opaque, at layer 2000000000: the window manager puts windows at their type's layer x 10000 + 1000
    (a few hundred thousand), the boot animation sits at 2^30.  Placed at the bottom of the logical display:
    layer positions are in the rotated frame, overscan is not.

The protocol, which hassmic's C side relies on:
    argv[1]         the band's height in rows; 0 = overscan reset, no band
    argv[2]         "builder": SurfaceControl.Builder (Android 8+), "ctor" or nothing: the constructor
    argv[3]         "ring": the ring mode below
    stdout          one line first: "ready <width> <rows>" (then the frames are wanted), or "off" for 0
    stdin           frames of width * rows * 4 bytes: each pixel 0xAARRGGBB as a little-endian int, rows top
                    to bottom; a frame is drawn once it is whole.  End of stdin (hassmic closed it or died):
                    the program ends and its layer goes with it
The ring mode (the Echo Spot's round screen, board.screen_round): no overscan (a band's is taken back), a
TRANSLUCENT layer over the whole screen, "ready <screen width> <screen height>", then on stdin a table once: 3
little-endian ints (the strip's width and rows, the number of pairs) and that many (layer pixel, strip pixel)
int pairs; then frames of the strip's size, each copied into the layer by the table (the rest stays clear) and
drawn whole with alpha.  The strip and the table are hassmic's (ring_band.c send_table: the rim's annulus, the
strip's middle at the bottom); the program knows nothing of circles either.

Reflection only, as Wifictl does (Class.forName/getMethod/getConstructor/getField): the dex references java.*
classes alone.  Ints and objects only (floats as boxed Float, made from strings), no try/catch: whatever
throws ends the process, and hassmic sees its stdout close.  The Java source it stands for:

    public class Ringbar {
        public static void main(String[] a) {
            int h = Integer.parseInt(a[0]);
            Object wm = sinv("android.view.IWindowManager$Stub", "asInterface", "android.os.IBinder",
                             sinv("android.os.ServiceManager", "getService", "java.lang.String", "window"));
            Object di = w1(sinv0("android.hardware.display.DisplayManagerGlobal", "getInstance"),
                           "getDisplayInfo", "int", Integer.valueOf(0));
            int w = fint(di, "logicalWidth"), dh = fint(di, "logicalHeight");
            overscan(wm, fint(di, "rotation"), h);
            if (h == 0) { out("off"); return; }                 // the band switched off: overscan reset, done
            Object s = surface(w, h, dh - h);
            out(new StringBuilder().append("ready ").append(w).append(" ").append(h).toString());
            loop(s, w, h);
        }
        static void overscan(Object wm, int rot, int h) {     // (display 0, left, top, right, bottom)
            String[] ps = new String[5]; Object[] as = new Object[5];
            for (int i = 0; i < 5; i++) { ps[i] = "int"; as[i] = Integer.valueOf(0); }
            as[rot == 0 ? 4 : rot] = Integer.valueOf(h);      // the natural side at the bottom now
            inv(wm, "setOverscan", ps, as);
        }
        static Object surface(int w, int h, int y) {
            Class scc = Class.forName("android.view.SurfaceControl");
            Class ssc = Class.forName("android.view.SurfaceSession");
            Object session = ssc.newInstance();
            Class[] cs = { ssc, Class.forName("java.lang.String"),
                           Integer.TYPE, Integer.TYPE, Integer.TYPE, Integer.TYPE };
            Object[] as = { session, "hassmic-band", Integer.valueOf(w), Integer.valueOf(h),
                            Integer.valueOf(-1) /* PixelFormat.OPAQUE */, Integer.valueOf(4) /* HIDDEN */ };
            Object sc = scc.getConstructor(cs).newInstance(as);
            sinv0("android.view.SurfaceControl", "openTransaction");
            w1(sc, "setLayer", "int", Integer.valueOf(2000000000));        // above every window
            w2(sc, "setPosition", "float", "float", Float.valueOf("0"),
               Float.valueOf(String.valueOf(Integer.valueOf(y))));
            os(sc, "show");
            sinv0("android.view.SurfaceControl", "closeTransaction");
            Object s = Class.forName("android.view.Surface").newInstance();
            w1(s, "copyFrom", "android.view.SurfaceControl", sc);
            return s;
        }
        static void loop(Object s, int w, int h) {
            Method lock = meth("android.view.Surface", "lockCanvas", new String[]{ "android.graphics.Rect" });
            Method post = meth("android.view.Surface", "unlockCanvasAndPost",
                               new String[]{ "android.graphics.Canvas" });
            Method draw = meth("android.graphics.Canvas", "drawBitmap", new String[]{ "[I", "int", "int", "int",
                               "int", "int", "int", "boolean", "android.graphics.Paint" });
            InputStream in = System.in;
            int[] px = new int[w * h]; byte[] buf = new byte[w * h * 4];
            Object[] lockArgs = new Object[1];                  // { null }: the whole surface
            Object[] drawArgs = { px, Integer.valueOf(0), Integer.valueOf(w), Integer.valueOf(0), Integer.valueOf(0),
                                  Integer.valueOf(w), Integer.valueOf(h), Boolean.FALSE, null };
            while (full(in, buf)) {
                ByteBuffer.wrap(buf).order(ByteOrder.LITTLE_ENDIAN).asIntBuffer().get(px);   // ARGB, row by row
                Object c = lock.invoke(s, lockArgs);
                draw.invoke(c, drawArgs);                       // offset 0, stride w, at 0,0, w x h, no alpha
                post.invoke(s, new Object[]{ c });
            }                                                    // end of stdin: the layer goes with the process
        }
        static boolean full(InputStream in, byte[] buf) {     // a whole frame, or false at the end
            for (int got = 0; got < buf.length; ) {
                int n = in.read(buf, got, buf.length - got);
                if (n < 0) return false;
                got += n;
            }
            return true;
        }
        static Method meth(String c, String n, String[] ps) {
            Class[] cs = new Class[ps.length];
            for (int i = 0; i < ps.length; i++) cs[i] = cls(ps[i]);
            return Class.forName(c).getMethod(n, cs);
        }
        static Class cls(String n) {             // Class.forName knows no primitive types ("[I" it does)
            if (n.equals("int")) return Integer.TYPE;
            if (n.equals("boolean")) return Boolean.TYPE;
            if (n.equals("float")) return Float.TYPE;
            return Class.forName(n);
        }
        static int fint(Object o, String f) { return ((Integer) o.getClass().getField(f).get(o)).intValue(); }
        static void out(String s) { System.out.println(s); System.out.flush(); }
        static Object sinv0(String c, String n) {                          // a static method, no arguments
            return Class.forName(c).getMethod(n, new Class[0]).invoke(null, new Object[0]);
        }
        // inv, sinv, w1, w2, os: as Wifictl's
    }

python3 tools/mkringbar.py            rebuild scripts/device/ringbar.dex
python3 tools/mkringbar.py --check    only re-read and check the one in the tree
    --framework=firmware/checkers/rootfs   also against the firmware's boot class path (constructors,
                                           methods, fields, PixelFormat.OPAQUE, SurfaceControl.HIDDEN)

What the code computes is tests/unit/ringbar_run.py's: it interprets the dex against fakes of the window
manager, the display and SurfaceControl/Surface/Canvas (make unit).  None of this has run on an Echo."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dexasm import (Code, Program, AGET_OBJ, APUT_OBJ, VIRT, DIRECT, STATIC, STATIC_RANGE,  # noqa: E402
                    ADD8, MUL8, USHR8, SUB, MUL, ADD, IFNE, IFGE, IFLT, IFEQZ, IFNEZ, IFLTZ,
                    SB, STR, CLS, OBJ, MTH, CTOR, INTG, ISTRM, CLSARR, STRARR, OBJARR, BYTES, INTS)
import dexasm                                                  # noqa: E402

ME = "LRingbar;"
OPAQUE, HIDDEN = -1, 4                    # PixelFormat.OPAQUE, SurfaceControl.HIDDEN (--framework checks both)
TRANSLUCENT = -3                          # PixelFormat.TRANSLUCENT: the ring's layer, clear but for the rim
AGET, APUT = 0x44, 0x4b                   # aget / aput of ints (dexasm has the object and byte ones)
LAYER = 2000000000                        # above every window (type layer x 10000 + 1000) and the boot animation (2^30)
NAME = "hassmic-band"                     # the layer's name in dumpsys SurfaceFlinger

# the class's own methods: (name, params, return)
SELF = [
    ("main",  (STRARR,), "V"),
    ("overscan", (OBJ, "I", "I"), "V"),
    ("create", ("I", "I", "I", "I"), OBJ),
    ("surface", (OBJ, "I"), OBJ),
    ("meths", (), OBJARR),
    ("loop",  (OBJ, "I", "I", "I", "I", INTS), "V"),
    ("table", (INTS,), INTS),
    ("map",   (INTS, INTS, INTS), "V"),
    ("ring",  (OBJ, "I", "I", "I", "I", INTS), "V"),
    ("setup", ("I", "I"), OBJARR),
    ("callm", (OBJARR, "I", OBJ, OBJARR), OBJ),
    ("fbox",  ("I",), OBJ),
    ("clear", (OBJARR,), "V"),
    ("textm", (OBJARR, STR, INTS, "I"), "V"),
    ("pixels", (OBJARR, INTS, "I", "I"), "V"),
    ("wipe",  (OBJARR, OBJ), "V"),
    ("payload", (OBJARR, ISTRM, INTS, "I"), "Z"),
    ("imagem", (OBJARR, BYTES, INTS, "I"), "V"),
    ("full",  (ISTRM, BYTES), "Z"),
    ("meth",  (STR, STR, STRARR), MTH),
    ("cls",   (STR,), CLS),
    ("inv",   (OBJ, STR, STRARR, OBJARR), OBJ),
    ("sinv",  (STR, STR, STR, OBJ), OBJ),
    ("sinv0", (STR, STR), OBJ),
    ("w1",    (OBJ, STR, STR, OBJ), OBJ),
    ("w2",    (OBJ, STR, STR, STR, OBJ, OBJ), OBJ),
    ("os",    (OBJ, STR), OBJ),
    ("fint",  (OBJ, STR), "I"),
    ("out",   (STR,), "V"),
]
DRAW = ("[I", "int", "int", "int", "int", "int", "int", "boolean", "android.graphics.Paint")


# ---------------------------------------------------------------- the methods, as bytecode
def emit():
    m = []

    def main():
        # a=0 h=1 t=2..5 wm=6 di=7 w=8 dh=9 rot=10 s=11 n=12 b=13 rg=14 hdr=15
        c = Code(16, 1)
        c.aget_at(2, 0, 0)
        c.invoke(STATIC, "int.parse", [2]); c.move_result(1)
        c.const_string(2, "android.os.ServiceManager"); c.const_string(3, "getService")
        c.const_string(4, "java.lang.String"); c.const_string(5, "window")
        c.invoke(STATIC, "sinv", [2, 3, 4, 5]); c.move_result(5, True)
        c.const_string(2, "android.view.IWindowManager$Stub"); c.const_string(3, "asInterface")
        c.const_string(4, "android.os.IBinder")
        c.invoke(STATIC, "sinv", [2, 3, 4, 5]); c.move_result(6, True)
        c.const_string(2, "android.hardware.display.DisplayManagerGlobal"); c.const_string(3, "getInstance")
        c.invoke(STATIC, "sinv0", [2, 3]); c.move_result(2, True)
        c.const_string(3, "getDisplayInfo"); c.const_string(4, "int")
        c.const_4(5, 0); c.invoke(STATIC, "int.value", [5]); c.move_result(5, True)     # Display.DEFAULT_DISPLAY
        c.invoke(STATIC, "w1", [2, 3, 4, 5]); c.move_result(7, True)
        for f, v in (("logicalWidth", 8), ("logicalHeight", 9), ("rotation", 10)):
            c.const_string(2, f); c.invoke(STATIC, "fint", [7, 2]); c.move_result(v)
        # the flags after the rows: argv[1] "builder" = SurfaceControl.Builder (Android 8+; "ctor" or nothing: the
        # constructor), argv[2] "ring" = the ring mode (a round screen)
        c.array_length(12, 0)
        c.const_4(13, 0); c.const_4(14, 0)
        c.const_4(2, 2); c.if2(IFLT, 12, 2, "flags")
        c.aget_at(2, 0, 1); c.const_string(3, "builder"); c.invoke(VIRT, "str.eq", [2, 3]); c.move_result(2)
        c.ifz(IFEQZ, 2, "ctor"); c.const_4(13, 2)
        c.label("ctor")
        c.const_4(2, 3); c.if2(IFLT, 12, 2, "flags")
        c.aget_at(2, 0, 2); c.const_string(3, "ring"); c.invoke(VIRT, "str.eq", [2, 3]); c.move_result(14)
        c.label("flags")
        c.ifz(IFNEZ, 14, "ring")
        c.invoke(STATIC, "overscan", [6, 10, 1])
        c.ifz(IFNEZ, 1, "on")
        c.const_string(2, "off"); c.invoke(STATIC, "out", [2])
        c.ret_void()
        c.label("on")
        c.binop(SUB, 9, 9, 1)                                 # y = dh - h: the band's top row
        c.lit(2, OPAQUE)
        c.invoke(STATIC, "create", [8, 1, 13, 2]); c.move_result(11, True)    # the SurfaceControl, which must outlive the loop:
        c.invoke(STATIC, "surface", [11, 9]); c.move_result(12, True)         # its finalizer releases the layer (Android 9: a GC
                                                                             # every ~55 s took the band away, 2026-10-14)
        ready(c, 8, 1)
        c.move(2, 12, True); c.move(3, 8); c.move(4, 1); c.move(5, 8); c.move(6, 1); c.const_4(7, 0)   # loop(s, w, h, w, h, null)
        c.invoke_range(STATIC_RANGE, "loop", 2, 6)
        c.goto_("end")
        # the ring: no overscan (a band's, if any, taken back), a clear layer over the whole screen, the table first
        c.label("ring")
        c.const_4(2, 0); c.invoke(STATIC, "overscan", [6, 10, 2])
        c.lit(2, TRANSLUCENT)
        c.invoke(STATIC, "create", [8, 9, 13, 2]); c.move_result(11, True)
        c.const_4(2, 0); c.invoke(STATIC, "surface", [11, 2]); c.move_result(12, True)
        ready(c, 8, 9)
        c.const_4(2, 3); c.new_array(15, 2, INTS)
        c.invoke(STATIC, "table", [15]); c.move_result(7, True)
        c.ifz(IFEQZ, 7, "end")                                # stdin ended before the table came
        c.move(2, 12, True); c.move(3, 8); c.move(4, 9)
        c.const_4(5, 0); c.aget(5, 15, 5, AGET); c.const_4(6, 1); c.aget(6, 15, 6, AGET)   # the strip's width and rows
        c.invoke_range(STATIC_RANGE, "ring", 2, 6)            # ring(s, w, dh, sw, sr, tab)
        c.label("end")
        c.invoke(VIRT, "obj.getcls", [11]); c.move_result(2, True)         # a use of it after the loop: live all along
        c.ret_void()
        m.append(("main", c))

    def ready(c, w, h):                                       # out("ready <w> <h>"), v2 and v3 scratch
        c.new_instance(2, SB); c.invoke(DIRECT, "SB.new", [2])
        c.const_string(3, "ready "); c.invoke(VIRT, "SB.appendO", [2, 3])
        c.invoke(VIRT, "SB.appendI", [2, w])
        c.const_string(3, " "); c.invoke(VIRT, "SB.appendO", [2, 3])
        c.invoke(VIRT, "SB.appendI", [2, h])
        c.invoke(VIRT, "SB.to", [2]); c.move_result(2, True)
        c.invoke(STATIC, "out", [2])

    def overscan():
        # wm=0 rot=1 h=2 ps=3 as=4 i=5 t=6 z=7 n=8
        c = Code(9, 3)
        c.const_4(8, 5); c.new_array(3, 8, STRARR); c.new_array(4, 8, OBJARR)
        c.const_string(6, "int")
        c.const_4(7, 0); c.invoke(STATIC, "int.value", [7]); c.move_result(7, True)
        c.const_4(5, 0)
        c.label("loop")
        c.if2(IFGE, 5, 8, "done")
        c.aput(6, 3, 5, APUT_OBJ); c.aput(7, 4, 5, APUT_OBJ)
        c.binop_lit8(ADD8, 5, 5, 1)
        c.goto_("loop")
        c.label("done")
        c.move(5, 1)                                          # the index of the natural side at the bottom:
        c.ifz(IFNEZ, 5, "side")                               # left 1, top 2, right 3 are the rotation itself,
        c.const_4(5, 4)                                       # bottom 4 for rotation 0
        c.label("side")
        c.invoke(STATIC, "int.value", [2]); c.move_result(7, True)
        c.aput(7, 4, 5, APUT_OBJ)
        c.const_string(6, "setOverscan")
        c.invoke(STATIC, "inv", [0, 6, 3, 4])
        c.ret_void()
        m.append(("overscan", c))

    def create():
        # w=0 h=1 n=2 fmt=3 scc=4 ssc=5 session=6 sc=7 t=8 cs=9 as=10 k=11
        c = Code(12, 4)
        c.const_string(8, "android.view.SurfaceControl")
        c.invoke(STATIC, "cls.forName", [8]); c.move_result(4, True)
        c.const_string(8, "android.view.SurfaceSession")
        c.invoke(STATIC, "cls.forName", [8]); c.move_result(5, True)
        c.invoke(VIRT, "cls.newi", [5]); c.move_result(6, True)
        c.const_4(8, 2)
        c.if2(IFGE, 2, 8, "builder")
        # Android 7: new SurfaceControl(session, name, w, h, format, flags)
        c.const_4(11, 6); c.new_array(9, 11, CLSARR); c.new_array(10, 11, OBJARR)
        c.const_4(11, 0); c.aput(5, 9, 11, APUT_OBJ)
        c.const_string(8, "java.lang.String"); c.invoke(STATIC, "cls.forName", [8]); c.move_result(8, True)
        c.const_4(11, 1); c.aput(8, 9, 11, APUT_OBJ)
        c.sget_obj(8, "int.TYPE")
        for k in (2, 3, 4, 5):
            c.const_4(11, k); c.aput(8, 9, 11, APUT_OBJ)
        c.const_4(11, 0); c.aput(6, 10, 11, APUT_OBJ)
        c.const_string(8, NAME); c.const_4(11, 1); c.aput(8, 10, 11, APUT_OBJ)
        for k, v in ((2, 0), (3, 1), (4, 3)):                 # width, height, format: the parameters
            c.invoke(STATIC, "int.value", [v]); c.move_result(8, True)
            c.const_4(11, k); c.aput(8, 10, 11, APUT_OBJ)
        c.lit(8, HIDDEN); c.invoke(STATIC, "int.value", [8]); c.move_result(8, True)     # flags
        c.const_4(11, 5); c.aput(8, 10, 11, APUT_OBJ)
        c.invoke(VIRT, "cls.getc", [4, 9]); c.move_result(8, True)
        c.invoke(VIRT, "ctor.newi", [8, 10]); c.move_result(7, True)
        c.ret(7, True)
        # Android 9: the constructor is gone; new SurfaceControl.Builder(session).setName().setSize().setFormat()
        # .setFlags().build() (each setter returns the builder)
        c.label("builder")
        c.const_string(8, "android.view.SurfaceControl$Builder")
        c.invoke(STATIC, "cls.forName", [8]); c.move_result(4, True)      # scc: the Builder class from here on
        c.const_4(11, 1); c.new_array(9, 11, CLSARR); c.new_array(10, 11, OBJARR)
        c.const_4(11, 0); c.aput(5, 9, 11, APUT_OBJ); c.aput(6, 10, 11, APUT_OBJ)
        c.invoke(VIRT, "cls.getc", [4, 9]); c.move_result(8, True)
        c.invoke(VIRT, "ctor.newi", [8, 10]); c.move_result(7, True)      # sc: the builder
        c.const_string(8, "setName"); c.const_string(9, "java.lang.String"); c.const_string(10, NAME)
        c.invoke(STATIC, "w1", [7, 8, 9, 10]); c.move_result(7, True)
        c.invoke(STATIC, "int.value", [0]); c.move_result(10, True)      # w2(builder, "setSize", "int", "int", w, h):
        c.invoke(STATIC, "int.value", [1]); c.move_result(11, True)      # six consecutive registers, 6..11
        c.move(6, 7, True)
        c.const_string(7, "setSize"); c.const_string(8, "int"); c.const_string(9, "int")
        c.invoke_range(STATIC_RANGE, "w2", 6, 6); c.move_result(7, True)
        c.const_string(8, "setFormat"); c.const_string(9, "int")
        c.invoke(STATIC, "int.value", [3]); c.move_result(10, True)
        c.invoke(STATIC, "w1", [7, 8, 9, 10]); c.move_result(7, True)
        c.const_string(8, "setFlags"); c.const_string(9, "int")
        c.lit(10, HIDDEN); c.invoke(STATIC, "int.value", [10]); c.move_result(10, True)
        c.invoke(STATIC, "w1", [7, 8, 9, 10]); c.move_result(7, True)
        c.const_string(8, "build"); c.invoke(STATIC, "os", [7, 8]); c.move_result(7, True)
        c.ret(7, True)
        m.append(("create", c))

    def surface():
        # sc=0 y=1 t=2 cs=3 as=4 s=5; w2's arguments 6..11
        c = Code(12, 2)
        c.const_string(2, "android.view.SurfaceControl")
        c.const_string(3, "openTransaction"); c.invoke(STATIC, "sinv0", [2, 3])
        c.const_string(3, "setLayer"); c.const_string(4, "int")
        c.lit(5, LAYER); c.invoke(STATIC, "int.value", [5]); c.move_result(5, True)
        c.invoke(STATIC, "w1", [0, 3, 4, 5])
        c.move(6, 0, True); c.const_string(7, "setPosition")
        c.const_string(8, "float"); c.const_string(9, "float")
        c.const_string(10, "0"); c.invoke(STATIC, "float.value", [10]); c.move_result(10, True)
        c.invoke(STATIC, "int.value", [1]); c.move_result(11, True)
        c.invoke(STATIC, "str.valueO", [11]); c.move_result(11, True)
        c.invoke(STATIC, "float.value", [11]); c.move_result(11, True)
        c.invoke_range(STATIC_RANGE, "w2", 6, 6)
        c.const_string(3, "show"); c.invoke(STATIC, "os", [0, 3])
        c.const_string(3, "closeTransaction"); c.invoke(STATIC, "sinv0", [2, 3])
        c.const_string(3, "android.view.Surface")
        c.invoke(STATIC, "cls.forName", [3]); c.move_result(3, True)
        c.invoke(VIRT, "cls.newi", [3]); c.move_result(3, True)
        c.const_string(4, "copyFrom"); c.const_string(5, "android.view.SurfaceControl")
        c.invoke(STATIC, "w1", [3, 4, 5, 0])
        c.ret(3, True)
        m.append(("surface", c))

    def meths():                                              # { lockCanvas, unlockCanvasAndPost, drawBitmap }
        # ms=0 strs=1 k=2 t=3
        c = Code(4, 0)
        c.const_4(2, 3); c.new_array(0, 2, OBJARR)

        def meth(at, cls, name, ps):
            c.lit(2, len(ps)); c.new_array(1, 2, STRARR)
            for i, p in enumerate(ps):
                c.const_string(3, p); c.lit(2, i); c.aput(3, 1, 2, APUT_OBJ)
            c.const_string(2, cls); c.const_string(3, name)
            c.invoke(STATIC, "meth", [2, 3, 1]); c.move_result(3, True)
            c.const_4(2, at); c.aput(3, 0, 2, APUT_OBJ)
        meth(0, "android.view.Surface", "lockCanvas", ("android.graphics.Rect",))
        meth(1, "android.view.Surface", "unlockCanvasAndPost", ("android.graphics.Canvas",))
        meth(2, "android.graphics.Canvas", "drawBitmap", DRAW)
        c.ret(0, True)
        m.append(("meths", c))

    def loop():
        # The frames: sw x sr pixels each (the band: the layer's own w x h; the ring: the strip's), drawn as they come.
        # tab (the ring) maps strip pixels into the w x h layer, which starts clear and keeps what is not mapped; null:
        # the band, every frame is the layer whole.
        # s=0 w=1 h=2 sw=3 sr=4 tab=5 ms=6 in=7 buf=8 px=9 strip=10 la=11 da=12 c=13 t=14 k=15
        c = Code(16, 6)
        c.invoke(STATIC, "meths", []); c.move_result(6, True)
        c.sget_obj(7, "sys.in")
        c.binop(MUL, 15, 1, 2)                                # w * h pixels: the layer
        c.new_array(9, 15, INTS)
        c.binop(MUL, 15, 3, 4)                                # sw * sr: a frame
        c.new_array(10, 15, INTS)
        c.binop_lit8(MUL8, 15, 15, 4)                         # 4 bytes each
        c.new_array(8, 15, BYTES)
        c.ifz(IFNEZ, 5, "mapped")
        c.move(10, 9, True)                                   # the band: a frame is the layer itself
        c.label("mapped")
        c.const_4(15, 1); c.new_array(11, 15, OBJARR)         # { null }: lockCanvas(null), the whole surface
        c.lit(15, len(DRAW)); c.new_array(12, 15, OBJARR)     # drawArgs; [8], the Paint, stays null
        c.const_4(15, 0); c.aput(9, 12, 15, APUT_OBJ)         # colors
        c.const_4(14, 0); c.invoke(STATIC, "int.value", [14]); c.move_result(14, True)
        for k in (1, 3, 4):                                   # offset, x, y
            c.const_4(15, k); c.aput(14, 12, 15, APUT_OBJ)
        c.invoke(STATIC, "int.value", [1]); c.move_result(14, True)
        for k in (2, 5):                                      # stride, width
            c.const_4(15, k); c.aput(14, 12, 15, APUT_OBJ)
        c.invoke(STATIC, "int.value", [2]); c.move_result(14, True)
        c.const_4(15, 6); c.aput(14, 12, 15, APUT_OBJ)        # height
        c.sget_obj(14, "bool.F")                              # hasAlpha: the band is opaque, the ring is clear around
        c.ifz(IFEQZ, 5, "opaque"); c.sget_obj(14, "bool.T")
        c.label("opaque")
        c.const_4(15, 7); c.aput(14, 12, 15, APUT_OBJ)
        c.label("frame")
        c.invoke(STATIC, "full", [7, 8]); c.move_result(14)
        c.ifz(IFNEZ, 14, "draw")
        c.ret_void()                                          # end of stdin
        c.label("draw")
        c.invoke(STATIC, "bb.wrap", [8]); c.move_result(14, True)
        c.sget_obj(15, "order.LE"); c.invoke(VIRT, "bb.order", [14, 15]); c.move_result(14, True)
        c.invoke(VIRT, "bb.asint", [14]); c.move_result(14, True)
        c.invoke(VIRT, "ib.get", [14, 10])
        c.ifz(IFEQZ, 5, "paint")
        c.invoke(STATIC, "map", [9, 10, 5])
        c.label("paint")
        # each Method out of the Object[] with a check-cast: ART's verifier wants the receiver typed (5.1, 2026-11-05)
        c.aget_at(14, 6, 0); c.check_cast(14, MTH); c.invoke(VIRT, "m.invoke", [14, 0, 11]); c.move_result(13, True)   # lockCanvas
        c.aget_at(14, 6, 2); c.check_cast(14, MTH); c.invoke(VIRT, "m.invoke", [14, 13, 12])                         # drawBitmap
        c.const_4(15, 1); c.new_array(15, 15, OBJARR); c.const_4(14, 0); c.aput(13, 15, 14, APUT_OBJ)
        c.aget_at(14, 6, 1); c.check_cast(14, MTH); c.invoke(VIRT, "m.invoke", [14, 0, 15])                          # unlockCanvasAndPost
        c.goto_("frame")
        m.append(("loop", c))

    def table():
        # The ring's table from stdin: 3 ints (the strip's width and rows, the number of pairs), then the pairs (layer
        # pixel, strip pixel), each int little-endian.  The header into hdr, the pairs returned; null at the end of stdin.
        # hdr=0 in=1 buf=2 t=3 n=4 tab=5 k=6
        c = Code(7, 1)
        c.sget_obj(1, "sys.in")
        c.lit(6, 12); c.new_array(2, 6, BYTES)
        c.invoke(STATIC, "full", [1, 2]); c.move_result(3)
        c.ifz(IFNEZ, 3, "hdr")
        c.const_4(5, 0); c.ret(5, True)
        c.label("hdr")
        c.invoke(STATIC, "bb.wrap", [2]); c.move_result(3, True)
        c.sget_obj(6, "order.LE"); c.invoke(VIRT, "bb.order", [3, 6]); c.move_result(3, True)
        c.invoke(VIRT, "bb.asint", [3]); c.move_result(3, True)
        c.invoke(VIRT, "ib.get", [3, 0])
        c.const_4(6, 2); c.aget(4, 0, 6, AGET)                # n = hdr[2]
        c.binop_lit8(MUL8, 6, 4, 8); c.new_array(2, 6, BYTES)
        c.invoke(STATIC, "full", [1, 2]); c.move_result(3)
        c.ifz(IFNEZ, 3, "pairs")
        c.const_4(5, 0); c.ret(5, True)
        c.label("pairs")
        c.binop_lit8(MUL8, 6, 4, 2); c.new_array(5, 6, INTS)
        c.invoke(STATIC, "bb.wrap", [2]); c.move_result(3, True)
        c.sget_obj(6, "order.LE"); c.invoke(VIRT, "bb.order", [3, 6]); c.move_result(3, True)
        c.invoke(VIRT, "bb.asint", [3]); c.move_result(3, True)
        c.invoke(VIRT, "ib.get", [3, 5])
        c.ret(5, True)
        m.append(("table", c))

    # ---- the ring mode's messages: a header of 5 little-endian ints (kind, a, b, c, n) then n payload bytes.
    # kind 0: a strip frame (n = sw * sr * 4), laid on the layer by the table; kind 1: the text cleared; kind 2: text
    # (the payload, UTF-8) drawn centred at x = w / 2, baseline a, size b px, colour c (ARGB), over what is there.  The
    # text lives in a Bitmap drawn by the framework's own Canvas and Paint (Roboto, antialiased); after a text
    # message the layer is that bitmap's pixels with the last strip laid on again, so the ring stays on top
    def ring():
        # s=0 w=1 h=2 sw=3 sr=4 tab=5 ms=6 in=7 buf=8 px=9 strip=10 la=11 da=12 c=13 t=14 k=15
        c = Code(16, 6)
        c.invoke(STATIC, "setup", [1, 2]); c.move_result(6, True)
        c.sget_obj(7, "sys.in")
        c.binop(MUL, 15, 1, 2); c.new_array(9, 15, INTS)      # the layer
        c.binop(MUL, 15, 3, 4); c.new_array(10, 15, INTS)     # the strip
        c.binop_lit8(MUL8, 15, 15, 4); c.new_array(8, 15, BYTES)
        c.const_4(15, 1); c.new_array(11, 15, OBJARR)         # { null }: lockCanvas(null)
        c.lit(15, len(DRAW)); c.new_array(12, 15, OBJARR)     # drawArgs, with alpha
        c.const_4(15, 0); c.aput(9, 12, 15, APUT_OBJ)
        c.const_4(14, 0); c.invoke(STATIC, "int.value", [14]); c.move_result(14, True)
        for k in (1, 3, 4):
            c.const_4(15, k); c.aput(14, 12, 15, APUT_OBJ)
        c.invoke(STATIC, "int.value", [1]); c.move_result(14, True)
        for k in (2, 5):
            c.const_4(15, k); c.aput(14, 12, 15, APUT_OBJ)
        c.invoke(STATIC, "int.value", [2]); c.move_result(14, True)
        c.const_4(15, 6); c.aput(14, 12, 15, APUT_OBJ)
        c.sget_obj(14, "bool.T"); c.const_4(15, 7); c.aput(14, 12, 15, APUT_OBJ)
        c.label("msg")
        c.lit(15, 8); c.aget(14, 6, 15, AGET_OBJ); c.check_cast(14, BYTES)          # the header's bytes
        c.invoke(STATIC, "full", [7, 14]); c.move_result(15)
        c.ifz(IFEQZ, 15, "end")
        c.invoke(STATIC, "bb.wrap", [14]); c.move_result(14, True)
        c.sget_obj(15, "order.LE"); c.invoke(VIRT, "bb.order", [14, 15]); c.move_result(14, True)
        c.invoke(VIRT, "bb.asint", [14]); c.move_result(14, True)
        c.lit(15, 9); c.aget(13, 6, 15, AGET_OBJ); c.check_cast(13, INTS)           # its ints
        c.invoke(VIRT, "ib.get", [14, 13])
        c.const_4(15, 0); c.aget(14, 13, 15, AGET)                                  # kind
        c.ifz(IFNEZ, 14, "text")
        c.invoke(STATIC, "full", [7, 8]); c.move_result(14)                         # a frame
        c.ifz(IFEQZ, 14, "end")
        c.invoke(STATIC, "bb.wrap", [8]); c.move_result(14, True)
        c.sget_obj(15, "order.LE"); c.invoke(VIRT, "bb.order", [14, 15]); c.move_result(14, True)
        c.invoke(VIRT, "bb.asint", [14]); c.move_result(14, True)
        c.invoke(VIRT, "ib.get", [14, 10])
        c.goto_("draw")
        c.label("text")
        c.const_4(15, 1); c.if2(IFNE, 14, 15, "t2")
        c.invoke(STATIC, "clear", [6])                        # kind 1
        c.goto_("redo")
        c.label("t2")
        c.invoke(STATIC, "payload", [6, 7, 13, 1]); c.move_result(14)             # kind 2 (text) or 3 (an image)
        c.ifz(IFEQZ, 14, "end")
        c.label("redo")
        c.invoke(STATIC, "pixels", [6, 9, 1, 2])              # the layer = the text bitmap's pixels
        c.label("draw")
        c.invoke(STATIC, "map", [9, 10, 5])                   # the ring on top
        c.aget_at(14, 6, 0); c.check_cast(14, MTH); c.invoke(VIRT, "m.invoke", [14, 0, 11]); c.move_result(13, True)   # lockCanvas
        c.invoke(STATIC, "wipe", [6, 13])                     # the buffer comes back with a frame of two or three ago in it
        c.aget_at(14, 6, 2); c.check_cast(14, MTH); c.invoke(VIRT, "m.invoke", [14, 13, 12])                         # drawBitmap
        c.const_4(15, 1); c.new_array(15, 15, OBJARR); c.const_4(14, 0); c.aput(13, 15, 14, APUT_OBJ)
        c.aget_at(14, 6, 1); c.check_cast(14, MTH); c.invoke(VIRT, "m.invoke", [14, 0, 15])                          # unlockCanvasAndPost
        c.goto_("msg")
        c.label("end")
        c.ret_void()
        m.append(("ring", c))

    def payload():                                            # the n bytes after the header: text (2) or an image (3)
        # ms=0 in=1 hdr=2 w=3 n=4 bytes=5 ok=6 k=7 s=8
        c = Code(9, 4)
        c.const_4(6, 4); c.aget(4, 2, 6, AGET)
        c.new_array(5, 4, BYTES)
        c.invoke(STATIC, "full", [1, 5]); c.move_result(6)
        c.ifz(IFNEZ, 6, "got")
        c.ret(6, False)                                       # stdin ended inside it
        c.label("got")
        c.const_4(6, 0); c.aget(7, 2, 6, AGET)
        c.const_4(6, 3); c.if2(IFNE, 7, 6, "text")
        c.invoke(STATIC, "imagem", [0, 5, 2, 3])
        c.const_4(6, 1); c.ret(6, False)
        c.label("text")
        c.new_instance(8, STR); c.invoke(DIRECT, "str.new_b", [8, 5])               # UTF-8, the platform's default
        c.invoke(STATIC, "textm", [0, 8, 2, 3])
        c.const_4(6, 1); c.ret(6, False)
        m.append(("payload", c))

    def imagem():
        # An encoded image (JPEG, PNG: whatever BitmapFactory decodes) drawn into the text bitmap as a square of b px,
        # its top at a, centred; filtered (the Paint has FILTER_BITMAP).  One that does not decode is left out.
        # ms=0 bytes=1 hdr=2 w=3 args=4 t=5 k=6 bm=7 x=8 y=9 sz=10 r=11 z=12
        c = Code(13, 4)
        c.const_4(6, 3); c.new_array(4, 6, OBJARR)
        c.const_4(6, 0); c.aput(1, 4, 6, APUT_OBJ)
        c.const_4(12, 0); c.invoke(STATIC, "int.value", [12]); c.move_result(5, True); c.const_4(6, 1); c.aput(5, 4, 6, APUT_OBJ)
        c.array_length(12, 1); c.invoke(STATIC, "int.value", [12]); c.move_result(5, True); c.const_4(6, 2); c.aput(5, 4, 6, APUT_OBJ)
        c.lit(6, 18); c.const_4(12, 0); c.invoke(STATIC, "callm", [0, 6, 12, 4]); c.move_result(7, True)   # decodeByteArray
        c.ifz(IFNEZ, 7, "decoded")
        c.ret_void()
        c.label("decoded")
        c.const_4(6, 1); c.aget(9, 2, 6, AGET); c.const_4(6, 2); c.aget(10, 2, 6, AGET)
        c.binop(SUB, 8, 3, 10); c.binop_lit8(USHR8, 8, 8, 1)  # x = (w - size) / 2
        c.const_4(6, 4); c.new_array(4, 6, OBJARR)
        c.invoke(STATIC, "int.value", [8]); c.move_result(5, True); c.const_4(6, 0); c.aput(5, 4, 6, APUT_OBJ)
        c.invoke(STATIC, "int.value", [9]); c.move_result(5, True); c.const_4(6, 1); c.aput(5, 4, 6, APUT_OBJ)
        c.binop(ADD, 12, 8, 10); c.invoke(STATIC, "int.value", [12]); c.move_result(5, True); c.const_4(6, 2); c.aput(5, 4, 6, APUT_OBJ)
        c.binop(ADD, 12, 9, 10); c.invoke(STATIC, "int.value", [12]); c.move_result(5, True); c.const_4(6, 3); c.aput(5, 4, 6, APUT_OBJ)
        c.lit(6, 19); c.aget(11, 0, 6, AGET_OBJ); c.check_cast(11, CTOR)
        c.invoke(VIRT, "ctor.newi", [11, 4]); c.move_result(11, True)                # new Rect(x, y, x + size, y + size)
        c.const_4(6, 4); c.new_array(4, 6, OBJARR)
        c.const_4(6, 0); c.aput(7, 4, 6, APUT_OBJ)            # the Bitmap; [1], the source Rect, stays null: all of it
        c.const_4(6, 2); c.aput(11, 4, 6, APUT_OBJ)
        c.const_4(6, 5); c.aget(5, 0, 6, AGET_OBJ); c.const_4(6, 3); c.aput(5, 4, 6, APUT_OBJ)       # the Paint
        c.const_4(6, 4); c.aget(5, 0, 6, AGET_OBJ)            # the text bitmap's Canvas
        c.lit(6, 20); c.invoke(STATIC, "callm", [0, 6, 5, 4])
        c.ret_void()
        m.append(("imagem", c))

    def wipe():                                               # canvas.drawColor(0, PorterDuff.Mode.CLEAR): the buffer cleared before the draw.  A
        # layer with alpha is blended over what the buffer holds, and SurfaceFlinger hands the buffers round: the frame
        # of two or three draws ago is in it (the Spot showed two minute digits at once, 2026-11-05)
        # ms=0 cv=1 args=2 t=3 k=4 z=5
        c = Code(6, 2)
        c.const_4(4, 2); c.new_array(2, 4, OBJARR)
        c.const_4(5, 0); c.invoke(STATIC, "int.value", [5]); c.move_result(3, True); c.const_4(4, 0); c.aput(3, 2, 4, APUT_OBJ)
        c.lit(4, 17); c.aget(3, 0, 4, AGET_OBJ); c.const_4(4, 1); c.aput(3, 2, 4, APUT_OBJ)
        c.lit(4, 16); c.invoke(STATIC, "callm", [0, 4, 1, 2])
        c.ret_void()
        m.append(("wipe", c))

    def setup():
        # What the ring mode keeps, in one Object[16] (a method's registers are few): 0 lockCanvas, 1 unlockCanvasAndPost,
        # 2 drawBitmap, 3 the text Bitmap, 4 its Canvas, 5 the Paint, 6 base (the bitmap's pixels), 7 System.arraycopy,
        # 8 the header's bytes, 9 its ints, 10 getPixels, 11 drawText, 12 eraseColor, 13 setTextSize, 14 setColor,
        # 15 createBitmap, 16 Canvas.drawColor, 17 PorterDuff.Mode.CLEAR, 18 BitmapFactory.decodeByteArray, 19 the Rect
        # constructor, 20 Canvas.drawBitmap(Bitmap, Rect, Rect, Paint)
        # w=0 h=1 ms=2 t=3 k=4 x=5 y=6 z=7
        c = Code(8, 2)
        c.lit(4, 21); c.new_array(2, 4, OBJARR)

        def meth(at, cls, name, ps):
            c.lit(4, len(ps)); c.new_array(3, 4, STRARR)
            for i, p in enumerate(ps):
                c.const_string(5, p); c.lit(4, i); c.aput(5, 3, 4, APUT_OBJ)
            c.const_string(4, cls); c.const_string(5, name)
            c.invoke(STATIC, "meth", [4, 5, 3]); c.move_result(5, True)
            c.lit(4, at); c.aput(5, 2, 4, APUT_OBJ)
        meth(0, "android.view.Surface", "lockCanvas", ("android.graphics.Rect",))
        meth(1, "android.view.Surface", "unlockCanvasAndPost", ("android.graphics.Canvas",))
        meth(2, "android.graphics.Canvas", "drawBitmap", DRAW)
        meth(7, "java.lang.System", "arraycopy", ("java.lang.Object", "int", "java.lang.Object", "int", "int"))
        meth(10, "android.graphics.Bitmap", "getPixels", ("[I", "int", "int", "int", "int", "int", "int"))
        meth(11, "android.graphics.Canvas", "drawText", ("java.lang.String", "float", "float", "android.graphics.Paint"))
        meth(12, "android.graphics.Bitmap", "eraseColor", ("int",))
        meth(13, "android.graphics.Paint", "setTextSize", ("float",))
        meth(14, "android.graphics.Paint", "setColor", ("int",))
        meth(15, "android.graphics.Bitmap", "createBitmap", ("int", "int", "android.graphics.Bitmap$Config"))
        meth(16, "android.graphics.Canvas", "drawColor", ("int", "android.graphics.PorterDuff$Mode"))
        meth(18, "android.graphics.BitmapFactory", "decodeByteArray", ("[B", "int", "int"))
        meth(20, "android.graphics.Canvas", "drawBitmap", ("android.graphics.Bitmap", "android.graphics.Rect", "android.graphics.Rect",
                                                          "android.graphics.Paint"))
        c.const_string(3, "android.graphics.Rect"); c.invoke(STATIC, "cls.forName", [3]); c.move_result(3, True)   # new Rect(int x4)
        c.const_4(4, 4); c.new_array(5, 4, CLSARR); c.sget_obj(6, "int.TYPE")
        for k in range(4):
            c.const_4(4, k); c.aput(6, 5, 4, APUT_OBJ)
        c.invoke(VIRT, "cls.getc", [3, 5]); c.move_result(3, True)
        c.lit(4, 19); c.aput(3, 2, 4, APUT_OBJ)
        c.const_string(3, "android.graphics.PorterDuff$Mode"); c.invoke(STATIC, "cls.forName", [3]); c.move_result(3, True)
        c.const_string(4, "CLEAR"); c.invoke(VIRT, "cls.getf", [3, 4]); c.move_result(3, True)
        c.const_4(4, 0); c.invoke(VIRT, "f.get", [3, 4]); c.move_result(3, True)
        c.lit(4, 17); c.aput(3, 2, 4, APUT_OBJ)
        c.binop(MUL, 4, 0, 1); c.new_array(3, 4, INTS); c.const_4(4, 6); c.aput(3, 2, 4, APUT_OBJ)    # base
        c.lit(4, 20); c.new_array(3, 4, BYTES); c.lit(4, 8); c.aput(3, 2, 4, APUT_OBJ)                 # the header's bytes
        c.const_4(4, 5); c.new_array(3, 4, INTS); c.lit(4, 9); c.aput(3, 2, 4, APUT_OBJ)                # its ints
        # the bitmap: Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        c.const_string(3, "android.graphics.Bitmap$Config"); c.invoke(STATIC, "cls.forName", [3]); c.move_result(3, True)
        c.const_string(4, "ARGB_8888"); c.invoke(VIRT, "cls.getf", [3, 4]); c.move_result(3, True)
        c.const_4(4, 0); c.invoke(VIRT, "f.get", [3, 4]); c.move_result(3, True)                      # a static field: get(null)
        c.const_4(4, 3); c.new_array(5, 4, OBJARR)
        c.invoke(STATIC, "int.value", [0]); c.move_result(6, True); c.const_4(4, 0); c.aput(6, 5, 4, APUT_OBJ)
        c.invoke(STATIC, "int.value", [1]); c.move_result(6, True); c.const_4(4, 1); c.aput(6, 5, 4, APUT_OBJ)
        c.const_4(4, 2); c.aput(3, 5, 4, APUT_OBJ)
        c.lit(4, 15); c.aget(3, 2, 4, AGET_OBJ); c.check_cast(3, MTH)
        c.const_4(4, 0); c.invoke(VIRT, "m.invoke", [3, 4, 5]); c.move_result(3, True)                 # the Bitmap
        c.const_4(4, 3); c.aput(3, 2, 4, APUT_OBJ)
        # its canvas: new Canvas(bitmap)
        c.const_string(5, "android.graphics.Canvas"); c.invoke(STATIC, "cls.forName", [5]); c.move_result(5, True)
        c.const_4(4, 1); c.new_array(6, 4, CLSARR)
        c.const_string(7, "android.graphics.Bitmap"); c.invoke(STATIC, "cls.forName", [7]); c.move_result(7, True)
        c.const_4(4, 0); c.aput(7, 6, 4, APUT_OBJ)
        c.invoke(VIRT, "cls.getc", [5, 6]); c.move_result(5, True)
        c.const_4(4, 1); c.new_array(6, 4, OBJARR); c.const_4(4, 0); c.aput(3, 6, 4, APUT_OBJ)
        c.invoke(VIRT, "ctor.newi", [5, 6]); c.move_result(5, True)
        c.const_4(4, 4); c.aput(5, 2, 4, APUT_OBJ)
        # the paint: new Paint(ANTI_ALIAS_FLAG), text centred
        c.const_string(5, "android.graphics.Paint"); c.invoke(STATIC, "cls.forName", [5]); c.move_result(5, True)
        c.const_4(4, 1); c.new_array(6, 4, CLSARR); c.sget_obj(7, "int.TYPE"); c.const_4(4, 0); c.aput(7, 6, 4, APUT_OBJ)
        c.invoke(VIRT, "cls.getc", [5, 6]); c.move_result(5, True)
        c.const_4(4, 1); c.new_array(6, 4, OBJARR)
        c.const_4(7, 3); c.invoke(STATIC, "int.value", [7]); c.move_result(7, True); c.const_4(4, 0); c.aput(7, 6, 4, APUT_OBJ)
        c.invoke(VIRT, "ctor.newi", [5, 6]); c.move_result(5, True)                   # ANTI_ALIAS | FILTER_BITMAP
        c.const_4(4, 5); c.aput(5, 2, 4, APUT_OBJ)
        c.const_string(6, "android.graphics.Paint$Align"); c.invoke(STATIC, "cls.forName", [6]); c.move_result(6, True)
        c.const_string(7, "CENTER"); c.invoke(VIRT, "cls.getf", [6, 7]); c.move_result(6, True)
        c.const_4(7, 0); c.invoke(VIRT, "f.get", [6, 7]); c.move_result(6, True)
        c.const_string(7, "setTextAlign"); c.const_string(3, "android.graphics.Paint$Align")
        c.invoke(STATIC, "w1", [5, 7, 3, 6])
        c.ret(2, True)
        m.append(("setup", c))

    def callm():                                              # ((Method) ms[k]).invoke(obj, args)
        # ms=0 k=1 obj=2 args=3 m=4 r=5
        c = Code(6, 4)
        c.aget(4, 0, 1, AGET_OBJ); c.check_cast(4, MTH)
        c.invoke(VIRT, "m.invoke", [4, 2, 3]); c.move_result(5, True)
        c.ret(5, True)
        m.append(("callm", c))

    def fbox():                                               # Float.valueOf(String.valueOf(Integer.valueOf(i))): a float, boxed
        # i=0 t=1
        c = Code(2, 1)
        c.invoke(STATIC, "int.value", [0]); c.move_result(1, True)
        c.invoke(STATIC, "str.valueO", [1]); c.move_result(1, True)
        c.invoke(STATIC, "float.value", [1]); c.move_result(1, True)
        c.ret(1, True)
        m.append(("fbox", c))

    def clear():                                              # bitmap.eraseColor(0): clear
        # ms=0 bmp=1 args=2 t=3 k=4 z=5
        c = Code(6, 1)
        c.const_4(4, 3); c.aget(1, 0, 4, AGET_OBJ)
        c.const_4(4, 1); c.new_array(2, 4, OBJARR)
        c.const_4(5, 0); c.invoke(STATIC, "int.value", [5]); c.move_result(3, True); c.const_4(4, 0); c.aput(3, 2, 4, APUT_OBJ)
        c.lit(4, 12); c.invoke(STATIC, "callm", [0, 4, 1, 2])
        c.ret_void()
        m.append(("clear", c))

    def textm():                                              # paint.setTextSize(hdr[2]); paint.setColor(hdr[3]); canvas.drawText(str, w / 2, hdr[1], paint)
        # ms=0 str=1 hdr=2 w=3 args=4 t=5 k=6 paint=7 canvas=8 z=9
        c = Code(10, 4)
        c.const_4(6, 5); c.aget(7, 0, 6, AGET_OBJ)
        c.const_4(6, 4); c.aget(8, 0, 6, AGET_OBJ)
        c.const_4(6, 1); c.new_array(4, 6, OBJARR)
        c.const_4(6, 2); c.aget(9, 2, 6, AGET); c.invoke(STATIC, "fbox", [9]); c.move_result(5, True)
        c.const_4(6, 0); c.aput(5, 4, 6, APUT_OBJ)
        c.lit(6, 13); c.invoke(STATIC, "callm", [0, 6, 7, 4])
        c.const_4(6, 1); c.new_array(4, 6, OBJARR)
        c.const_4(6, 3); c.aget(9, 2, 6, AGET); c.invoke(STATIC, "int.value", [9]); c.move_result(5, True)
        c.const_4(6, 0); c.aput(5, 4, 6, APUT_OBJ)
        c.lit(6, 14); c.invoke(STATIC, "callm", [0, 6, 7, 4])
        c.const_4(6, 4); c.new_array(4, 6, OBJARR)
        c.const_4(6, 0); c.aput(1, 4, 6, APUT_OBJ)
        c.binop_lit8(USHR8, 9, 3, 1); c.invoke(STATIC, "fbox", [9]); c.move_result(5, True); c.const_4(6, 1); c.aput(5, 4, 6, APUT_OBJ)
        c.const_4(6, 1); c.aget(9, 2, 6, AGET); c.invoke(STATIC, "fbox", [9]); c.move_result(5, True); c.const_4(6, 2); c.aput(5, 4, 6, APUT_OBJ)
        c.const_4(6, 3); c.aput(7, 4, 6, APUT_OBJ)
        c.lit(6, 11); c.invoke(STATIC, "callm", [0, 6, 8, 4])
        c.ret_void()
        m.append(("textm", c))

    def pixels():                                             # base = bitmap.getPixels(...); px = base (System.arraycopy)
        # ms=0 px=1 w=2 h=3 args=4 t=5 k=6 base=7 bmp=8 z=9
        c = Code(10, 4)
        c.const_4(6, 6); c.aget(7, 0, 6, AGET_OBJ)
        c.const_4(6, 3); c.aget(8, 0, 6, AGET_OBJ)
        c.const_4(6, 7); c.new_array(4, 6, OBJARR)
        c.const_4(6, 0); c.aput(7, 4, 6, APUT_OBJ)
        c.const_4(9, 0); c.invoke(STATIC, "int.value", [9]); c.move_result(5, True)
        for k in (1, 3, 4):
            c.const_4(6, k); c.aput(5, 4, 6, APUT_OBJ)
        c.invoke(STATIC, "int.value", [2]); c.move_result(5, True)
        for k in (2, 5):
            c.const_4(6, k); c.aput(5, 4, 6, APUT_OBJ)
        c.invoke(STATIC, "int.value", [3]); c.move_result(5, True)
        c.const_4(6, 6); c.aput(5, 4, 6, APUT_OBJ)
        c.lit(6, 10); c.invoke(STATIC, "callm", [0, 6, 8, 4])
        c.const_4(6, 5); c.new_array(4, 6, OBJARR)
        c.const_4(6, 0); c.aput(7, 4, 6, APUT_OBJ)
        c.const_4(9, 0); c.invoke(STATIC, "int.value", [9]); c.move_result(5, True)
        c.const_4(6, 1); c.aput(5, 4, 6, APUT_OBJ); c.const_4(6, 3); c.aput(5, 4, 6, APUT_OBJ)
        c.const_4(6, 2); c.aput(1, 4, 6, APUT_OBJ)
        c.binop(MUL, 9, 2, 3); c.invoke(STATIC, "int.value", [9]); c.move_result(5, True)
        c.const_4(6, 4); c.aput(5, 4, 6, APUT_OBJ)
        c.const_4(6, 7); c.const_4(9, 0); c.invoke(STATIC, "callm", [0, 6, 9, 4])     # static: a null receiver
        c.ret_void()
        m.append(("pixels", c))

    def map_():                                               # px[tab[i]] = strip[tab[i + 1]] for every pair
        # px=0 strip=1 tab=2 i=3 len=4 a=5 b=6 t=7
        c = Code(8, 3)
        c.array_length(4, 2); c.const_4(3, 0)
        c.label("loop")
        c.if2(IFGE, 3, 4, "done")
        c.aget(5, 2, 3, AGET)
        c.binop_lit8(ADD8, 7, 3, 1); c.aget(6, 2, 7, AGET)
        c.aget(7, 1, 6, AGET); c.aput(7, 0, 5, APUT)
        c.binop_lit8(ADD8, 3, 3, 2)
        c.goto_("loop")
        c.label("done")
        c.ret_void()
        m.append(("map", c))

    def full():
        # in=0 buf=1 got=2 len=3 n=4 rest=5
        c = Code(6, 2)
        c.array_length(3, 1)
        c.const_4(2, 0)
        c.label("loop")
        c.if2(IFGE, 2, 3, "whole")
        c.binop(SUB, 5, 3, 2)
        c.invoke(VIRT, "in.read", [0, 1, 2, 5]); c.move_result(4)
        c.ifz(IFLTZ, 4, "end")                                # -1: hassmic closed the pipe
        c.binop(ADD, 2, 2, 4)
        c.goto_("loop")
        c.label("end")
        c.const_4(4, 0); c.ret(4, False)
        c.label("whole")
        c.const_4(4, 1); c.ret(4, False)
        m.append(("full", c))

    def meth():
        # c=0 n=1 ps=2 k=3 cs=4 i=5 len=6 t=7
        c = Code(8, 3)
        c.array_length(6, 2)
        c.new_array(4, 6, CLSARR)
        c.const_4(5, 0)
        c.label("loop")
        c.if2(IFGE, 5, 6, "done")
        c.aget(7, 2, 5, AGET_OBJ)
        c.invoke(STATIC, "cls", [7]); c.move_result(7, True)
        c.aput(7, 4, 5, APUT_OBJ)
        c.binop_lit8(ADD8, 5, 5, 1)
        c.goto_("loop")
        c.label("done")
        c.invoke(STATIC, "cls.forName", [0]); c.move_result(3, True)
        c.invoke(VIRT, "cls.getm", [3, 1, 4]); c.move_result(7, True)
        c.ret(7, True)
        m.append(("meth", c))

    def cls():
        # n=0 t=1
        c = Code(2, 1)
        for name, f in (("int", "int.TYPE"), ("boolean", "bool.TYPE"), ("float", "float.TYPE")):
            c.const_string(1, name); c.invoke(VIRT, "str.eq", [0, 1]); c.move_result(1)
            c.ifz(IFEQZ, 1, "not " + name)
            c.sget_obj(1, f); c.ret(1, True)
            c.label("not " + name)
        c.invoke(STATIC, "cls.forName", [0]); c.move_result(1, True)
        c.ret(1, True)
        m.append(("cls", c))

    def inv():
        # o=0 n=1 ps=2 as=3 cs=4 m=5 i=6 len=7 r=8
        c = Code(9, 4)
        c.array_length(7, 2)
        c.new_array(4, 7, CLSARR)
        c.const_4(6, 0)
        c.label("loop")
        c.if2(IFGE, 6, 7, "done")
        c.aget(8, 2, 6, AGET_OBJ)
        c.invoke(STATIC, "cls", [8]); c.move_result(8, True)
        c.aput(8, 4, 6, APUT_OBJ)
        c.binop_lit8(ADD8, 6, 6, 1)
        c.goto_("loop")
        c.label("done")
        c.invoke(VIRT, "obj.getcls", [0]); c.move_result(7, True)
        c.invoke(VIRT, "cls.getm", [7, 1, 4]); c.move_result(5, True)
        c.const_4(7, 1); c.invoke(VIRT, "m.setacc", [5, 7])   # the binder proxy class is private
        c.invoke(VIRT, "m.invoke", [5, 0, 3]); c.move_result(7, True)
        c.ret(7, True)
        m.append(("inv", c))

    def sinv():
        # c=0 n=1 p=2 a=3 k=4 cs=5 as=6 m=7 z=8
        c = Code(9, 4)
        c.invoke(STATIC, "cls.forName", [0]); c.move_result(4, True)
        c.invoke(STATIC, "cls", [2]); c.move_result(7, True)
        c.const_4(8, 1); c.new_array(5, 8, CLSARR); c.const_4(8, 0); c.aput(7, 5, 8, APUT_OBJ)
        c.const_4(8, 1); c.new_array(6, 8, OBJARR); c.const_4(8, 0); c.aput(3, 6, 8, APUT_OBJ)
        c.invoke(VIRT, "cls.getm", [4, 1, 5]); c.move_result(7, True)
        c.const_4(8, 0)                                       # null: the static call
        c.invoke(VIRT, "m.invoke", [7, 8, 6]); c.move_result(7, True)
        c.ret(7, True)
        m.append(("sinv", c))

    def sinv0():
        # c=0 n=1 k=2 cs=3 as=4 m=5 z=6
        c = Code(7, 2)
        c.invoke(STATIC, "cls.forName", [0]); c.move_result(2, True)
        c.const_4(6, 0); c.new_array(3, 6, CLSARR); c.new_array(4, 6, OBJARR)
        c.invoke(VIRT, "cls.getm", [2, 1, 3]); c.move_result(5, True)
        c.invoke(VIRT, "m.invoke", [5, 6, 4]); c.move_result(5, True)    # v6 still 0: null, the static call
        c.ret(5, True)
        m.append(("sinv0", c))

    def w1():
        # w=0 n=1 p=2 a=3 ps=4 as=5 r=6
        c = Code(7, 4)
        c.const_4(6, 1); c.new_array(4, 6, STRARR); c.const_4(6, 0); c.aput(2, 4, 6, APUT_OBJ)
        c.const_4(6, 1); c.new_array(5, 6, OBJARR); c.const_4(6, 0); c.aput(3, 5, 6, APUT_OBJ)
        c.invoke(STATIC, "inv", [0, 1, 4, 5]); c.move_result(6, True)
        c.ret(6, True)
        m.append(("w1", c))

    def w2():
        # w=0 n=1 p1=2 p2=3 a1=4 a2=5 ps=6 as=7 r=8
        c = Code(9, 6)
        c.const_4(8, 2); c.new_array(6, 8, STRARR)
        c.const_4(8, 0); c.aput(2, 6, 8, APUT_OBJ); c.const_4(8, 1); c.aput(3, 6, 8, APUT_OBJ)
        c.const_4(8, 2); c.new_array(7, 8, OBJARR)
        c.const_4(8, 0); c.aput(4, 7, 8, APUT_OBJ); c.const_4(8, 1); c.aput(5, 7, 8, APUT_OBJ)
        c.invoke(STATIC, "inv", [0, 1, 6, 7]); c.move_result(8, True)
        c.ret(8, True)
        m.append(("w2", c))

    def os_():
        # o=0 n=1 ps=2 as=3 r=4
        c = Code(5, 2)
        c.const_4(4, 0); c.new_array(2, 4, STRARR)
        c.const_4(4, 0); c.new_array(3, 4, OBJARR)
        c.invoke(STATIC, "inv", [0, 1, 2, 3]); c.move_result(4, True)
        c.ret(4, True)
        m.append(("os", c))

    def fint():
        # o=0 f=1 k=2 fl=3
        c = Code(4, 2)
        c.invoke(VIRT, "obj.getcls", [0]); c.move_result(2, True)
        c.invoke(VIRT, "cls.getf", [2, 1]); c.move_result(3, True)
        c.invoke(VIRT, "f.get", [3, 0]); c.move_result(2, True)
        c.check_cast(2, INTG)
        c.invoke(VIRT, "int.intval", [2]); c.move_result(2)
        c.ret(2, False)
        m.append(("fint", c))

    def out():
        # s=0 o=1
        c = Code(2, 1)
        c.sget_obj(1, "sys.out")
        c.invoke(VIRT, "println", [1, 0])
        c.invoke(VIRT, "flush", [1])                          # a pipe: System.out would hold the line back
        c.ret_void()
        m.append(("out", c))

    for f in (main, overscan, create, surface, meths, loop, table, ring, setup, callm, fbox, clear, textm, pixels, wipe, payload, imagem, map_,
              full, meth, cls, inv, sinv, sinv0, w1, w2, os_, fint, out):
        f()
    return m


# ---------------------------------------------------------------- against the firmware's own framework
# What the reflection asks the framework for (class, method or "<init>", parameter types, static), checked
# against the boot class path of each firmware --framework names (make unit: every model with the band), and the ints
# the code has written in.
SC, SURF, CANVAS = "Landroid/view/SurfaceControl;", "Landroid/view/Surface;", "Landroid/graphics/Canvas;"
BITMAP, BCONFIG, PAINT, PALIGN = "Landroid/graphics/Bitmap;", "Landroid/graphics/Bitmap$Config;", "Landroid/graphics/Paint;", "Landroid/graphics/Paint$Align;"
PDMODE = "Landroid/graphics/PorterDuff$Mode;"
SCB = "Landroid/view/SurfaceControl$Builder;"
DMG, DINFO = "Landroid/hardware/display/DisplayManagerGlobal;", "Landroid/view/DisplayInfo;"
REFLECTED = [
    ("Landroid/os/ServiceManager;", "getService", [STR], True),
    ("Landroid/view/IWindowManager$Stub;", "asInterface", ["Landroid/os/IBinder;"], True),
    ("Landroid/view/IWindowManager;", "setOverscan", ["I", "I", "I", "I", "I"], False),
    (DMG, "getInstance", [], True),
    (DMG, "getDisplayInfo", ["I"], False),
    ("Landroid/view/SurfaceSession;", "<init>", [], False),
    (SC, "<init>", ["Landroid/view/SurfaceSession;", STR, "I", "I", "I", "I"], False),
    (SCB, "<init>", ["Landroid/view/SurfaceSession;"], False),
    (SCB, "setName", [STR], False),
    (SCB, "setSize", ["I", "I"], False),
    (SCB, "setFormat", ["I"], False),
    (SCB, "setFlags", ["I"], False),
    (SCB, "build", [], False),
    (SC, "openTransaction", [], True),
    (SC, "closeTransaction", [], True),
    (SC, "setLayer", ["I"], False),
    (SC, "setPosition", ["F", "F"], False),
    (SC, "show", [], False),
    (SURF, "<init>", [], False),
    (SURF, "copyFrom", [SC], False),
    (SURF, "lockCanvas", ["Landroid/graphics/Rect;"], False),
    (SURF, "unlockCanvasAndPost", [CANVAS], False),
    (CANVAS, "drawBitmap", ["[I", "I", "I", "I", "I", "I", "I", "Z", "Landroid/graphics/Paint;"], False),
    # the ring mode's text (the clock): a bitmap drawn by the framework's Canvas and Paint
    (BITMAP, "createBitmap", ["I", "I", BCONFIG], True),
    (BITMAP, "getPixels", ["[I", "I", "I", "I", "I", "I", "I"], False),
    (BITMAP, "eraseColor", ["I"], False),
    (CANVAS, "<init>", [BITMAP], False),
    (CANVAS, "drawText", [STR, "F", "F", PAINT], False),
    (PAINT, "<init>", ["I"], False),
    (PAINT, "setTextAlign", [PALIGN], False),
    (PAINT, "setTextSize", ["F"], False),
    (PAINT, "setColor", ["I"], False),
    ("Ljava/lang/System;", "arraycopy", ["Ljava/lang/Object;", "I", "Ljava/lang/Object;", "I", "I"], True),
    (CANVAS, "drawColor", ["I", PDMODE], False),
    ("Landroid/graphics/BitmapFactory;", "decodeByteArray", ["[B", "I", "I"], True),
    ("Landroid/graphics/Rect;", "<init>", ["I", "I", "I", "I"], False),
    (CANVAS, "drawBitmap", [BITMAP, "Landroid/graphics/Rect;", "Landroid/graphics/Rect;", PAINT], False),
]
REFLECTED_FIELDS = [
    (DINFO, "logicalWidth", "I"),
    (DINFO, "logicalHeight", "I"),
    (DINFO, "rotation", "I"),
    (BCONFIG, "ARGB_8888", BCONFIG),
    (PALIGN, "CENTER", PALIGN),
    (PDMODE, "CLEAR", PDMODE),
]
CONSTANTS = [
    ("Landroid/graphics/PixelFormat;", "OPAQUE", OPAQUE),
    ("Landroid/graphics/PixelFormat;", "TRANSLUCENT", TRANSLUCENT),
    (SC, "HIDDEN", HIDDEN),
    (SURF, "ROTATION_0", 0), (SURF, "ROTATION_90", 1), (SURF, "ROTATION_180", 2), (SURF, "ROTATION_270", 3),
]

# the constructor of Android 7, the Builder of Android 9 (athena): the code takes the one the caller says (argv[2])
ONLY_IF = {(SC, "<init>", ("Landroid/view/SurfaceSession;", STR, "I", "I", "I", "I")): "!" + SCB}
for _n, _p in (("<init>", ("Landroid/view/SurfaceSession;",)), ("setName", (STR,)), ("setSize", ("I", "I")),
               ("setFormat", ("I",)), ("setFlags", ("I",)), ("build", ())):
    ONLY_IF[(SCB, _n, _p)] = SCB

PROG = Program(ME, SELF, reflected=REFLECTED, reflected_fields=REFLECTED_FIELDS, constants=CONSTANTS, only_if=ONLY_IF)

def main():
    dexasm.cli(PROG, emit, __file__, "ringbar.dex")


if __name__ == "__main__":
    main()
