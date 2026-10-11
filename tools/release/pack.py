#!/usr/bin/env python3
"""Packs a built Phoenix into a Velopack release: the installer, the full
and delta update packages and the feed the app's updater reads.

  python tools/release/pack.py --version 0.2.0 [--build-dir build]
                               [--out releases] [--target windows-x64]

It installs the Release build's Phoenix component (Phoenix and the Qt it
needs, see CMakeLists.txt) into <out>/stage-<target> and runs vpk pack
over it. vpk must be installed, at the Velopack version the build pins
(cmake/PhoenixVelopack.cmake):

  dotnet tool install -g vpk --version <version>

For delta packages, <out> should first hold the previous release, which
`vpk download github` fetches; the release workflow does that, then uploads
what this makes with `vpk upload github`.

Only Windows is packed so far; macOS and Linux are still to come.
"""

import argparse
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Never change: the installed app is found by its id, and an app installed
# under one id is not updated by releases made under another.
PACK_ID = "Phoenix"
PACK_TITLE = "Phoenix"
PACK_AUTHORS = "Brendan Duncan"

# Per target: the Velopack channel (each has its own feed), the runtime it
# is built for, and on Windows the Visual C++ runtime that Setup installs
# when it is missing.
TARGETS = {
    "windows-x64": {"channel": "win-x64", "runtime": "win-x64", "framework": "vcredist143-x64"},
    "windows-arm64": {"channel": "win-arm64", "runtime": "win-arm64", "framework": "vcredist143-arm64"},
}


def host_target():
    machine = platform.machine().lower()
    if sys.platform == "win32":
        return "windows-arm64" if machine in ("arm64", "aarch64") else "windows-x64"
    sys.exit("Packing is only done for Windows so far")


def run(cmd):
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True, help="the release's version, e.g. 0.2.0")
    parser.add_argument("--build-dir", default="build", help="the configured and built build folder")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--out", default="releases", help="where the release is written")
    parser.add_argument("--target", choices=sorted(TARGETS), default=None)
    args = parser.parse_args()

    if not re.match(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$", args.version):
        sys.exit(f"{args.version} is not a version such as 1.2.3 or 1.2.3-beta.1")

    target = args.target or host_target()
    settings = TARGETS[target]
    build_dir = Path(args.build_dir).resolve()
    out = Path(args.out).resolve()
    stage = out / f"stage-{target}"

    # The build must be the version being released, since the updater
    # compares the version it was built as against the feed.
    cache = (build_dir / "CMakeCache.txt").read_text(encoding="utf-8")
    built = re.search(r"^PHOENIX_VERSION:STRING=(.*)$", cache, re.M)
    if not built or built.group(1).strip() != args.version:
        sys.exit(f"{build_dir} is configured as {built.group(1) if built else 'no version'}, "
                 f"not {args.version}; configure it with -DPHOENIX_VERSION={args.version}")

    if stage.exists():
        shutil.rmtree(stage)
    run(["cmake", "--install", build_dir, "--config", args.config,
         "--component", "Phoenix", "--prefix", stage])

    run(["vpk", "pack",
         "--packId", PACK_ID,
         "--packVersion", args.version,
         "--packDir", stage,
         "--packTitle", PACK_TITLE,
         "--packAuthors", PACK_AUTHORS,
         "--mainExe", "Phoenix.exe",
         "--icon", ROOT / "src" / "phoenix.ico",
         "--framework", settings["framework"],
         "--channel", settings["channel"],
         "--runtime", settings["runtime"],
         "--outputDir", out])

    shutil.rmtree(stage)
    print(f"Packed Phoenix {args.version} for {target} into {out}")


if __name__ == "__main__":
    main()
