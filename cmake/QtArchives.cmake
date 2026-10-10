# The trimmed Qt the build downloads when it finds no Qt (see PhoenixQt.cmake).
# tools/qt/make_qt_archive.py rewrites the hashes here as it packs each
# archive; an empty hash means that archive has not been built yet and a
# download of it is not verified.
set(PHOENIX_QT_VERSION "6.11.2")
set(PHOENIX_QT_SHA256_windows_x64 "777edaedb4a85b166034ca4f84b6dc4fb3032305d7b1e9b2cdaa54329efbd49f")
set(PHOENIX_QT_SHA256_windows_arm64 "540819226809914d70f42d387e8204f8229d3f6386f4d6bcb297d0601f9176ab")
set(PHOENIX_QT_SHA256_macos "a989c83f9abd4b9d189de66a4a6285a59d4f5fe168d29cab103a819b1f22768b")
set(PHOENIX_QT_SHA256_linux_x64 "4dad1d9f1eedce2c6db34821d6a195e6edc6b7e9b79dd04156284252126c4625")
set(PHOENIX_QT_SHA256_linux_arm64 "8a7bc071a5cd21903823d6199cd9453ade83b9a90a233e0c996d683974950bb5")
