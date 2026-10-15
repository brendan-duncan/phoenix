#!/usr/bin/env python3
"""Checks that a frame looks the same however Phoenix draws it.

For each document and frame, renders the frame three ways and compares the
pixels:

- Phoenix --export-image (a still, as File > Export > Export Image)
- Phoenix --export-movie (a PNG sequence, as File > Export > Export Movie)
- Phoenix play --image (the movie played as Control > Test Movie plays it)

Behaviors, shape operators, letter animators, mattes and adjustment layers
are worked out every frame, so this is where they would drift apart.

Usage:
    python tools/consistency_check.py --exe build/Release/Phoenix.exe doc.fla [doc2.xfl ...] [--frames 1,12,30]

Exits 1 when any frame differs by more than the tolerance (--tolerance, the
largest channel difference allowed, 2 by default for antialiasing noise).
"""
import argparse
import os
import subprocess
import sys
import tempfile

try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit('consistency_check needs Pillow: pip install pillow')


def run(exe, args, env):
    result = subprocess.run([exe] + args, env=env, capture_output=True, text=True, timeout=600)
    if result.returncode != 0:
        raise RuntimeError('%s failed: %s' % (' '.join(args[:2]), (result.stderr or result.stdout)[-400:]))


def largest_difference(a, b):
    if a.size != b.size:
        return 255
    return max(high for low, high in ImageChops.difference(a.convert('RGBA'), b.convert('RGBA')).getextrema())


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('documents', nargs='+')
    parser.add_argument('--exe', default='build/Release/Phoenix.exe')
    parser.add_argument('--frames', default='1,6,12,24')
    parser.add_argument('--tolerance', type=int, default=2)
    options = parser.parse_args()
    frames = [int(f) for f in options.frames.split(',')]
    env = dict(os.environ)
    env.setdefault('QT_QPA_PLATFORM', 'offscreen')

    failed = False
    for document in options.documents:
        with tempfile.TemporaryDirectory() as folder:
            run(options.exe, ['--export-movie', document, os.path.join(folder, 'movie.png')], env)
            for frame in frames:
                still = os.path.join(folder, 'still%d.png' % frame)
                played = os.path.join(folder, 'played%d.png' % frame)
                run(options.exe, ['--export-image', document, still, '--frame', str(frame)], env)
                run(options.exe, ['play', document, '--frames', str(frame - 1), '--image', played], env)
                movie = os.path.join(folder, 'movie%04d.png' % frame)
                if not os.path.exists(movie):
                    print('%s frame %d: past the end of the movie' % (document, frame))
                    continue
                images = {name: Image.open(path) for name, path in
                          (('still', still), ('movie', movie), ('played', played))}
                worst = max(largest_difference(images['still'], images['movie']),
                            largest_difference(images['still'], images['played']))
                ok = worst <= options.tolerance
                failed = failed or not ok
                print('%s frame %d: %s (largest difference %d)' % (document, frame, 'same' if ok else 'DIFFERENT', worst))
                if not ok:
                    for name, image in images.items():
                        image.save('%s.frame%d.%s.png' % (os.path.splitext(os.path.basename(document))[0], frame, name))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
