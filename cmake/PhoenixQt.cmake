# Finding Qt without asking the developer to install it.
#
# In order of preference:
#   1. -DQt6_ROOT or -DCMAKE_PREFIX_PATH on the command line
#   2. the Qt6_ROOT or QTDIR environment variables
#   3. a trimmed Qt already unpacked under third_party/qt/<target>
#   4. on Windows, the newest C:/Qt/6.*/msvc*_64 install
#   5. downloading the trimmed Qt for this machine into third_party/qt
#
# The trimmed archives are made by tools/qt/make_qt_archive.py and published
# as release assets; cmake/QtArchives.cmake pins the version and the
# archives' hashes. -DPHOENIX_FETCH_QT=OFF turns the download off, and
# -DPHOENIX_QT_BASE_URL=... points it somewhere else, and
# -DPHOENIX_QT_ARCHIVE_DIR=... uses archives already on disk.

include("${CMAKE_CURRENT_LIST_DIR}/QtArchives.cmake")

option(PHOENIX_FETCH_QT "Download a trimmed Qt into third_party/qt when no Qt is found" ON)
option(PHOENIX_USE_SYSTEM_QT "Look for an installed Qt before the vendored one" ON)
set(PHOENIX_QT_BASE_URL
    "https://github.com/brendan-duncan/phoenix/releases/download/qt-${PHOENIX_QT_VERSION}"
    CACHE STRING "Where the trimmed Qt archives are downloaded from")
set(PHOENIX_QT_ARCHIVE_DIR "" CACHE PATH
    "A folder already holding the trimmed Qt archives, used instead of downloading")

# The archive name for this machine: windows-x64, windows-arm64, macos,
# linux-x64 or linux-arm64. On Windows the generator's platform decides,
# since an x64 machine can build for arm64.
function(phoenix_qt_target out_var)
    set(arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    string(TOLOWER "${arch}" arch)
    if(CMAKE_HOST_WIN32)
        if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$" OR
           (NOT CMAKE_GENERATOR_PLATFORM AND arch MATCHES "arm64|aarch64"))
            set(${out_var} "windows-arm64" PARENT_SCOPE)
        else()
            set(${out_var} "windows-x64" PARENT_SCOPE)
        endif()
    elseif(CMAKE_HOST_APPLE)
        set(${out_var} "macos" PARENT_SCOPE)
    elseif(arch MATCHES "arm64|aarch64")
        set(${out_var} "linux-arm64" PARENT_SCOPE)
    else()
        set(${out_var} "linux-x64" PARENT_SCOPE)
    endif()
endfunction()

# Downloads and unpacks the trimmed Qt for this machine. Sets Qt6_ROOT (and
# QT_HOST_PATH for a cross kit) on success.
function(phoenix_fetch_qt)
    phoenix_qt_target(target)
    set(vendored "${CMAKE_SOURCE_DIR}/third_party/qt/${target}")

    if(NOT EXISTS "${vendored}/lib/cmake/Qt6/Qt6Config.cmake")
        if(NOT PHOENIX_FETCH_QT)
            return()
        endif()

        if(target MATCHES "^windows")
            set(kind "zip")
        else()
            set(kind "tar.gz")
        endif()
        set(name "qt-${PHOENIX_QT_VERSION}-${target}.${kind}")
        set(url "${PHOENIX_QT_BASE_URL}/${name}")
        set(archive "${CMAKE_BINARY_DIR}/qt-download/${name}")

        string(REPLACE "-" "_" key "${target}")
        set(hash "${PHOENIX_QT_SHA256_${key}}")

        # An archive already on disk is used as it is; otherwise it is
        # downloaded. Either way a pinned hash has to match.
        set(local "")
        if(PHOENIX_QT_ARCHIVE_DIR)
            file(TO_CMAKE_PATH "${PHOENIX_QT_ARCHIVE_DIR}" archive_dir)
            if(EXISTS "${archive_dir}/${name}")
                set(local "${archive_dir}/${name}")
            endif()
        endif()

        if(local)
            message(STATUS "Qt not found; using the archive ${local}")
            set(archive "${local}")
            if(hash)
                file(SHA256 "${archive}" actual)
                if(NOT actual STREQUAL hash)
                    message(FATAL_ERROR "${local} does not match the hash pinned in cmake/QtArchives.cmake")
                endif()
            endif()
        else()
            set(hash_args "")
            if(hash)
                set(hash_args EXPECTED_HASH "SHA256=${hash}")
            else()
                message(WARNING "No pinned hash for ${name}; the download is not verified")
            endif()

            message(STATUS "Qt not found; downloading ${url}")
            file(DOWNLOAD "${url}" "${archive}" ${hash_args} SHOW_PROGRESS STATUS status TLS_VERIFY ON)
            list(GET status 0 code)
            if(NOT code EQUAL 0)
                list(GET status 1 reason)
                file(REMOVE "${archive}")
                message(FATAL_ERROR
                    "Downloading Qt failed: ${reason}\n"
                    "Either install Qt and point the build at it (-DQt6_ROOT=...), or\n"
                    "make the archive with tools/qt/make_qt_archive.py and pass\n"
                    "-DPHOENIX_QT_ARCHIVE_DIR=/path/to/its/folder")
            endif()
        endif()

        message(STATUS "Unpacking ${name} into ${vendored}")
        file(MAKE_DIRECTORY "${vendored}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${vendored}")
        if(NOT local)
            file(REMOVE "${archive}")
        endif()
    endif()

    message(STATUS "Using the vendored Qt in ${vendored}")
    set(Qt6_ROOT "${vendored}" PARENT_SCOPE)
    if(EXISTS "${vendored}/host/lib/cmake/Qt6HostInfo")
        set(QT_HOST_PATH "${vendored}/host" PARENT_SCOPE)
    endif()
