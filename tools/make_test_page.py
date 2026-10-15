#!/usr/bin/env python3
"""Generate a full-page monochrome PBM test pattern for Canon LBP-1120 CAPT."""
import sys

WIDTH = 4960
HEIGHT = 7014
OUT = sys.argv[1] if len(sys.argv) > 1 else "lbp1120-test.pbm"
ROW_BYTES = WIDTH // 8

def make_row(y: int) -> bytes:
    row = bytearray(ROW_BYTES)
    # Page frame and calibration rules.
    if y < 32 or y >= HEIGHT - 32:
        row[:] = b"\xff" * ROW_BYTES
    else:
        for i in range(5):
            row[i] = 0xff
            row[ROW_BYTES - 1 - i] = 0xff
        if y % 500 < 4:
            row[:] = b"\xff" * ROW_BYTES
        # Large gray-like bars rendered as alternating black/white lines.
        if 800 <= y < 2400:
            for i in range(70, 550):
                if ((i + y) % 2) == 0:
                    row[i] = 0xaa
                else:
                    row[i] = 0x55
        # Vertical calibration bars near the center.
        if 3000 <= y < 4300:
            for i in range(100, 520):
                if i % 32 < 16:
                    row[i] = 0xff
        # Checkerboard at the bottom for resolution inspection.
        if 4700 <= y < 6100:
            for i in range(80, 540):
                row[i] = 0xff if ((i // 8 + y // 8) % 2 == 0) else 0x00
    return bytes(row)

with open(OUT, "wb") as f:
    f.write(f"P4\n{WIDTH} {HEIGHT}\n".encode("ascii"))
    for y in range(HEIGHT):
        f.write(make_row(y))
print(f"Wrote {OUT}: {WIDTH}x{HEIGHT} PBM, 600-dpi A4 raster")
