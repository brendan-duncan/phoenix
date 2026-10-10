# The trimmed Qt the build downloads when it finds no Qt (see PhoenixQt.cmake).
# tools/qt/make_qt_archive.py rewrites the hashes here as it packs each
# archive; an empty hash means that archive has not been built yet and a
# download of it is not verified.
set(PHOENIX_QT_VERSION "6.11.2")
set(PHOENIX_QT_SHA256_windows_x64 "b6555da02201b2ac8d040cac27a1c351ac91611a55b6da64a7a742a4176c75da")
set(PHOENIX_QT_SHA256_windows_arm64 "")
set(PHOENIX_QT_SHA256_macos "")
set(PHOENIX_QT_SHA256_linux_x64 "")
set(PHOENIX_QT_SHA256_linux_arm64 "")
