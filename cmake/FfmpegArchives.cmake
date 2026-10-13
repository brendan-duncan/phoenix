# The ffmpeg Phoenix ships beside itself (see PhoenixFfmpeg.cmake). The
# ffmpeg archives workflow builds each archive with
# tools/ffmpeg/build_ffmpeg.sh and tools/ffmpeg/pin_ffmpeg_hashes.py writes
# its hash here; an empty hash means that archive has not been published yet
# and a download of it is not verified.
set(PHOENIX_FFMPEG_VERSION "8.1.3")
set(PHOENIX_FFMPEG_SHA256_windows_x64 "")
set(PHOENIX_FFMPEG_SHA256_windows_arm64 "")
set(PHOENIX_FFMPEG_SHA256_macos "")
set(PHOENIX_FFMPEG_SHA256_linux_x64 "")
set(PHOENIX_FFMPEG_SHA256_linux_arm64 "")
