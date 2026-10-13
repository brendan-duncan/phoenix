#!/usr/bin/env bash
# Builds the ffmpeg program Phoenix ships beside itself, and packs it with
# its licenses into the archive the build downloads (cmake/PhoenixFfmpeg.cmake).
#
#   tools/ffmpeg/build_ffmpeg.sh sources <dir>
#       Downloads the pinned sources into <dir>: the FFmpeg release tarball
#       (checked against its hash) and x264 at its pinned commit, packed as
#       a tarball. Every platform builds from the same <dir>, which is also
#       published, since a GPL program is shipped with its source.
#
#   tools/ffmpeg/build_ffmpeg.sh build <target> <sources dir> <out dir>
#       Builds ffmpeg for <target> (windows-x64, windows-arm64, macos,
#       linux-x64 or linux-arm64) and writes ffmpeg-<version>-<target>.zip
#       (Windows) or .tar.gz, with a .sha256 beside it, into <out dir>.
#
# The build is one static program with no libraries of its own to find:
# x264 for H.264, and FFmpeg's own encoders for ProRes, AAC, PCM and JPEG,
# and its decoders for reading reference video. Nothing on the machine is
# picked up by chance (--disable-autodetect), there is no networking, and
# zlib is the only library asked for.
#
# Windows runs this in MSYS2: MINGW64 for x64, CLANGARM64 for arm64. macOS
# makes one universal program, arm64 and x86_64. Linux builds on the oldest
# runner, so the program runs on older systems too.

set -euo pipefail

FFMPEG_VERSION="8.1.3"
FFMPEG_SHA256="7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3"
X264_COMMIT="b35605ace3ddf7c1a5d67a2eb553f034aef41d55"
MACOS_MIN="12.0"

ncpu() {
    getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4
}

# strip and objdump, or LLVM's, which is all an MSYS2 clang environment has.
tool() {
    if command -v "$1" >/dev/null; then echo "$1"; else echo "llvm-$1"; fi
}

sha256() {
    if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

fetch_sources() {
    local dir="$1"
    mkdir -p "$dir"
    dir=$(cd "$dir" && pwd)
    local tarball="$dir/ffmpeg-$FFMPEG_VERSION.tar.xz"
    if [ ! -f "$tarball" ]; then
        curl -fsSL -o "$tarball" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
    fi
    local actual
    actual=$(sha256 "$tarball")
    if [ "$actual" != "$FFMPEG_SHA256" ]; then
        echo "ffmpeg-$FFMPEG_VERSION.tar.xz has hash $actual, not the pinned $FFMPEG_SHA256" >&2
        exit 1
    fi

    local x264="$dir/x264-$X264_COMMIT.tar.gz"
    if [ ! -f "$x264" ]; then
        local work
        work=$(mktemp -d)
        git init -q "$work/x264-$X264_COMMIT"
        git -C "$work/x264-$X264_COMMIT" fetch -q --depth 1 https://code.videolan.org/videolan/x264.git "$X264_COMMIT"
        git -C "$work/x264-$X264_COMMIT" checkout -q FETCH_HEAD
        rm -rf "$work/x264-$X264_COMMIT/.git"
        tar -C "$work" -czf "$x264" "x264-$X264_COMMIT"
        rm -rf "$work"
    fi
    echo "Sources in $dir:"
    ls -la "$dir"
}

# build_arch <prefix> <x264 configure args...> -- <ffmpeg configure args...>
# Builds x264 and then ffmpeg into <prefix>, from the unpacked sources in
# $work. CC, if set, is the compiler for both.
build_arch() {
    local prefix="$1"
    shift
    local x264_args=() ffmpeg_args=()
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do x264_args+=("$1"); shift; done
    [ $# -gt 0 ] && shift
    ffmpeg_args=("$@")

    local x264_build="$prefix-x264-build" ffmpeg_build="$prefix-ffmpeg-build"
    rm -rf "$x264_build" "$ffmpeg_build"
    cp -R "$work/x264-$X264_COMMIT" "$x264_build"
    (
        cd "$x264_build"
        ./configure --prefix="$prefix" --enable-static --disable-cli --enable-pic "${x264_args[@]}"
        make -j"$(ncpu)"
        make install
    )

    mkdir -p "$ffmpeg_build"
    (
        cd "$ffmpeg_build"
        PKG_CONFIG_PATH="$prefix/lib/pkgconfig" "$work/ffmpeg-$FFMPEG_VERSION/configure" \
            --prefix="$prefix" \
            --enable-gpl --enable-libx264 --enable-zlib \
            --enable-static --disable-shared \
            --disable-autodetect --disable-network \
            --disable-doc --disable-ffplay --disable-ffprobe --disable-debug \
            --pkg-config-flags=--static \
            --extra-cflags="-I$prefix/include" --extra-ldflags="-L$prefix/lib" \
            "${ffmpeg_args[@]}"
        make -j"$(ncpu)"
    )
}

# Encodes a second of test pattern each way Phoenix does, so a broken
# program never gets packed.
smoke_test() {
    local program="$1" dir
    dir=$(mktemp -d)
    "$program" -hide_banner -loglevel error -f lavfi -i testsrc=duration=1:size=320x240:rate=24 \
        -f lavfi -i sine=duration=1 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$dir/test.mp4"
    "$program" -hide_banner -loglevel error -f lavfi -i testsrc=duration=1:size=320x240:rate=24 \
        -c:v prores_ks -profile:v 4 -pix_fmt yuva444p10le "$dir/test.mov"
    "$program" -hide_banner -loglevel error -i "$dir/test.mp4" -q:v 3 "$dir/frame_%04d.jpg" \
        -vn -ac 2 -ar 44100 -c:a pcm_s16le "$dir/sound.wav"
    ls "$dir"/frame_0001.jpg "$dir"/sound.wav >/dev/null
    rm -rf "$dir"
    echo "smoke test passed: $program"
}

# The libraries a Windows program needs that are not part of Windows.
check_windows_imports() {
    local program="$1" dll bad=""
    for dll in $("$(tool objdump)" -p "$program" | sed -n 's/^.*DLL Name: //p' | tr 'A-Z' 'a-z'); do
        case "$dll" in
            kernel32.dll|user32.dll|advapi32.dll|bcrypt.dll|ole32.dll|oleaut32.dll|shell32.dll|ws2_32.dll|\
            msvcrt.dll|ucrtbase.dll|api-ms-win-*|gdi32.dll|psapi.dll|secur32.dll|shlwapi.dll|ntdll.dll) ;;
            *) bad="$bad $dll" ;;
        esac
    done
    if [ -n "$bad" ]; then
        echo "$program needs libraries Windows does not have:$bad" >&2
        exit 1
    fi
}

