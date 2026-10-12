#!/usr/bin/env python3
"""Round-trip check: does every file Phoenix saves draw as the original did?

For each .fla (or .phx) in a folder, runs `Phoenix --roundtrip`, which saves
the document as .fla and as .phx, opens each copy again and compares sample
frames with the original's pixel by pixel. Prints a table, writes
report.json, and keeps images of anything that differs in the output folder.
Exits 1 when any file differs or fails.

    python tools/roundtrip_check.py D:/fla --phoenix build/Release/Phoenix.exe

Rendering runs headless (QT_QPA_PLATFORM=offscreen); nothing appears on
screen. Set the Qt platform plugin folder with --plugins when Phoenix is not
next to its plugins.
"""

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
import tempfile
import time


def run_one(phoenix, source, out_dir, samples, timeout, env):
    started = time.time()
    folder = os.path.join(out_dir, os.path.splitext(os.path.basename(source))[0])
    try:
        done = subprocess.run([phoenix, '--roundtrip', source, folder, str(samples)],
                              capture_output=True, text=True, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return {'file': source, 'error': 'timed out after %d s' % timeout, 'seconds': timeout}
    report = None
    for line in done.stdout.splitlines():
        line = line.strip()
        if line.startswith('{'):
            try:
                report = json.loads(line)
            except ValueError:
                pass
    if report is None:
        report = {'file': source, 'error': 'no report (exit %d): %s' % (done.returncode, done.stderr.strip()[-300:])}
    report['seconds'] = round(time.time() - started, 1)
    return report


def describe(result):
    if not isinstance(result, dict):
        return '-'
    if 'error' in result:
        return 'ERROR ' + result['error']
    if result.get('pixelsChanged', 0) == 0:
        return 'same'
    return '%d px (max %d) frames %s' % (result['pixelsChanged'], result['maxDifference'],
                                          ','.join(str(f + 1) for f in result['framesChanged']))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('corpus', help='folder of .fla/.phx files')
    parser.add_argument('--phoenix', default=os.path.join('build', 'Release', 'Phoenix.exe'))
    parser.add_argument('--out', default=os.path.join(tempfile.gettempdir(), 'phoenix-roundtrip'))
    parser.add_argument('--samples', type=int, default=5, help='frames compared per file')
    parser.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--plugins', help='Qt platforms plugin folder, if not beside Phoenix')
    parser.add_argument('--match', default='', help='only files whose name contains this')
    args = parser.parse_args()

    files = sorted(os.path.join(args.corpus, name) for name in os.listdir(args.corpus)
                   if name.lower().endswith(('.fla', '.phx')) and args.match.lower() in name.lower())
    if not files:
        print('No .fla or .phx files in', args.corpus)
        return 1
    args.phoenix = os.path.abspath(args.phoenix)
    if not os.path.exists(args.phoenix):
        print('No Phoenix at', args.phoenix)
        return 1
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    if args.plugins:
        env['QT_QPA_PLATFORM_PLUGIN_PATH'] = args.plugins
    os.makedirs(args.out, exist_ok=True)

    print('Checking %d files with %d jobs; images of differences in %s' % (len(files), args.jobs, args.out))
    reports = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_one, args.phoenix, f, args.out, args.samples, args.timeout, env) for f in files]
        for future in concurrent.futures.as_completed(futures):
            report = future.result()
            reports.append(report)
            name = os.path.basename(report['file'])
            if 'error' in report:
                print('  %-45s ERROR %s' % (name, report['error']))
            else:
                print('  %-45s fla: %-30s phx: %s' % (name, describe(report.get('fla')), describe(report.get('phx'))))
            sys.stdout.flush()

    reports.sort(key=lambda r: r['file'])
    bad = [r for r in reports if 'error' in r or any(
        isinstance(r.get(k), dict) and ('error' in r[k] or r[k].get('pixelsChanged', 0) > 0) for k in ('fla', 'phx'))]
    with open(os.path.join(args.out, 'report.json'), 'w', encoding='utf-8') as f:
        json.dump(reports, f, indent=1)
    print()
    print('%d of %d files draw the same after saving as .fla and .phx.' % (len(reports) - len(bad), len(reports)))
    for r in bad:
        print('  differs:', os.path.basename(r['file']))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
