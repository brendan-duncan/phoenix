#!/usr/bin/env python3
"""Compares how Phoenix and Ruffle draw SWF movies, to find import bugs.

For each SWF (files, or folders searched for *.swf), renders sample frames
with Ruffle's exporter and with Phoenix, and scores how much they differ: the
share of pixels that differ clearly, once differences along edges (where the
two antialias differently) are discounted. Prints the movies that differ
most; the output folder gets report.json and, for each frame over
--threshold, a picture of Ruffle's frame, Phoenix's and where they differ.

    python tools/ruffle_compare.py "D:/CN/Documents/Fosters" --out D:/tmp/compare
    python tools/ruffle_compare.py movie.swf --frames 1,12,50% --mode play

Phoenix draws the imported timeline (--export-image) unless --mode play,
which runs the movie's scripts as Test Movie does; Ruffle always runs them,
so script-driven movies compare best with --mode play. Ruffle is a reference,
not the truth: a difference is a lead to look at.

Ruffle's exporter is built from its source (https://github.com/ruffle-rs/ruffle):
    cargo build --release -p exporter
and found at RUFFLE_EXPORTER, or target/release/exporter.exe under
RUFFLE_SOURCE (default D:/src/ruffle).
"""

import argparse
import concurrent.futures
import json
import lzma
import os
import struct
import subprocess
import sys
import time
import zlib

import numpy
from PIL import Image, ImageFilter


