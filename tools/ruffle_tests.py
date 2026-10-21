#!/usr/bin/env python3
"""Runs Ruffle's SWF regression tests through Phoenix's Test Movie.

Ruffle (https://github.com/ruffle-rs/ruffle, MIT/Apache-2.0) keeps thousands
of small SWFs under tests/tests/swfs, each with a test.toml saying how many
frames to run, an output.txt of what Flash Player traced, and for some an
expected image of the last frame. This plays each with `Phoenix play`,
compares what it traces with output.txt and what it draws with the expected
images, and prints a summary by folder. report.json in the output folder has
every result; failing traces and images are kept beside it.

    python tools/ruffle_tests.py --ruffle D:/src/ruffle --out D:/tmp/ruffle-run
    python tools/ruffle_tests.py avm1/ visual/ --baseline D:/tmp/ruffle-run/report.json

Positional arguments pick tests whose path (relative to tests/tests/swfs)
contains any of them. --baseline compares with an earlier report.json and
lists what newly fails and newly passes; it exits 1 when anything newly
fails, so a run before and after a change shows what the change broke.

Rendering runs headless (QT_QPA_PLATFORM=offscreen); nothing appears on
screen.
"""

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import time
import tomllib

try:
    import numpy
    from PIL import Image
except ImportError:  # images are then not compared
    numpy = None


def find_tests(root, patterns):
    tests = []
    for folder, dirs, files in os.walk(root):
        dirs.sort()
        if "test.toml" in files and "test.swf" in files:
            name = os.path.relpath(folder, root).replace("\\", "/")
            if not patterns or any(p in name for p in patterns):
                tests.append(name)
    return tests


def skip_reason(config, folder):
    if config.get("ignore"):
        return "ignored by Ruffle"
    condition = config.get("filter")
    if condition and "windows" not in condition:
        return "not for Windows"
    features = config.get("required_features", {})
    if features.get("jpegxr"):
        return "needs JPEG XR"
    if config.get("player_options", {}).get("runtime") == "AIR":
        return "AIR only"
    if os.path.exists(os.path.join(folder, "input.json")):
        return "plays recorded input"
    return None


def play(phoenix, swf, frames, image, timeout, env):
    command = [phoenix, "play", swf, "--frames", str(frames)]
    if image:
        command += ["--image", image]
    try:
        done = subprocess.run(command, capture_output=True, timeout=timeout, env=env,
                              cwd=os.path.dirname(swf))
    except subprocess.TimeoutExpired:
        return None, "timeout"
    return done.stdout.decode("utf-8", "replace"), done.returncode


PLAY_REPORT = re.compile(r"^(after \d+ frames: |frame \d+ drawn to |\d+ errors$|getURL |sound |fscommand\()")


def normalized(text, play_output=False):
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    # What `play` says itself, not what the movie traced.
    if play_output:
        lines = [line for line in lines if not PLAY_REPORT.match(line)]
    while lines and not lines[-1].strip():
        lines.pop()
    return lines


def first_difference(expected, actual):
    for i in range(max(len(expected), len(actual))):
        a = expected[i] if i < len(expected) else "<end>"
        b = actual[i] if i < len(actual) else "<end>"
        if a != b:
            return {"line": i + 1, "expected": a, "actual": b}
    return None


def compare_images(expected_path, actual_path, tolerance, max_outliers):
    """'pass' as Ruffle's own check counts it, 'near' when under 1% of the
    pixels are off by more than 16, else 'fail'; and that fraction."""
    if numpy is None:
        return "unchecked", None
    if not os.path.exists(actual_path):
        return "no image", None
    a = numpy.asarray(Image.open(expected_path).convert("RGBA"), dtype=numpy.int16)
    b = numpy.asarray(Image.open(actual_path).convert("RGBA"), dtype=numpy.int16)
    # Taken at a scale factor: brought down to the movie's size.
    if a.shape != b.shape and a.shape[0] * b.shape[1] == a.shape[1] * b.shape[0]:
        a = numpy.asarray(Image.open(expected_path).convert("RGBA").resize((b.shape[1], b.shape[0]), Image.BOX),
                          dtype=numpy.int16)
    if a.shape != b.shape:
        return "size %dx%d, expected %dx%d" % (b.shape[1], b.shape[0], a.shape[1], a.shape[0]), None
    difference = numpy.abs(a - b)
    outliers = int((difference > tolerance).sum())
    off = float((difference.max(axis=2) > 16).mean())
    if outliers <= max_outliers:
        return "pass", off
    return ("near" if off < 0.01 else "fail"), off


