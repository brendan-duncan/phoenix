# The trimmed Qt the build downloads when it finds no Qt (see PhoenixQt.cmake).
# tools/qt/make_qt_archive.py rewrites the hashes here as it packs each
# archive; an empty hash means that archive has not been built yet and a
# download of it is not verified.
set(PHOENIX_QT_VERSION "6.12.0")
set(PHOENIX_QT_SHA256_windows_x64 "2f4b962dee268825da17eeda4643600efbec2b9fcc82f94e7e05d4c5b5a021c5")
set(PHOENIX_QT_SHA256_windows_arm64 "979b4348d7abbe73cb3127885c0ae14392be2a2561c54962682a3b4be16a77f4")
set(PHOENIX_QT_SHA256_macos "a4d86320115e10a5c6ea1ed7d040c52389f2c59279833187b538d0bb8d0331d8")
set(PHOENIX_QT_SHA256_linux_x64 "b16196fffabb8015c9a94452f0e0bf94c534ac739f4d305389d1ec3f1455018c")
set(PHOENIX_QT_SHA256_linux_arm64 "3c04a96c53df3bb1b0bff695faa971c4270e94547b38d3a92ef6a06d508d682a")
