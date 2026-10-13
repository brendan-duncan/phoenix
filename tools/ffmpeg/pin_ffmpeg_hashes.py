#!/usr/bin/env python3
"""Writes ffmpeg archive hashes into cmake/FfmpegArchives.cmake.

  python tools/ffmpeg/pin_ffmpeg_hashes.py 8.1.3 dist/*.sha256

Each .sha256 file is the one build_ffmpeg.sh writes beside its archive:
"<hash>  ffmpeg-<version>-<target>.<ext>". The ffmpeg archives workflow runs
this over the archives from every platform before opening the pull request
that pins them.
"""

import re
import sys
from pathlib import Path

CMAKE_FILE = Path(__file__).resolve().parents[2] / "cmake" / "FfmpegArchives.cmake"
TARGETS = "windows-x64|windows-arm64|macos|linux-x64|linux-arm64"


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    version = sys.argv[1]
    text = CMAKE_FILE.read_text(encoding="utf-8")
    pinned = re.search(r'set\(PHOENIX_FFMPEG_VERSION "([^"]*)"\)', text)
    if not pinned or pinned.group(1) != version:
        sys.exit(f"{CMAKE_FILE.name} is for ffmpeg {pinned.group(1) if pinned else '?'}, not {version}")
    for sha_file in sys.argv[2:]:
        line = Path(sha_file).read_text(encoding="utf-8").strip()
        digest, name = line.split()[:2]
        match = re.match(rf"ffmpeg-([^-]+)-({TARGETS})\.(zip|tar\.gz)$", name)
        if not match:
            sys.exit(f"{sha_file}: cannot tell the target from {name}")
        if match.group(1) != version:
            sys.exit(f"{sha_file}: archive is ffmpeg {match.group(1)}, not {version}")
        key = match.group(2).replace("-", "_")
        text, count = re.subn(rf'(set\(PHOENIX_FFMPEG_SHA256_{key} ")[0-9a-f]*("\))',
                              lambda m: m.group(1) + digest + m.group(2), text)
        if count != 1:
            sys.exit(f"no PHOENIX_FFMPEG_SHA256_{key} line in {CMAKE_FILE.name}")
        print(f"{match.group(2)}: {digest}")
    CMAKE_FILE.write_text(text, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
