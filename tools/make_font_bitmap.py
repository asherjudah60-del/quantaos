#!/usr/bin/env python3
"""Rasterize the supplied Ubuntu Mono face to the kernel's 8x16 glyph format."""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser()
parser.add_argument("font", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
font = ImageFont.truetype(str(args.font), 13)
bitmap = bytearray(256 * 16)
for character in range(32, 127):
    image = Image.new("1", (8, 16), 0)
    draw = ImageDraw.Draw(image)
    draw.text((0, -1), chr(character), font=font, fill=1, stroke_width=0)
    for row in range(16):
        bits = 0
        for column in range(8):
            if image.getpixel((column, row)):
                bits |= 0x80 >> column
        bitmap[character * 16 + row] = bits
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(bitmap)