build() {
    local target="$1" src out
    src=$(cd "$2" && pwd)
    mkdir -p "$3"
    out=$(cd "$3" && pwd)

    work=$(mktemp -d)
    tar -C "$work" -xf "$src/ffmpeg-$FFMPEG_VERSION.tar.xz"
    tar -C "$work" -xzf "$src/x264-$X264_COMMIT.tar.gz"

    local stage="$work/stage" program="ffmpeg"
    mkdir -p "$stage"
    case "$target" in
        windows-x64)
            program="ffmpeg.exe"
            build_arch "$work/x64" -- --extra-ldflags=-static
            cp "$work/x64-ffmpeg-build/ffmpeg.exe" "$stage/"
            "$(tool strip)" "$stage/ffmpeg.exe"
            check_windows_imports "$stage/ffmpeg.exe"
            ;;
        windows-arm64)
            program="ffmpeg.exe"
            CC=clang build_arch "$work/arm64" -- --cc=clang --cxx=clang++ --extra-ldflags=-static
            cp "$work/arm64-ffmpeg-build/ffmpeg.exe" "$stage/"
            "$(tool strip)" "$stage/ffmpeg.exe"
            check_windows_imports "$stage/ffmpeg.exe"
            ;;
        macos)
            export MACOSX_DEPLOYMENT_TARGET="$MACOS_MIN"
            CC="clang -arch arm64" build_arch "$work/arm64" \
                --host=aarch64-apple-darwin \
                -- --cc="clang -arch arm64" --arch=arm64 --target-os=darwin --enable-cross-compile
            CC="clang -arch x86_64" build_arch "$work/x86_64" \
                --host=x86_64-apple-darwin \
                -- --cc="clang -arch x86_64" --arch=x86_64 --target-os=darwin --enable-cross-compile
            lipo -create "$work/arm64-ffmpeg-build/ffmpeg" "$work/x86_64-ffmpeg-build/ffmpeg" \
                -output "$stage/ffmpeg"
            strip -x "$stage/ffmpeg"
            lipo -info "$stage/ffmpeg"
            ;;
        linux-x64|linux-arm64)
            build_arch "$work/native" --
            cp "$work/native-ffmpeg-build/ffmpeg" "$stage/"
            strip "$stage/ffmpeg"
            ldd "$stage/ffmpeg" || true
            ;;
        *)
            echo "unknown target $target" >&2
            exit 1
            ;;
    esac
    chmod 755 "$stage/$program"
    smoke_test "$stage/$program"

    # The licenses go with the program: FFmpeg built with x264 is GPL 2 or
    # later, and its source is published beside the archives.
    cp "$work/ffmpeg-$FFMPEG_VERSION/COPYING.GPLv2" "$stage/COPYING.GPLv2.txt"
    cp "$work/x264-$X264_COMMIT/COPYING" "$stage/x264-COPYING.txt"
    cat > "$stage/README-ffmpeg.txt" <<EOF
Phoenix runs this ffmpeg to encode and read video. It is FFmpeg $FFMPEG_VERSION
(https://ffmpeg.org) built with x264 (https://www.videolan.org/developers/x264.html,
commit $X264_COMMIT), and is distributed under the GNU General Public License,
version 2 or later (COPYING.GPLv2.txt). It is a separate program from Phoenix,
which is under its own MIT license.

The exact source it was built from is published with it:
https://github.com/brendan-duncan/phoenix/releases/tag/ffmpeg-$FFMPEG_VERSION
(ffmpeg-$FFMPEG_VERSION.tar.xz and x264-$X264_COMMIT.tar.gz), and the steps are
tools/ffmpeg/build_ffmpeg.sh in Phoenix's repository.
EOF

    local name="ffmpeg-$FFMPEG_VERSION-$target"
    case "$target" in
        windows-*)
            rm -f "$out/$name.zip"
            (cd "$stage" && zip -qr "$out/$name.zip" .)
            name="$name.zip"
            ;;
        *)
            tar -C "$stage" -czf "$out/$name.tar.gz" .
            name="$name.tar.gz"
            ;;
    esac
    (cd "$out" && echo "$(sha256 "$name")  $name" > "$name.sha256")
    cat "$out/$name.sha256"
    rm -rf "$work"
}

case "${1:-}" in
    sources) fetch_sources "$2" ;;
    build) build "$2" "$3" "$4" ;;
    version) echo "$FFMPEG_VERSION" ;;
    *) sed -n '2,24p' "$0"; exit 1 ;;
esac
