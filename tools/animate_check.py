#!/usr/bin/env python3
"""Does Animate open what Phoenix saves, and draw it as it drew the original?

Three steps, the middle one in Animate:

    python tools/animate_check.py prepare D:/fla --out D:/animate_check
        Saves every .fla in the folder through Phoenix (as Phoenix's own
        Save would) next to a copy of the original, and puts
        animate_check.jsfl and files.txt beside them.

    In Animate: Commands > Run Command, and pick
        D:/animate_check/animate_check.jsfl. It opens every file in turn,
        exports its first, middle and last frames, and writes animate.json.
        Watch for repair prompts; note any file that shows one.

    python tools/animate_check.py compare D:/animate_check
        Compares Animate's pictures of each original with its pictures of
        Phoenix's copy, and their frame, layer, scene and library counts.
        Prints a table, writes report.json, and exits 1 when any file
        differs or did not open.

--for-other-apps saves the copies with Export FLA for Other Apps instead.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

try:
    from PIL import Image, ImageChops
except ImportError:
    Image = None


def prepare(args):
    phoenix = os.path.abspath(args.phoenix)
    if not os.path.exists(phoenix):
        print('No Phoenix at', phoenix)
        return 1
    files = sorted(name for name in os.listdir(args.corpus)
                   if name.lower().endswith('.fla') and args.match.lower() in name.lower())
    if not files:
        print('No .fla files in', args.corpus)
        return 1
    os.makedirs(args.out, exist_ok=True)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    listed = []
    for name in files:
        stem = os.path.splitext(name)[0]
        original = stem + '.original.fla'
        copy = stem + '.phoenix.fla'
        shutil.copyfile(os.path.join(args.corpus, name), os.path.join(args.out, original))
        command = [phoenix, '--convert', os.path.join(args.corpus, name), os.path.join(args.out, copy)]
        if args.for_other_apps:
            command.append('--for-other-apps')
        try:
            done = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout, env=env)
            ok = done.returncode == 0 and os.path.exists(os.path.join(args.out, copy))
        except subprocess.TimeoutExpired:
            ok = False
        print('  %-50s %s' % (name, 'saved' if ok else 'PHOENIX COULD NOT SAVE IT'))
        listed.append(original)
        if ok:
            listed.append(copy)
    with open(os.path.join(args.out, 'files.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(listed) + '\n')
    shutil.copyfile(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'animate_check.jsfl'),
                    os.path.join(args.out, 'animate_check.jsfl'))
    print()
    print('Now in Animate: Commands > Run Command, and pick', os.path.join(args.out, 'animate_check.jsfl'))
    print('Then: python tools/animate_check.py compare', args.out)
    return 0


def largest_difference(a, b, tolerance):
    if a.size != b.size:
        return 255, a.size[0] * a.size[1]
    diff = ImageChops.difference(a.convert('RGBA'), b.convert('RGBA'))
    largest = max(high for low, high in diff.getextrema())
    histogram = diff.convert('L').histogram()
    return largest, sum(histogram[tolerance + 1:])


def compare(args):
    if Image is None:
        print('compare needs Pillow: pip install pillow')
        return 1
    path = os.path.join(args.folder, 'animate.json')
    if not os.path.exists(path):
        print('No animate.json in', args.folder, '- run animate_check.jsfl in Animate first.')
        return 1
    with open(path, encoding='utf-8') as f:
        found = {entry['file']: entry for entry in json.load(f)}
    reports = []
    for name, original in sorted(found.items()):
        if not name.endswith('.original.fla'):
            continue
        stem = name[:-len('.original.fla')]
        copy = found.get(stem + '.phoenix.fla')
        report = {'file': stem}
        if 'error' in original:
            report['error'] = 'the original did not open in Animate: ' + original['error']
        elif copy is None:
            report['error'] = 'Phoenix could not save it'
        elif 'error' in copy:
            report['error'] = 'Phoenix\'s copy did not open in Animate: ' + copy['error']
        else:
            counts = [key for key in ('frames', 'layers', 'scenes', 'libraryItems') if original.get(key) != copy.get(key)]
            if counts:
                report['counts'] = {key: [original.get(key), copy.get(key)] for key in counts}
            frames = []
            for frame in original.get('exported', []):
                a = os.path.join(args.folder, 'animate', '%s.original.%d.png' % (stem, frame))
                b = os.path.join(args.folder, 'animate', '%s.phoenix.%d.png' % (stem, frame))
                if not (os.path.exists(a) and os.path.exists(b)):
                    frames.append({'frame': frame, 'missing': True})
                    continue
                largest, pixels = largest_difference(Image.open(a), Image.open(b), args.tolerance)
                if largest > args.tolerance:
                    frames.append({'frame': frame, 'largest': largest, 'pixels': pixels})
            if frames:
                report['frames'] = frames
        reports.append(report)
        if 'error' in report:
            line = 'ERROR ' + report['error']
        elif 'counts' in report or 'frames' in report:
            parts = ['%s %s' % (key, '->'.join(str(v) for v in value)) for key, value in report.get('counts', {}).items()]
            parts += ['frame %d %s' % (f['frame'], 'missing' if f.get('missing') else '%d px (max %d)' % (f['pixels'], f['largest']))
                      for f in report.get('frames', [])]
            line = 'DIFFERS ' + ', '.join(parts)
        else:
            line = 'same'
        print('  %-50s %s' % (stem, line))
    with open(os.path.join(args.folder, 'report.json'), 'w', encoding='utf-8') as f:
        json.dump(reports, f, indent=1)
    bad = [r for r in reports if len(r) > 1]
    print()
    print('%d of %d files open in Animate and draw as the originals do.' % (len(reports) - len(bad), len(reports)))
    return 1 if bad else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    steps = parser.add_subparsers(dest='step', required=True)
    first = steps.add_parser('prepare', help='save the corpus through Phoenix for Animate to open')
    first.add_argument('corpus', help='folder of .fla files')
    first.add_argument('--out', required=True, help='folder for the originals, the copies and the script')
    first.add_argument('--phoenix', default=os.path.join('build', 'Release', 'Phoenix.exe'))
    first.add_argument('--match', default='', help='only files whose name contains this')
    first.add_argument('--for-other-apps', action='store_true', help='save with Export FLA for Other Apps')
    first.add_argument('--timeout', type=int, default=600)
    last = steps.add_parser('compare', help='compare what Animate exported')
    last.add_argument('folder', help='the folder prepare made, with animate.json in it')
    last.add_argument('--tolerance', type=int, default=8, help='largest channel difference allowed')
    args = parser.parse_args()
    return prepare(args) if args.step == 'prepare' else compare(args)


if __name__ == '__main__':
    sys.exit(main())
