"""Regenerate main/ui_glyphs.h from NotoSansCJK-Bold.ttc (SC face by default)."""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ASCII = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZkm~Ω./ ()+-*"
CHINESE = "电流监测数显指针曲线组合量程压功率字示模拟趋势波形仪表万用交流直二极管阻档参考测试"


def make_font(font_file, face, size, chars):
    font = ImageFont.truetype(font_file, size, index=face)
    pixels = bytearray()
    glyphs = []
    for char in sorted(set(chars)):
        left, top, right, bottom = font.getbbox(char, anchor="ls")
        width, height = right - left, bottom - top
        if height:
            image = Image.new("L", (width, height))
            ImageDraw.Draw(image).text((-left, -top), char, font=font, fill=255, anchor="ls")
            values = [(value + 8) // 17 for value in image.tobytes()]
        else:
            values = []
        if len(values) % 2:
            values.append(0)
        glyphs.append((ord(char), len(pixels), width, height, left, top, round(font.getlength(char))))
        pixels.extend(a | b << 4 for a, b in zip(values[::2], values[1::2]))
    return pixels, glyphs, -font.getbbox("0", anchor="ls")[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("--face", type=int, default=2)
    parser.add_argument("--output", type=Path, default=Path("main/ui_glyphs.h"))
    args = parser.parse_args()
    with args.output.open("w", encoding="ascii", newline="\n") as out:
        out.write("/* Generated from Noto Sans CJK SC Bold; see docs/font-license.txt. */\n")
        for name, size, chars in (
            ("axis", 14, ASCII + "电压流功率"),
            ("label", 19, ASCII + CHINESE),
            ("title", 28, ASCII + CHINESE),
            ("meter", 36, ASCII + "万用表"),
            ("large", 80, "0123456789.VAWOL- "),
            ("dmm_value", 108, "0123456789.VOL-MkΩ "),
            ("dmm_history", 23, "0123456789.VOL-MkΩ "),
        ):
            pixels, glyphs, cap_height = make_font(str(args.font), args.face, size, chars)
            out.write(f"static const uint8_t {name}_pixels[] = {{\n")
            for pos in range(0, len(pixels), 24):
                out.write("    " + ",".join(f"0x{i:02x}" for i in pixels[pos:pos + 24]) + ",\n")
            out.write(f"}};\nstatic const glyph_t {name}_glyphs[] = {{\n")
            for code, offset, width, height, left, top, advance in glyphs:
                out.write(f"    {{{code}, {offset}, {width}, {height}, {left}, {top}, {advance}}},\n")
            out.write(f"}};\nstatic const font_t {name}_font = {{"
                      f"{name}_pixels, {name}_glyphs, sizeof({name}_glyphs)/sizeof(glyph_t), {cap_height}}};\n")


if __name__ == "__main__":
    main()