endfunction()

# The whole search, in the order above.
macro(phoenix_find_qt)
    # A build directory that has already found its Qt keeps it, whatever has
    # appeared under third_party/qt since.
    if(DEFINED CACHE{Qt6_DIR} AND EXISTS "${Qt6_DIR}/Qt6Config.cmake")
        set(_phoenix_qt_settled ON)
    else()
        set(_phoenix_qt_settled OFF)
    endif()

    if(NOT _phoenix_qt_settled AND NOT DEFINED Qt6_ROOT AND NOT DEFINED CMAKE_PREFIX_PATH)
        if(DEFINED ENV{Qt6_ROOT})
            set(Qt6_ROOT "$ENV{Qt6_ROOT}")
        elseif(DEFINED ENV{QTDIR})
            set(Qt6_ROOT "$ENV{QTDIR}")
        endif()
    endif()

    if(NOT _phoenix_qt_settled AND NOT DEFINED Qt6_ROOT AND NOT DEFINED CMAKE_PREFIX_PATH)
        phoenix_qt_target(_phoenix_qt_target)
        if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/qt/${_phoenix_qt_target}/lib/cmake/Qt6/Qt6Config.cmake")
            phoenix_fetch_qt()
        elseif(WIN32 AND PHOENIX_USE_SYSTEM_QT)
            file(GLOB _phoenix_qt_candidates "C:/Qt/6.*/msvc*_64" "C:/Qt/6.*/mingw*_64")
            # Newest first, so a machine with several Qt versions picks the latest.
            list(SORT _phoenix_qt_candidates COMPARE NATURAL ORDER DESCENDING)
            if(_phoenix_qt_candidates)
                list(GET _phoenix_qt_candidates 0 Qt6_ROOT)
                message(STATUS "Qt location not specified, using ${Qt6_ROOT}")
            endif()
            unset(_phoenix_qt_candidates)
        endif()
        unset(_phoenix_qt_target)
    endif()

    if(NOT _phoenix_qt_settled AND NOT DEFINED Qt6_ROOT AND NOT DEFINED CMAKE_PREFIX_PATH)
        phoenix_fetch_qt()
    endif()
    unset(_phoenix_qt_settled)

    find_package(Qt6 COMPONENTS Widgets Svg)

    if(NOT Qt6_FOUND)
        message(FATAL_ERROR
            "Qt 6 (Widgets and Svg) was not found.\n"
            "Point the build at a Qt installation:\n"
            "  cmake -DQt6_ROOT=C:/Qt/<version>/msvc2022_64 ...\n"
            "  set QTDIR=C:/Qt/<version>/msvc2022_64\n"
            "or let it download a trimmed Qt (the default; -DPHOENIX_FETCH_QT=ON).\n\n"
            "The non-Qt core and its tests build without Qt:\n"
            "  cmake -S tests -B build-tests && cmake --build build-tests\n")
    endif()
endmacro()
