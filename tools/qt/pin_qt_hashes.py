#!/usr/bin/env python3
"""Writes archive hashes into cmake/QtArchives.cmake.

  python tools/qt/pin_qt_hashes.py 6.11.2 dist/*.sha256

Each .sha256 file is the one make_qt_archive.py writes beside its archive:
"<hash>  qt-<version>-<target>.<ext>". The Qt archives workflow runs this
over the archives from every platform before opening the pull request that
pins them.
"""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from make_qt_archive import update_cmake_hashes  # noqa: E402


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    version = sys.argv[1]
    cmake_file = Path(__file__).resolve().parents[2] / "cmake" / "QtArchives.cmake"
    for sha_file in sys.argv[2:]:
        text = Path(sha_file).read_text(encoding="utf-8").strip()
        digest, name = text.split()[:2]
        match = re.match(r"qt-([^-]+)-(windows-x64|windows-arm64|macos|linux-x64|linux-arm64)\.", name)
        if not match:
            sys.exit(f"{sha_file}: cannot tell the target from {name}")
        if match.group(1) != version:
            sys.exit(f"{sha_file}: archive is for Qt {match.group(1)}, not {version}")
        update_cmake_hashes(cmake_file, match.group(2), version, digest)
        print(f"{match.group(2)}: {digest}")


if __name__ == "__main__":
    main()
