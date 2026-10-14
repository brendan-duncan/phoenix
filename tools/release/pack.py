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

On macOS the app is signed with --sign-app-identity and the .pkg installer
with --sign-install-identity ("Developer ID Application: ..." and
"Developer ID Installer: ..."), and both are notarized when
--notary-profile names credentials stored with `xcrun notarytool
store-credentials`. Without them the release is unsigned, which macOS
refuses to open from a download.

On Linux the release is an AppImage, which runs on its own wherever it is
saved and updates itself as installed copies do elsewhere.

Windows, macOS (Apple silicon) and Linux (x64) are packed so far.
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
# is built for, what in the staged install is packed, the program in it,
# its icon, and on Windows the Visual C++ runtime that Setup installs when
# it is missing.
TARGETS = {
    "windows-x64": {
        "channel": "win-x64", "runtime": "win-x64", "pack_dir": ".", "main_exe": "Phoenix.exe",
        "icon": "src/phoenix.ico", "framework": "vcredist143-x64",
    },
    "windows-arm64": {
        "channel": "win-arm64", "runtime": "win-arm64", "pack_dir": ".", "main_exe": "Phoenix.exe",
        "icon": "src/phoenix.ico", "framework": "vcredist143-arm64",
    },
    "macos-arm64": {
        "channel": "osx-arm64", "runtime": "osx-arm64", "pack_dir": "Phoenix.app", "main_exe": "Phoenix",
        "icon": "src/phoenix.icns",
    },
    "linux-x64": {
        "channel": "linux-x64", "runtime": "linux-x64", "pack_dir": ".", "main_exe": "Phoenix",
        "icon": "src/phoenix.png",
    },
    "linux-arm64": {
        "channel": "linux-arm64", "runtime": "linux-arm64", "pack_dir": ".", "main_exe": "Phoenix",
        "icon": "src/phoenix.png",
    },
}


def host_target():
    machine = platform.machine().lower()
    if sys.platform == "win32":
        return "windows-arm64" if machine in ("arm64", "aarch64") else "windows-x64"
    if sys.platform == "darwin" and machine == "arm64":
        return "macos-arm64"
    if sys.platform.startswith("linux"):
        return "linux-arm64" if machine in ("arm64", "aarch64") else "linux-x64"
    sys.exit(f"Packing is not done for {sys.platform} {machine} yet")


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
    parser.add_argument("--sign-app-identity", help="macOS: the certificate that signs the app")
    parser.add_argument("--sign-install-identity", help="macOS: the certificate that signs the .pkg")
    parser.add_argument("--notary-profile", help="macOS: the notarytool credentials to notarize with")
    parser.add_argument("--keychain", help="macOS: the keychain holding the certificates and credentials")
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
    version_file = build_dir / "phoenix_version.txt"
    built = version_file.read_text(encoding="utf-8").strip() if version_file.exists() else "no version"
    if built != args.version:
        sys.exit(f"{build_dir} is configured as {built}, not {args.version}; "
                 f"configure it with -DPHOENIX_VERSION={args.version}")

    if stage.exists():
        shutil.rmtree(stage)
    run(["cmake", "--install", build_dir, "--config", args.config,
         "--component", "Phoenix", "--prefix", stage])

    cmd = ["vpk", "pack",
           "--packId", PACK_ID,
           "--packVersion", args.version,
           "--packDir", stage / settings["pack_dir"],
           "--packTitle", PACK_TITLE,
           "--packAuthors", PACK_AUTHORS,
           "--mainExe", settings["main_exe"],
           "--icon", ROOT / settings["icon"],
           "--channel", settings["channel"],
           "--runtime", settings["runtime"],
           "--outputDir", out]
    if "framework" in settings:
        cmd += ["--framework", settings["framework"]]
    if args.sign_app_identity:
        cmd += ["--signAppIdentity", args.sign_app_identity]
    if args.sign_install_identity:
        cmd += ["--signInstallIdentity", args.sign_install_identity]
    if args.notary_profile:
        cmd += ["--notaryProfile", args.notary_profile]
    if args.keychain:
        cmd += ["--keychain", args.keychain]
    run(cmd)

    shutil.rmtree(stage)
    print(f"Packed Phoenix {args.version} for {target} into {out}")


if __name__ == "__main__":
    main()
