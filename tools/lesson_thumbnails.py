"""Makes each built-in lesson's thumbnail: its final document, rendered with
no window, as a small looping GIF (thumbnail.gif in the lesson's folder),
which the Tutorials window shows beside the lesson and plays when it is
picked.

    python tools/lesson_thumbnails.py [path/to/Phoenix.exe] [lesson-id ...]

A lesson with no final keeps a thumbnail made by hand (thumbnail.png or
thumbnail.gif), if it has one. Needs Pillow.
"""

import json
import re
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LESSONS = os.path.join(ROOT, 'resources', 'tutorials')
SIZE = (240, 135)
MOST_FRAMES = 36
LONGEST_SECONDS = 4.0
COLORS = 128


def phoenix_exe(argv):
    for arg in argv:
        if arg.lower().endswith('.exe') or os.path.basename(arg) == 'Phoenix':
            return os.path.abspath(arg)
    for guess in ('build/Release/Phoenix.exe', 'build/Phoenix.app/Contents/MacOS/Phoenix', 'build/Phoenix'):
        path = os.path.join(ROOT, guess)
        if os.path.exists(path):
            return path
    sys.exit('Where is Phoenix? Give its path.')


def final_of(folder, lesson):
    final = lesson.get('final')
    if not final:
        return None
    path = os.path.join(folder, final)
    if os.path.isdir(path):
        path = os.path.join(path, 'DOMDocument.xml')
    return path if os.path.exists(path) else None


def fitted(frame):
    """The frame scaled to fit the thumbnail, centered on its background."""
    frame = frame.convert('RGB')
    scale = min(SIZE[0] / frame.width, SIZE[1] / frame.height)
    size = (max(1, round(frame.width * scale)), max(1, round(frame.height * scale)))
    small = frame.resize(size, Image.LANCZOS)
    out = Image.new('RGB', SIZE, frame.getpixel((0, 0)))
    out.paste(small, ((SIZE[0] - size[0]) // 2, (SIZE[1] - size[1]) // 2))
    return out


def render(exe, document, fps):
    work = tempfile.mkdtemp(prefix='phoenix-thumb-')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    qt = os.environ.get('QT_PLUGIN_PLATFORMS')
    if qt:
        env['QT_QPA_PLATFORM_PLUGIN_PATH'] = qt
    try:
        subprocess.run([exe, '--export-movie', document, os.path.join(work, 'f.png')], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600, check=False)
        names = sorted(n for n in os.listdir(work) if n.endswith('.png'))
        if not names:
            return []
        # At most a few seconds, at most so many frames, evenly spread.
        names = names[:max(1, int(LONGEST_SECONDS * fps))]
        step = max(1, -(-len(names) // MOST_FRAMES))
        frames = [fitted(Image.open(os.path.join(work, n))) for n in names[::step]]
        return frames, step / fps
    finally:
        shutil.rmtree(work, ignore_errors=True)


def save_gif(frames, seconds_each, path):
    # One palette for all the frames, so colors hold still as it plays.
    sheet = Image.new('RGB', (SIZE[0], SIZE[1] * len(frames)))
    for i, frame in enumerate(frames):
        sheet.paste(frame, (0, SIZE[1] * i))
    palette = sheet.quantize(colors=COLORS, method=Image.MEDIANCUT)
    quantized = [frame.quantize(palette=palette, dither=Image.NONE) for frame in frames]
    quantized[0].save(path, save_all=True, append_images=quantized[1:], loop=0,
                      duration=max(20, round(seconds_each * 1000)), optimize=True, disposal=1)


def main():
    exe = phoenix_exe(sys.argv[1:])
    wanted = [a for a in sys.argv[1:] if os.path.abspath(a) != exe and not a.lower().endswith('.exe')]
    for name in sorted(os.listdir(LESSONS)):
        folder = os.path.join(LESSONS, name)
        if wanted and name not in wanted:
            continue
        try:
            lesson = json.load(open(os.path.join(folder, 'lesson.json'), encoding='utf-8'))
        except (OSError, ValueError):
            continue
        document = final_of(folder, lesson)
        if not document:
            print(f'{name}: no final, thumbnail left as it is')
            continue
        # The document's own frame rate.
        head = open(document, encoding='utf-8', errors='replace').read(4096)
        found = re.search(r'frameRate="([0-9.]+)"', head)
        fps = float(found.group(1)) if found else 24.0
        made = render(exe, document, fps)
        if not made:
            print(f'{name}: nothing rendered')
            continue
        frames, seconds = made
        path = os.path.join(folder, 'thumbnail.gif')
        save_gif(frames, seconds, path)
        print(f'{name}: {len(frames)} frames, {os.path.getsize(path) // 1024} KB')


if __name__ == '__main__':
    main()
