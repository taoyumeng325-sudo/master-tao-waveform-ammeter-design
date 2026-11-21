"""Pack fit-to-screen reference images as indexed RGB565 frames."""
import argparse
from pathlib import Path
import struct

WIDTH, HEIGHT = 854, 480
MAX_IMAGES = 8


def pack(output, sources):
    if not sources:
        output.write_bytes(b"")
        return
    if len(sources) > MAX_IMAGES:
        raise SystemExit(f"Expected up to {MAX_IMAGES} UI images, got {len(sources)}.")
    try:
        from PIL import Image, ImageOps
    except ImportError:
        raise SystemExit("Install Pillow in the ESP-IDF Python environment: python -m pip install Pillow")
    frames = bytearray()
    for source in sources:
        with Image.open(source) as original:
            image = ImageOps.exif_transpose(original).convert("RGBA")
            image.thumbnail((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
            canvas = Image.new("RGB", (WIDTH, HEIGHT))
            canvas.paste(image, ((WIDTH - image.width) // 2, (HEIGHT - image.height) // 2), image)
        indexed = canvas.quantize(colors=256, dither=Image.Dither.NONE)
        palette = indexed.getpalette()
        palette.extend([0] * (768 - len(palette)))
        rgb565 = [(r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)
                  for r, g, b in zip(palette[::3], palette[1::3], palette[2::3])]
        frames.extend(struct.pack("<256H", *rgb565))
        frames.extend(indexed.tobytes())
    output.write_bytes(frames)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("images", metavar="IMAGE", nargs="*", type=Path)
    args = parser.parse_args()
    pack(args.output, args.images)
