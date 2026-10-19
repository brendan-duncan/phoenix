"""Makes the synthetic paper scans that Import Paper Drawings is tested and
shown with: pencil drawings of a bouncing ball on tinted paper, lit
unevenly, with specks of dust, as a scanner or a phone would see them.

    python tools/make_paper_scans.py

writes tests/data/paper/scan_1.ppm .. scan_3.ppm (small, for the tests) and
website/assets/img/paper-scan-1.jpg .. paper-scan-4.jpg (for the guide).
Nothing is taken from real scans; the same seed makes the same files.
"""

import os
import random

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def scan(width, height, frame, seed):
    """One page: a ball over a ground line, the ball further along each
    frame, drawn in soft gray pencil on warm paper."""
    rng = random.Random(seed)
    scale = width / 160.0

    # The drawing, black on white, drawn large and shrunk for soft edges.
    big = 4
    art = Image.new('L', (width * big, height * big), 255)
    pen = ImageDraw.Draw(art)
    line = max(1, int(round(1.6 * scale * big)))
    ground = int(height * 0.82) * big
    pen.line([(int(width * 0.08) * big, ground), (int(width * 0.92) * big, ground)], fill=0, width=line)
    # The ball travels right and comes down to the ground.
    cx = (0.25 + 0.17 * frame) * width
    cy = (0.30 + 0.13 * frame) * height
    r = 0.13 * width
    if frame == 3:
        # Squashed on landing, resting on the ground.
        box = [cx - r * 1.2, ground / big - r * 1.6, cx + r * 1.2, ground / big]
    else:
        box = [cx - r, cy - r, cx + r, cy + r]
    pen.ellipse([v * big for v in box], outline=0, width=line)
    # A band across the ball, so it has two regions to paint.
    hy = (box[1] + box[3]) / 2
    pen.line([(box[0] * big, hy * big), (box[2] * big, hy * big)], fill=0, width=line)
    art = art.resize((width, height), Image.LANCZOS)

    # Pencil is gray, not black, and grainy.
    pencil = art.point(lambda v: 255 - int((255 - v) * 0.55))

    # Warm paper, darker toward one corner, with grain.
    page = Image.new('RGB', (width, height))
    pixels = page.load()
    shade = pencil.load()
    for y in range(height):
        for x in range(width):
            light = 1.0 - 0.22 * ((x / width) * 0.6 + (y / height) * 0.4)
            grain = rng.uniform(-6, 6)
            mark = (255 - shade[x, y]) * rng.uniform(0.85, 1.1)
            base = (246 * light + grain - mark, 238 * light + grain - mark, 214 * light + grain - mark)
            pixels[x, y] = tuple(max(0, min(255, int(v))) for v in base)

    # Dust: a few dark dots.
    dots = ImageDraw.Draw(page)
    for _ in range(int(6 * scale)):
        x = rng.randrange(width)
        y = rng.randrange(height)
        size = rng.choice([0, 0, 1])
        dots.ellipse([x, y, x + size, y + size], fill=(110, 105, 95))
    return page.filter(ImageFilter.GaussianBlur(0.3))


def main():
    tests = os.path.join(ROOT, 'tests', 'data', 'paper')
    os.makedirs(tests, exist_ok=True)
    for frame in range(3):
        page = scan(160, 120, frame, 100 + frame)
        page.save(os.path.join(tests, 'scan_%d.ppm' % (frame + 1)))
    docs = os.path.join(ROOT, 'website', 'assets', 'img')
    for frame in range(4):
        page = scan(480, 360, frame, 200 + frame)
        page.save(os.path.join(docs, 'paper-scan-%d.jpg' % (frame + 1)), quality=85)
    print('wrote', tests, 'and', docs)


if __name__ == '__main__':
    main()
