"""Classify a Banjo PC capture and score the title screen's foliage.

  python tools/pc/grass_score.py <png> [<png> ...]

Prints per image: title (the BANJO logo is in the middle band), lower_black
(black fraction of the lower half), gray (bright desaturated noise), and
the verdict BAD when a title-screen capture has black grass or gray
foliage. Canary Vulkan and the fork's D3D12 score under 0.06 on both; the
fork's Vulkan scores 0.25 to 0.87 (2026-09-21). Exit 1 when any image is
BAD, 2 when none is a title screen, else 0.
"""
import sys

from PIL import Image


def stats(path):
    im = Image.open(path).convert('RGB').resize((320, 180))
    px = list(im.getdata())
    w, h = 320, 180
    red = blue = 0
    for y in range(int(h * 0.25), int(h * 0.55)):
        for x in range(w):
            r, g, b = px[y * w + x]
            if r > 150 and g < 90 and b < 90:
                red += 1
            if b > 140 and r < 90 and g < 140:
                blue += 1
    band = w * (int(h * 0.55) - int(h * 0.25))
    title = red / band > 0.01 and blue / band > 0.01
    lower = [px[y * w + x] for y in range(h // 2, h) for x in range(w)]
    black = sum(1 for r, g, b in lower if max(r, g, b) < 12) / len(lower)
    gray = sum(1 for r, g, b in lower if max(r, g, b) > 120 and max(r, g, b) - min(r, g, b) < 40) / len(lower)
    return title, black, gray


def main():
    exit_code = 2
    for path in sys.argv[1:]:
        title, black, gray = stats(path)
        bad = title and (black > 0.15 or gray > 0.30)
        print('%s title=%s lower_black=%.3f gray=%.3f %s' % (path, title, black, gray, 'BAD' if bad else ('ok' if title else 'not-title')))
        if title and exit_code == 2:
            exit_code = 0
        if bad:
            exit_code = 1
    return exit_code


if __name__ == '__main__':
    sys.exit(main())
