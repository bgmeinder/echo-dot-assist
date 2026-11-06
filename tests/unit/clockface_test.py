#!/usr/bin/env python3
"""clockface.c against a package of its own: a zip in the shape of the Echo Spot's clock-face package (templates,
backgrounds, faces as JSON under assets/raw, deflated as aapt leaves them; a thumbnail stored), nothing of Amazon's.
Checks: the templates with an analog face only, sorted; the defaults; the ops for a face at a given time (background,
tinted image layers, the three hands at their angles, the date-only layers left out); night images and colours; the
thumbnail; a damaged package refused without a crash."""
import json
import os
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

C = r'''
#include "clockface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    int n; const char *const *names = clockface_names(&n);
    printf("names");
    for (int i = 0; i < n; i++) printf(" %s", names[i]);
    printf("\n");
    for (int t = 0; t < clockface_count(); t++) {
        int ns, nc; const char *const *s = clockface_styles(t, &ns), *const *c = clockface_colors(t, &nc);
        printf("tmpl %d styles", t);
        for (int i = 0; i < ns; i++) printf(" %s", s[i]);
        printf(" colors");
        for (int i = 0; i < nc; i++) printf(" %s", c[i]);
        printf(" default %d %d\n", clockface_default_style(t), clockface_default_color(t));
    }
    struct tm tm = { .tm_hour = 22, .tm_min = 8, .tm_sec = 30 };
    for (int night = 0; night < 2 && clockface_count(); night++) {
        struct cf_op ops[16]; int k = clockface_ops(0, -1, -1, &tm, night, ops, 16);
        for (int i = 0; i < k; i++) printf("op %d %s %d %08x %d %d\n", night, ops[i].name, ops[i].rot, ops[i].tint, ops[i].cx, ops[i].cy);
    }
    size_t len = 0; unsigned char *p = clockface_count() ? clockface_thumb(0, &len) : NULL;
    printf("thumb %zu %s\n", len, p && len >= 4 && !memcmp(p + 1, "PNG", 3) ? "ok" : "none");
    free(p);
    return 0;
}
'''


def build(path, damaged=False):
    face = {"type": "ClockFace", "id": "zen_analog_bold", "is_analog": True, "layers": [
        {"type": "ImageLayer", "src": "img_ticks", "day_color": "#FFFFFF", "night_color": "#00FFF4"},
        {"type": "ImageLayer", "day_src": "img_date_day", "night_src": "img_date_night", "center_x": 240,
         "center_y": 154, "layer_properties": ["SHOWS_DATE"]},
        {"type": "DateWindowLayer", "center_x": 240, "bottom_y": 164},
        {"type": "ComplicationAlertLayer", "day_color": "alert_color_white"},
        {"type": "ImageLayer", "day_src": "img_logo_day", "night_src": "img_logo_night", "center_x": 240, "center_y": 330,
         "day_color": "#80102030"},
        {"type": "RotatingHandLayer", "day_src": "hand_hour_day", "night_src": "hand_hour_night", "hand_rotation": "HOUR_12"},
        {"type": "RotatingHandLayer", "day_src": "hand_min_day", "night_src": "hand_min_night", "hand_rotation": "MINUTE"},
        {"type": "RotatingHandLayer", "day_src": "hand_sec_day", "night_src": "hand_sec_night", "hand_rotation": "SECOND"},
    ]}
    digital = {"type": "ClockFace", "id": "zen_digital", "is_analog": False, "layers": [{"type": "TextClockLayer"}]}
    files = {
        "assets/raw/clock_template_zen": {"id": "zen", "face_ids": ["zen_digital", "zen_analog_bold"],
                                          "background_ids": ["zen_sand", "zen_stone"],
                                          "default": {"face": "zen_analog_bold", "background": "zen_stone"}},
        "assets/raw/clock_template_beach": {"id": "beach", "face_ids": ["zen_analog_bold"], "background_ids": ["zen_sand"],
                                            "default": {"face": "zen_analog_bold", "background": "zen_sand"}},
        "assets/raw/clock_template_lcd": {"id": "lcd", "face_ids": ["zen_digital"], "background_ids": ["zen_sand"]},
        "assets/raw/clock_template_photos": {"id": "photos", "photo_provider": "ZORDON", "face_ids": ["zen_analog_bold"],
                                             "background_ids": ["custom_photo"]},
        "assets/raw/clock_face_zen_analog_bold": face,
        "assets/raw/clock_face_zen_digital": digital,
        "assets/raw/clock_background_zen_sand": {"id": "zen_sand", "layers": [{"type": "ImageLayer", "day_src": "bg_sand_day", "night_src": "bg_night"}]},
        "assets/raw/clock_background_zen_stone": {"id": "zen_stone", "layers": [{"type": "ImageLayer", "day_src": "bg_stone_day", "night_src": "bg_night"}]},
    }
    with zipfile.ZipFile(path, "w") as z:
        for name, obj in files.items():
            z.writestr(zipfile.ZipInfo(name), json.dumps(obj, indent=2), compress_type=zipfile.ZIP_DEFLATED)
        z.writestr(zipfile.ZipInfo("res/drawable/img_list_clockface_beach.png"), b"\x89PNG fake", compress_type=zipfile.ZIP_STORED)
    if damaged:
        data = bytearray(open(path, "rb").read())
        end = data.rfind(b"PK\x05\x06")
        data[end + 16:end + 20] = (len(data) * 2).to_bytes(4, "little")      # the directory past the end of the file
        open(path, "wb").write(data)


