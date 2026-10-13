# The ffmpeg Phoenix ships beside itself (see PhoenixFfmpeg.cmake). The
# ffmpeg archives workflow builds each archive with
# tools/ffmpeg/build_ffmpeg.sh and tools/ffmpeg/pin_ffmpeg_hashes.py writes
# its hash here; an empty hash means that archive has not been published yet
# and a download of it is not verified.
set(PHOENIX_FFMPEG_VERSION "8.1.3")
set(PHOENIX_FFMPEG_SHA256_windows_x64 "c55f2d07f23f04cfb3896d4da9630e812dc73d8279c3cd57045d3f07a1747b92")
set(PHOENIX_FFMPEG_SHA256_windows_arm64 "c52c9ef75966e6f908fd4635cfc20086b4c629794ebead8d85b89937ab6ad883")
set(PHOENIX_FFMPEG_SHA256_macos "5bb893106f2fe20eeadcb0aa7d300ca606f3a85e21abcf48390929f77d354869")
set(PHOENIX_FFMPEG_SHA256_linux_x64 "02d801d0eedcde3d9796b9a735c067b55ae9af75b56eaddf441ef92ae6a6a3d8")
set(PHOENIX_FFMPEG_SHA256_linux_arm64 "268015e293b862228960cf50b2cb2d3289028abb812b6e14143c6fc153c8a579")
