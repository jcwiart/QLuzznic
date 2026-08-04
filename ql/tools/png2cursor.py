#!/usr/bin/env python3
"""Convert selection.png (marching-ants cursor, N frames) into AND/OR
mask pairs for a masked mode-8 blit, and emit an as68 .s file.

Tiles are 24x32 (not square) to match blocks.png/png2sprites.py's tile
size -- see that script's header for why (mode 8's 4:3 pixel aspect).

Transparency rule ported exactly from game.js's cursor-loading IIFE:
only the outer 1px ring and the diagonal corner-bevel pixel (at x/y one
step in from each corner) are opaque (the dashed outline itself);
everything else is transparent, letting the block underneath show
through.

If the source frames are already 24x32 (real hand-drawn art), the ring
is classified directly at that size. If the source is smaller (e.g. a
16x16 placeholder awaiting real art), classifying the ring at the
*destination* size would check positions the stretch didn't actually
move the original ring/bevel pixels to -- e.g. a 16-wide frame's bevel
at source x=14 lands at destination x=15 after a 16->24 stretch, not at
x=22 (24-2) like a native 24-wide frame's would. So instead: classify
ring/opaque/colour per pixel at the SOURCE's own native size (where the
1px-ring assumption is valid), then nearest-neighbour stretch that
already-classified grid up to 24x32. This is correct for both a native
24x32 drawing (stretch is then a no-op, 1:1) and a smaller placeholder.

Per pixel, in each of the two mode-8 planes (G/F even byte, R/B odd
byte), 2 bits encode that pixel's slot (see png2sprites.py for the full
bit-layout derivation). The masked blit is `dest = (dest & and) | or`:
  opaque pixel:      and bits = 00 (clear the slot), or bits = the
                      pixel's actual colour (forces black or white)
  transparent pixel:  and bits = 11 (preserve), or bits = 00 (no-op)
"""
import sys
from pathlib import Path
from PIL import Image

TILE_W = 24
TILE_H = 32


def is_ring_native(x, y, w, h):
    on_edge = x == 0 or x == w - 1 or y == 0 or y == h - 1
    on_corner_bevel = (x == 1 or x == w - 2) and (y == 1 or y == h - 2)
    return on_edge or on_corner_bevel


def classify_source_frame(img, frame_x0, frame_w, frame_h):
    """Returns a frame_h x frame_w grid of (is_opaque, is_white), classified
    at the source's own native resolution (see module docstring)."""
    grid = []
    for y in range(frame_h):
        row = []
        for x in range(frame_w):
            if is_ring_native(x, y, frame_w, frame_h):
                r, g, b = img.getpixel((frame_x0 + x, y))[:3]
                row.append((True, r > 127))
            else:
                row.append((False, False))
        grid.append(row)
    return grid


def stretch_grid(grid, src_w, src_h, dst_w, dst_h):
    """Nearest-neighbour (centre-of-pixel) stretch of a classification grid
    -- same mapping as the manual blocks.png/selection.png placeholder
    stretch, so ring pixels land consistently with the rest of the art."""
    out = []
    for dy in range(dst_h):
        sy = min(int((dy + 0.5) * src_h / dst_h), src_h - 1)
        row = []
        for dx in range(dst_w):
            sx = min(int((dx + 0.5) * src_w / dst_w), src_w - 1)
            row.append(grid[sy][sx])
        out.append(row)
    return out


def encode_frame_grid(grid):
    """grid: TILE_H x TILE_W of (is_opaque, is_white). Returns (and_bytes, or_bytes)."""
    and_out = []
    or_out = []
    for y in range(TILE_H):
        for g in range(TILE_W // 4):
            and_even = 0
            and_odd = 0
            or_even = 0
            or_odd = 0
            for px in range(4):
                x = g * 4 + px
                shift = (3 - px) * 2
                opaque, white = grid[y][x]
                if opaque:
                    and_even |= 0 << shift  # clear this pixel's slot
                    and_odd |= 0 << shift
                    if white:
                        or_even |= 0b10 << shift  # G=1,F=0
                        or_odd |= 0b11 << shift   # R=1,B=1
                    # black: or bits stay 0
                else:
                    and_even |= 0b11 << shift  # preserve
                    and_odd |= 0b11 << shift
            and_out.append(and_even & 0xFF)
            and_out.append(and_odd & 0xFF)
            or_out.append(or_even & 0xFF)
            or_out.append(or_odd & 0xFF)
    return and_out, or_out


def emit_bytes(lines, label, data, n_frames):
    lines.append(f"\t.extern\t{label}")
    lines.append("\t.even")
    lines.append(f"{label}:")
    per_frame = len(data) // n_frames
    for f in range(n_frames):
        frame_bytes = data[f * per_frame:(f + 1) * per_frame]
        lines.append(f"; frame {f}")
        for row in range(0, len(frame_bytes), 16):
            chunk = frame_bytes[row:row + 16]
            hex_list = ",".join(f"${b:02x}" for b in chunk)
            lines.append(f"\t.data1\t{hex_list}")


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("assets/selection.png")
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("asm/cursor_data.s")

    img = Image.open(src).convert("RGB")

    if img.height == TILE_H:
        src_frame_w, src_frame_h = TILE_W, TILE_H
    else:
        # Legacy/placeholder: frames are square, sized to the image height.
        src_frame_w = src_frame_h = img.height
    n_frames = img.width // src_frame_w
    needs_stretch = (src_frame_w, src_frame_h) != (TILE_W, TILE_H)

    all_and = []
    all_or = []
    for f in range(n_frames):
        grid = classify_source_frame(img, f * src_frame_w, src_frame_w, src_frame_h)
        if needs_stretch:
            grid = stretch_grid(grid, src_frame_w, src_frame_h, TILE_W, TILE_H)
        and_bytes, or_bytes = encode_frame_grid(grid)
        all_and.extend(and_bytes)
        all_or.extend(or_bytes)

    bytes_per_frame = TILE_H * (TILE_W // 4 * 2)
    lines = []
    lines.append("; cursor_data.s -- auto-generated by tools/png2cursor.py from")
    lines.append(f"; {src.name}. Do not edit by hand -- regenerate instead.")
    lines.append(f"; {n_frames} frames, {TILE_W}x{TILE_H} mode 8 pixels, {bytes_per_frame} bytes each,")
    lines.append("; as an AND-mask/OR-mask pair per frame (masked blit: dest=(dest&and)|or).")
    lines.append("")
    lines.append("\t.sect\t.data")
    emit_bytes(lines, "_cursor_and_data", all_and, n_frames)
    lines.append("")
    emit_bytes(lines, "_cursor_or_data", all_or, n_frames)

    dst.write_text("\n".join(lines) + "\n")
    print(f"Wrote {dst} ({n_frames} frames, source {src_frame_w}x{src_frame_h}{' stretched' if needs_stretch else ' native'})")


if __name__ == "__main__":
    main()