def run(exe, pkg):
    env = dict(os.environ, HASSMIC_CLOCK_FACES=pkg, HASSMIC_CLOCK_FACES_PKG="com.example.faces")
    return subprocess.run([exe], env=env, capture_output=True, text=True, check=True).stdout.splitlines()


def main():
    fails = 0

    def check(what, ok):
        nonlocal fails
        print(("ok   " if ok else "FAIL ") + what)
        fails += not ok

    with tempfile.TemporaryDirectory() as d:
        src, exe = os.path.join(d, "t.c"), os.path.join(d, "t")
        open(src, "w").write(C)
        board = os.path.join(d, "board.c")
        open(board, "w").write('#include "board.h"\nconst struct board board = { .model = "test" };\n')
        subprocess.run(["cc", "-O2", "-Wall", "-Wextra", f"-I{ROOT}/src/hassmic", src, board, f"{ROOT}/src/hassmic/clockface.c",
                        f"{ROOT}/src/hassmic/json.c", "-lz", "-lpthread", "-o", exe], check=True)
        pkg = os.path.join(d, "faces.apk")
        build(pkg)
        out = run(exe, pkg)
        check("templates with an analog face, sorted, after \"digital\"; none with online photos", out[0] == "names digital beach zen")
        check("zen's analog faces only, its default background", out[2] == "tmpl 1 styles zen_analog_bold colors zen_sand zen_stone default 0 1")
        ops = [l.split() for l in out if l.startswith("op 0 ")]
        names = [o[2] for o in ops]
        check("beach by day: background, ticks, logo, three hands; date-only layers out",
              names == ["bg_sand_day", "img_ticks", "img_logo_day", "hand_hour_day", "hand_min_day", "hand_sec_day"])
        check("ticks tinted white, filling the face", ops[1][4:] == ["ffffffff", "-1", "-1"])
        check("the logo's tint with its alpha, centred at (240, 330)", ops[2][4:] == ["80102030", "240", "330"])
        # 22:08:30 -> hour (10 + 8.5/60) * 30 = 304.25 deg, minute 8.5 * 6 = 51 deg, second 180 deg
        check("hands at 22:08:30", [int(o[3]) for o in ops[3:]] == [30425, 5100, 18000])
        night = [l.split()[2] for l in out if l.startswith("op 1 ")]
        check("by night: night images", night[0] == "bg_night" and night[3:] == ["hand_hour_night", "hand_min_night", "hand_sec_night"])
        check("by night: night tint", [l.split()[4] for l in out if l.startswith("op 1 ")][1] == "ff00fff4")
        check("thumbnail read stored", out[-1] == "thumb 9 ok")
        build(pkg, damaged=True)
        out = run(exe, pkg)
        check("a damaged package: no templates, no crash", out[0] == "names digital" and out[-1] == "thumb 0 none")
        out = run(exe, os.path.join(d, "missing.apk"))
        check("no package: digital only", out[0] == "names digital")
    print(f"\n{'FAILED' if fails else 'passed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
