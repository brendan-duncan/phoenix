# The trimmed Qt the build downloads when it finds no Qt (see PhoenixQt.cmake).
# tools/qt/make_qt_archive.py rewrites the hashes here as it packs each
# archive; an empty hash means that archive has not been built yet and a
# download of it is not verified.
set(PHOENIX_QT_VERSION "6.12.0")
set(PHOENIX_QT_SHA256_windows_x64 "9965196340600be4dba2c46c960b5908a868864c09efdb549febca00ccfce2e1")
set(PHOENIX_QT_SHA256_windows_arm64 "627bc028aba9c2a6bb5e1fe90ac8cf43a4aa5fb68e409af638230f2e9b45e163")
set(PHOENIX_QT_SHA256_macos "4b6c7cd46deec19113a4f735222f86a2e0e926d2d1da58eeb98681d4d91ef384")
set(PHOENIX_QT_SHA256_linux_x64 "9d8c9bd3855d62739fbb8ec7001c43efaddebc573469960fa735bac5607b5973")
set(PHOENIX_QT_SHA256_linux_arm64 "bdfbbb759ccd64eb05ea582a057641aac8fb5d6ab42809716066f3cbfbe063c8")