def frame_count(path):
    """The frame count in an SWF's header, or 1 when it cannot be read."""
    try:
        with open(path, "rb") as f:
            data = f.read(64 * 1024)
        signature = data[:3]
        if signature == b"FWS":
            body = data[8:]
        elif signature == b"CWS":
            body = zlib.decompressobj().decompress(data[8:], 64)
        elif signature == b"ZWS":
            # LZMA: compressed length, five property bytes, then the data.
            props = data[12:17]
            d = props[0]
            filters = [{"id": lzma.FILTER_LZMA1, "lc": d % 9, "lp": (d // 9) % 5, "pb": d // 45,
                        "dict_size": struct.unpack("<I", props[1:5])[0]}]
            body = lzma.LZMADecompressor(lzma.FORMAT_RAW, filters=filters).decompress(data[17:], 64)
        else:
            return 1
        bits = body[0] >> 3
        offset = (5 + 4 * bits + 7) // 8 + 2
        return max(1, struct.unpack_from("<H", body, offset)[0])
    except (OSError, zlib.error, lzma.LZMAError, struct.error, IndexError):
        return 1


def sample_frames(spec, count):
    """Frame numbers (1-based) from a list such as "1,12,50%,last"."""
    frames = []
    for part in spec.split(","):
        part = part.strip().lower()
        if not part:
            continue
        if part == "last":
            n = count
        elif part.endswith("%"):
            n = 1 + round((count - 1) * float(part[:-1]) / 100.0)
        else:
            n = int(part)
        n = min(max(1, n), count)
        if n not in frames:
            frames.append(n)
    return frames


def difference(ruffle_png, phoenix_png):
    """The share of pixels that differ clearly away from edges, and a
    picture of where."""
    a = Image.open(ruffle_png).convert("RGB")
    b = Image.open(phoenix_png).convert("RGB")
    if a.size != b.size:
        b = b.resize(a.size, Image.BILINEAR)
    x = numpy.asarray(a, dtype=numpy.int16)
    y = numpy.asarray(b, dtype=numpy.int16)
    off = (numpy.abs(x - y).max(axis=2) > 48).astype(numpy.uint8) * 255
    # A difference only along an edge is antialiasing; three pixels across
    # or more is not.
    solid = numpy.asarray(Image.fromarray(off).filter(ImageFilter.MinFilter(3)))
    return float((solid > 0).mean()), a, b, solid


def side_by_side(a, b, solid, path):
    w, h = a.size
    highlight = numpy.asarray(a.convert("L").convert("RGB"), dtype=numpy.uint8) // 3 + 160
    highlight[solid > 0] = (255, 0, 160)
    out = Image.new("RGB", (w * 3 + 20, h + 18), (40, 40, 40))
    out.paste(a, (0, 18))
    out.paste(b, (w + 10, 18))
    out.paste(Image.fromarray(highlight), (2 * w + 20, 18))
    try:
        from PIL import ImageDraw
        draw = ImageDraw.Draw(out)
        for i, label in enumerate(("Ruffle", "Phoenix", "Differs")):
            draw.text((i * (w + 10) + 4, 3), label, fill=(230, 230, 230))
    except ImportError:
        pass
    out.save(path)


def render_ruffle(exporter, swf, frame, out, timeout):
    command = [exporter, swf, out, "--skipframes", str(frame - 1), "--frames", "1", "--silent"]
    subprocess.run(command, capture_output=True, timeout=timeout)
    return os.path.exists(out)


def render_phoenix(phoenix, mode, swf, frame, out, timeout, env):
    if mode == "play":
        command = [phoenix, "play", swf, "--frames", str(frame), "--image", out]
    else:
        command = [phoenix, "--export-image", swf, out, "--frame", str(frame)]
    subprocess.run(command, capture_output=True, timeout=timeout, env=env)
    return os.path.exists(out)


def compare(args, swf, env):
    count = frame_count(swf)
    name = os.path.splitext(os.path.basename(swf))[0]
    folder = os.path.join(args.out, "frames", "%s-%08x" % (name, abs(hash(swf)) & 0xFFFFFFFF))
    os.makedirs(folder, exist_ok=True)
    result = {"swf": swf, "frames": count, "samples": []}
    started = time.time()
    for frame in sample_frames(args.frames, count):
        sample = {"frame": frame}
        ruffle_png = os.path.join(folder, "ruffle%d.png" % frame)
        phoenix_png = os.path.join(folder, "phoenix%d.png" % frame)
        try:
            if not render_ruffle(args.ruffle, swf, frame, ruffle_png, args.timeout):
                sample["error"] = "Ruffle drew nothing"
            elif not render_phoenix(args.phoenix, args.mode, swf, frame, phoenix_png, args.timeout, env):
                sample["error"] = "Phoenix drew nothing"
        except subprocess.TimeoutExpired:
            sample["error"] = "timeout"
        if "error" not in sample:
            score, a, b, solid = difference(ruffle_png, phoenix_png)
            sample["differs"] = round(score, 5)
            if score > args.threshold:
                sample["picture"] = os.path.join(folder, "compare%d.png" % frame)
                side_by_side(a, b, solid, sample["picture"])
        for each in (ruffle_png, phoenix_png):
            if os.path.exists(each):
                os.remove(each)
        result["samples"].append(sample)
    scores = [s["differs"] for s in result["samples"] if "differs" in s]
    result["worst"] = max(scores) if scores else None
    result["seconds"] = round(time.time() - started, 1)
    if not os.listdir(folder):
        os.rmdir(folder)
    return result


def find_swfs(inputs):
    found = []
    for each in inputs:
        if os.path.isdir(each):
            for folder, dirs, files in os.walk(each):
                dirs.sort()
                found += [os.path.join(folder, f) for f in sorted(files) if f.lower().endswith(".swf")]
        elif os.path.isfile(each):
            found.append(each)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="SWF files, or folders to search for them")
    source = os.environ.get("RUFFLE_SOURCE", "D:/src/ruffle")
    parser.add_argument("--ruffle", default=os.environ.get(
        "RUFFLE_EXPORTER", os.path.join(source, "target", "release", "exporter.exe")))
    parser.add_argument("--phoenix", default=os.path.join(os.path.dirname(__file__), "..", "build", "Release",
                                                          "Phoenix.exe"))
    parser.add_argument("--mode", choices=("timeline", "play"), default="timeline")
    parser.add_argument("--frames", default="1,25%,50%,75%", help="frames to sample: numbers, percents, last")
    parser.add_argument("--threshold", type=float, default=0.002,
                        help="share of differing pixels above which a picture is kept")
    parser.add_argument("--out", default=os.path.join(os.environ.get("TEMP", "."), "phoenix-ruffle-compare"))
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 4))
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--top", type=int, default=30, help="how many of the worst to list")
    args = parser.parse_args()
    args.phoenix = os.path.abspath(args.phoenix)
    args.out = os.path.abspath(args.out)
    if not os.path.exists(args.ruffle):
        sys.exit("No Ruffle exporter at %s; build it with `cargo build --release -p exporter` in Ruffle's source, "
                 "or pass --ruffle" % args.ruffle)
    swfs = find_swfs(args.inputs)
    print("%d movies, %d at a time" % (len(swfs), args.jobs), flush=True)

    environments = []
    for n in range(args.jobs):
        temp = os.path.join(args.out, "temp", str(n))
        os.makedirs(temp, exist_ok=True)
        environments.append(dict(os.environ, QT_QPA_PLATFORM="offscreen", TEMP=temp, TMP=temp))
    free = list(environments)

    def task(swf):
        env = free.pop()
        try:
            return compare(args, swf, env)
        finally:
            free.append(env)

    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for i, result in enumerate(pool.map(task, swfs)):
            results.append(result)
            worst = result["worst"]
            print("  %d/%d %s %s" % (i + 1, len(swfs), "-" if worst is None else "%.4f" % worst,
                                     os.path.basename(result["swf"])), flush=True)

    with open(os.path.join(args.out, "report.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(results, f, indent=1)
    ranked = sorted((r for r in results if r["worst"] is not None), key=lambda r: -r["worst"])
    print("\nDiffer most (share of pixels, at the worst sampled frame):")
    for r in ranked[:args.top]:
        sample = max((s for s in r["samples"] if "differs" in s), key=lambda s: s["differs"])
        print("  %.4f  frame %-5d %s" % (r["worst"], sample["frame"], r["swf"]))
        if "picture" in sample:
            print("          %s" % sample["picture"])
    failed = [r for r in results if any("error" in s for s in r["samples"])]
    if failed:
        print("\nCould not compare (%d):" % len(failed))
        for r in failed:
            print("  %s: %s" % (r["swf"], "; ".join(sorted({s["error"] for s in r["samples"] if "error" in s}))))
    print("\nReport: %s" % os.path.join(args.out, "report.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
