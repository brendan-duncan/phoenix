# The trimmed Qt the build downloads when it finds no Qt (see PhoenixQt.cmake).
# tools/qt/make_qt_archive.py rewrites the hashes here as it packs each
# archive; an empty hash means that archive has not been built yet and a
# download of it is not verified.
set(PHOENIX_QT_VERSION "6.11.2")
set(PHOENIX_QT_SHA256_windows_x64 "56f44824a63673f2310d681f534cb2b43e851fc4aa764682368e80c9b40189b8")
set(PHOENIX_QT_SHA256_windows_arm64 "")
set(PHOENIX_QT_SHA256_macos "")
set(PHOENIX_QT_SHA256_linux_x64 "")
set(PHOENIX_QT_SHA256_linux_arm64 "")
