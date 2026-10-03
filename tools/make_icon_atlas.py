#!/usr/bin/env python3
"""Rasterize selected Quanta SVG icons into a fixed-size RGBA atlas."""
from __future__ import annotations

import argparse
import pathlib

import cairo
import gi

gi.require_version("Rsvg", "2.0")
from gi.repository import Rsvg

ICON_SIZE = 24
ICONS = (
    "system/files-svgrepo-com.svg",
    "system/settings-svgrepo-com.svg",
    "system/terminal-svgrepo-com.svg",
    "system/menu-svgrepo-com.svg",
    "system/wifi-svgrepo-com.svg",
    "system/speaker-2-svgrepo-com.svg",
    "system/battery-full-svgrepo-com.svg",
    "files/word-document-svgrepo-com.svg",
)

parser = argparse.ArgumentParser()
parser.add_argument("source", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()

pixels = bytearray()
for filename in ICONS:
    handle = Rsvg.Handle.new_from_file(str(args.source / filename))
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, ICON_SIZE, ICON_SIZE)
    context = cairo.Context(surface)
    viewport = Rsvg.Rectangle()
    viewport.x = 0
    viewport.y = 0
    viewport.width = ICON_SIZE
    viewport.height = ICON_SIZE
    if not handle.render_document(context, viewport):
        raise SystemExit(f"{filename}: SVG rendering failed")
    surface.flush()
    raster = surface.get_data()
    stride = surface.get_stride()
    for y in range(ICON_SIZE):
        for x in range(ICON_SIZE):
            alpha = raster[y * stride + x * 4 + 3]
            pixels.extend((0, 0, 0, alpha))

args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(pixels)