def run_test(args, name, worker_env):
    folder = os.path.join(args.tests, name)
    started = time.time()
    result = {"test": name}
    try:
        with open(os.path.join(folder, "test.toml"), "rb") as f:
            config = tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError) as error:
        result["skipped"] = "test.toml: %s" % error
        return result
    reason = skip_reason(config, folder)
    if reason:
        result["skipped"] = reason
        return result
    if config.get("known_failure"):
        result["ruffle_known_failure"] = True
    frames = max(1, int(config.get("num_frames", config.get("num_ticks", 1))))
    shutil.rmtree(os.path.join(worker_env["PHOENIX_SETTINGS_DIR"], "SharedObjects"), ignore_errors=True)
    swf = os.path.join(folder, "test.swf")
    out = os.path.join(args.out, "results", name)
    os.makedirs(out, exist_ok=True)

    # Image comparisons by the frame they are taken on.
    shots = {}
    for shot, settings in config.get("image_comparisons", {}).items():
        trigger = settings.get("trigger", "last_frame")
        if trigger == "fs_command":
            continue
        at = frames if trigger == "last_frame" else int(trigger)
        expected = os.path.join(folder, shot + ".expected.png")
        if os.path.exists(expected):
            shots.setdefault(at, []).append((shot, settings, expected))

    images = {}
    for at in sorted(shots):
        if at == frames:
            continue
        image = os.path.join(out, "frame%d.png" % at)
        play(args.phoenix, swf, at, image, args.timeout, worker_env)
        for shot, settings, expected in shots[at]:
            images[shot] = compare_images(expected, image, settings.get("tolerance", 0),
                                          settings.get("max_outliers", 0))

    last = os.path.join(out, "frame%d.png" % frames) if frames in shots else None
    trace, code = play(args.phoenix, swf, frames, last, args.timeout, worker_env)
    for shot, settings, expected in shots.get(frames, []):
        images[shot] = compare_images(expected, last, settings.get("tolerance", 0), settings.get("max_outliers", 0))
    if images:
        result["images"] = {k: v[0] for k, v in images.items()}

    expected_path = os.path.join(folder, config.get("output_path", "output.txt"))
    if code == "timeout":
        result["trace"] = "timeout"
    elif os.path.exists(expected_path):
        with open(expected_path, encoding="utf-8", errors="replace") as f:
            expected = normalized(f.read())
        actual = normalized(trace, True)
        difference = first_difference(expected, actual)
        if difference:
            result["trace"] = "fail"
            result["difference"] = difference
            with open(os.path.join(out, "actual.txt"), "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(actual) + "\n")
        else:
            result["trace"] = "pass"
    result["seconds"] = round(time.time() - started, 2)
    # Nothing kept for a test that passes throughout.
    if result.get("trace") != "fail" and all(v == "pass" for v in result.get("images", {}).values()):
        for each in os.listdir(out):
            os.remove(os.path.join(out, each))
        os.rmdir(out)
    return result


def passed(result):
    """Whether a result passes as a whole: its trace, and its images at
    least near."""
    if "skipped" in result:
        return None
    if result.get("trace") not in (None, "pass"):
        return False
    return all(v in ("pass", "near") for v in result.get("images", {}).values())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("patterns", nargs="*", help="run only tests whose path contains one of these")
    parser.add_argument("--ruffle", default=os.environ.get("RUFFLE_SOURCE", "D:/src/ruffle"),
                        help="Ruffle's source folder (RUFFLE_SOURCE)")
    parser.add_argument("--phoenix", default=os.path.join(os.path.dirname(__file__), "..", "build", "Release",
                                                          "Phoenix.exe"))
    parser.add_argument("--out", default=os.path.join(os.environ.get("TEMP", "."), "phoenix-ruffle-tests"))
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds each play may take")
    parser.add_argument("--baseline", help="an earlier report.json to compare with")
    args = parser.parse_args()
    args.tests = os.path.join(args.ruffle, "tests", "tests", "swfs")
    args.phoenix = os.path.abspath(args.phoenix)
    args.out = os.path.abspath(args.out)
    if not os.path.isdir(args.tests):
        sys.exit("No Ruffle tests at %s (clone https://github.com/ruffle-rs/ruffle there, or pass --ruffle)"
                 % args.tests)
    tests = find_tests(args.tests, args.patterns)
    print("%d tests, %d at a time" % (len(tests), args.jobs), flush=True)

    # Each worker its own TEMP, as Phoenix keeps files there.
    environments = []
    for n in range(args.jobs):
        temp = os.path.join(args.out, "temp", str(n))
        os.makedirs(temp, exist_ok=True)
        # And its own settings, where SharedObjects are kept: one test's
        # are not another's, and none are left with yours.
        settings = os.path.join(args.out, "settings", str(n))
        os.makedirs(settings, exist_ok=True)
        environments.append(dict(os.environ, QT_QPA_PLATFORM="offscreen", TEMP=temp, TMP=temp,
                                 PHOENIX_SETTINGS_DIR=settings))
    free = list(environments)

    def task(name):
        env = free.pop()
        try:
            return run_test(args, name, env)
        finally:
            free.append(env)

    results = []
    started = time.time()
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for i, result in enumerate(pool.map(task, tests)):
            results.append(result)
            if (i + 1) % 100 == 0:
                print("  %d/%d, %.0fs" % (i + 1, len(tests), time.time() - started), flush=True)

    # By the folder each test is in.
    summary = {}
    for result in results:
        group = result["test"].split("/")[0]
        counts = summary.setdefault(group, {"pass": 0, "fail": 0, "skipped": 0})
        verdict = passed(result)
        counts["skipped" if verdict is None else "pass" if verdict else "fail"] += 1
    print("\n%-14s %6s %6s %8s" % ("", "pass", "fail", "skipped"))
    for group, counts in sorted(summary.items()):
        print("%-14s %6d %6d %8d" % (group, counts["pass"], counts["fail"], counts["skipped"]))
    total = {k: sum(c[k] for c in summary.values()) for k in ("pass", "fail", "skipped")}
    print("%-14s %6d %6d %8d" % ("all", total["pass"], total["fail"], total["skipped"]))

    report = os.path.join(args.out, "report.json")
    with open(report, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"results": results, "summary": summary}, f, indent=1)
    print("\nReport: %s" % report)

    if args.baseline:
        with open(args.baseline, encoding="utf-8") as f:
            before = {r["test"]: passed(r) for r in json.load(f)["results"]}
        broke = [r["test"] for r in results if before.get(r["test"]) is True and passed(r) is False]
        fixed = [r["test"] for r in results if before.get(r["test"]) is False and passed(r) is True]
        print("\nNewly failing (%d):" % len(broke))
        for name in broke:
            print("  " + name)
        print("Newly passing (%d):" % len(fixed))
        for name in fixed:
            print("  " + name)
        return 1 if broke else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
