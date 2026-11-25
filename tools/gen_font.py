"""Генератор шрифтів Adafruit GFX з кирилицею (кодування CP1251) на основі GNU FreeSans.
Коди символів: 0x20..0x7E ASCII, 0xA8 Ё, 0xAA Є, 0xAF Ї, 0xB2 І, 0xB3 і, 0xB4 ґ, 0xA5 Ґ,
0xB8 ё, 0xBA є, 0xBF ї, 0xC0..0xFF А..я, 0xB0 градус."""
from PIL import Image, ImageDraw, ImageFont
import sys

FD = "/usr/share/fonts/truetype/freefont/"
FIRST, LAST = 0x20, 0xFF
EXTRA = {0xA5, 0xA8, 0xAA, 0xAF, 0xB0, 0xB2, 0xB3, 0xB4, 0xB8, 0xBA, 0xBF}
WANTED = set(range(0x20, 0x7F)) | EXTRA | set(range(0xC0, 0x100))


def build(ttf, px, name, ydv):
    font = ImageFont.truetype(FD + ttf, px)
    bitmaps = bytearray()
    glyphs = []
    space_adv = int(round(font.getlength(" ")))
    table = {}
    for code in range(FIRST, LAST + 1):
        if code not in WANTED:
            glyphs.append((len(bitmaps), 0, 0, space_adv, 0, 0))
            continue
        ch = bytes([code]).decode("cp1251")
        adv = int(round(font.getlength(ch)))
        l, t, r, b = font.getbbox(ch, anchor="ls")
        w, h = r - l, b - t
        if w <= 0 or h <= 0 or ch == " ":
            glyphs.append((len(bitmaps), 0, 0, adv if adv else space_adv, 0, 0))
            continue
        im = Image.new("1", (w, h), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((-l, -t), ch, font=font, fill=1, anchor="ls")
        px_ = im.load()
        off = len(bitmaps)
        acc, nbits = 0, 0
        for y in range(h):
            for x in range(w):
                acc = (acc << 1) | (1 if px_[x, y] else 0)
                nbits += 1
                if nbits == 8:
                    bitmaps.append(acc)
                    acc, nbits = 0, 0
        if nbits:
            bitmaps.append(acc << (8 - nbits))
        glyphs.append((off, w, h, adv, l, t))
        table[code] = (off, w, h, adv, l, t)
    return bitmaps, glyphs, ydv


def emit(f, name, bitmaps, glyphs, ydv):
    f.write(f"const uint8_t {name}Bitmaps[] PROGMEM = {{\n")
    for i in range(0, len(bitmaps), 16):
        f.write("  " + ", ".join(f"0x{b:02X}" for b in bitmaps[i:i + 16]) + ("," if i + 16 < len(bitmaps) else "") + "\n")
    f.write("};\n\n")
    f.write(f"const GFXglyph {name}Glyphs[] PROGMEM = {{\n")
    for i, (off, w, h, adv, xo, yo) in enumerate(glyphs):
        code = FIRST + i
        f.write(f"  {{{off}, {w}, {h}, {adv}, {xo}, {yo}}}" + ("," if i < len(glyphs) - 1 else "") + f"  // 0x{code:02X}\n")
    f.write("};\n\n")
    f.write(f"const GFXfont {name} PROGMEM = {{(uint8_t *){name}Bitmaps, (GFXglyph *){name}Glyphs, 0x{FIRST:02X}, 0x{LAST:02X}, {ydv}}};\n\n")


if __name__ == "__main__":
    out = sys.argv[1]
    fonts = [("FreeSans.ttf", 17.625, "FreeSans9ptCyr", 22),
             ("FreeSans.ttf", 23.5, "FreeSans12ptCyr", 28)]
    built = []
    with open(out, "w", encoding="utf-8") as f:
        f.write("#pragma once\n// Шрифти Adafruit GFX з українською кирилицею.\n"
                "// Створено скриптом на основі GNU FreeSans (та сама гарнітура, що й FreeSans9pt/12pt в Adafruit GFX).\n"
                "// Символи кодуються в CP1251: див. ukr.h, там UTF-8 перетворюється в цей код.\n"
                "// Діапазон 0x20..0xFF: ASCII, літери А-я, Ґ ґ Є є І і Ї ї, градус (0xB0).\n\n"
                "#include <Arduino.h>\n#include <gfxfont.h>\n\n")
        for ttf, px, name, ydv in fonts:
            b, g, y = build(ttf, px, name, ydv)
            emit(f, name, b, g, y)
            built.append((name, len(b)))
    print(built)
