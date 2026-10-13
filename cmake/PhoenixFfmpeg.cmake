# The ffmpeg program Phoenix encodes and reads video with. It ships beside
# Phoenix (next to Phoenix.exe on Windows, in Phoenix.app/Contents/MacOS on
# macOS, beside the phoenix binary on Linux), so video export and import
# never depend on anything installed on the machine. Phoenix only ever runs
# that copy (MovieExport::ffmpegPath).
#
# It is a separate program rather than a library linked in: it is built with
# x264, which is GPL, and as its own program it leaves Phoenix under its MIT
# license; a crash in a codec cannot take Phoenix down; and Phoenix already
# talks to it over pipes, the same way from the window and the command line.
#
# Where it comes from, first found first:
#   1. -DPHOENIX_FFMPEG_EXECUTABLE=/path/to/ffmpeg, a program to use as is
#      (for trying another build);
#   2. third_party/ffmpeg/<target>, unpacked from an archive before;
#   3. the archive in -DPHOENIX_FFMPEG_ARCHIVE_DIR, or else downloaded from
#      the ffmpeg-<version> release (-DPHOENIX_FFMPEG_BASE_URL), checked
#      against the hash pinned in cmake/FfmpegArchives.cmake.
# The archives are made by tools/ffmpeg/build_ffmpeg.sh, which the ffmpeg
# archives workflow runs for every platform. -DPHOENIX_FFMPEG_REQUIRED=ON
# (release builds) makes a missing ffmpeg an error rather than a warning.

include(${CMAKE_CURRENT_LIST_DIR}/FfmpegArchives.cmake)

set(PHOENIX_FFMPEG_EXECUTABLE "" CACHE FILEPATH "An ffmpeg program to ship instead of the pinned archive's")
set(PHOENIX_FFMPEG_ARCHIVE_DIR "" CACHE PATH "A folder already holding the ffmpeg archive, used instead of downloading")
set(PHOENIX_FFMPEG_BASE_URL
    "https://github.com/brendan-duncan/phoenix/releases/download/ffmpeg-${PHOENIX_FFMPEG_VERSION}"
    CACHE STRING "Where the ffmpeg archives are downloaded from")
option(PHOENIX_FETCH_FFMPEG "Download the ffmpeg archive when it is not unpacked yet" ON)
option(PHOENIX_FFMPEG_REQUIRED "Fail when there is no ffmpeg to ship (release builds)" OFF)

# Sets PHOENIX_FFMPEG_PROGRAM to the ffmpeg to ship, and
# PHOENIX_FFMPEG_LICENSES to the files that go with it; both empty when
# there is none.
function(phoenix_find_ffmpeg)
    set(PHOENIX_FFMPEG_PROGRAM "" PARENT_SCOPE)
    set(PHOENIX_FFMPEG_LICENSES "" PARENT_SCOPE)
    if(WIN32)
        set(exe "ffmpeg.exe")
    else()
        set(exe "ffmpeg")
    endif()

    if(PHOENIX_FFMPEG_EXECUTABLE)
        if(NOT EXISTS "${PHOENIX_FFMPEG_EXECUTABLE}")
            message(FATAL_ERROR "PHOENIX_FFMPEG_EXECUTABLE ${PHOENIX_FFMPEG_EXECUTABLE} does not exist")
        endif()
        message(STATUS "Shipping the ffmpeg ${PHOENIX_FFMPEG_EXECUTABLE}")
        set(PHOENIX_FFMPEG_PROGRAM "${PHOENIX_FFMPEG_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif()

    phoenix_qt_target(target)
    set(vendored "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/${PHOENIX_FFMPEG_VERSION}/${target}")
    if(NOT EXISTS "${vendored}/${exe}")
        if(target MATCHES "^windows")
            set(kind "zip")
        else()
            set(kind "tar.gz")
        endif()
        set(name "ffmpeg-${PHOENIX_FFMPEG_VERSION}-${target}.${kind}")
        string(REPLACE "-" "_" key "${target}")
        set(hash "${PHOENIX_FFMPEG_SHA256_${key}}")

        set(local "")
        if(PHOENIX_FFMPEG_ARCHIVE_DIR)
            file(TO_CMAKE_PATH "${PHOENIX_FFMPEG_ARCHIVE_DIR}" archive_dir)
            if(EXISTS "${archive_dir}/${name}")
                set(local "${archive_dir}/${name}")
            endif()
        endif()

        if(local)
            set(archive "${local}")
            if(hash)
                file(SHA256 "${archive}" actual)
                if(NOT actual STREQUAL hash)
                    message(FATAL_ERROR "${local} does not match the hash pinned in cmake/FfmpegArchives.cmake")
                endif()
            endif()
        elseif(PHOENIX_FETCH_FFMPEG)
            set(archive "${CMAKE_BINARY_DIR}/ffmpeg-download/${name}")
            set(hash_args "")
            if(hash)
                set(hash_args EXPECTED_HASH "SHA256=${hash}")
            else()
                message(WARNING "No pinned hash for ${name}; the download is not verified")
            endif()
            message(STATUS "Downloading ${PHOENIX_FFMPEG_BASE_URL}/${name}")
            file(DOWNLOAD "${PHOENIX_FFMPEG_BASE_URL}/${name}" "${archive}" ${hash_args}
                STATUS status TLS_VERIFY ON)
            list(GET status 0 code)
            if(NOT code EQUAL 0)
                list(GET status 1 reason)
                file(REMOVE "${archive}")
                set(archive "")
                set(why "downloading ${name} failed: ${reason}")
            endif()
        else()
            set(archive "")
            set(why "${name} is not in PHOENIX_FFMPEG_ARCHIVE_DIR and downloading is off")
        endif()

        if(NOT archive)
            set(text "No ffmpeg to ship with Phoenix (${why}). Video export and import "
                "will not work in this build. Make the archive with tools/ffmpeg/build_ffmpeg.sh "
                "and pass -DPHOENIX_FFMPEG_ARCHIVE_DIR, or -DPHOENIX_FFMPEG_EXECUTABLE to use a program as is.")
            string(CONCAT text ${text})
            if(PHOENIX_FFMPEG_REQUIRED)
                message(FATAL_ERROR "${text}")
            endif()
            message(WARNING "${text}")
            return()
        endif()

        message(STATUS "Unpacking ${name} into ${vendored}")
        file(REMOVE_RECURSE "${vendored}")
        file(MAKE_DIRECTORY "${vendored}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${vendored}")
        if(NOT local)
            file(REMOVE "${archive}")
        endif()
        if(NOT EXISTS "${vendored}/${exe}")
            message(FATAL_ERROR "${name} has no ${exe} in it")
        endif()
    endif()

    message(STATUS "Shipping ffmpeg ${PHOENIX_FFMPEG_VERSION} from ${vendored}")
    set(PHOENIX_FFMPEG_PROGRAM "${vendored}/${exe}" PARENT_SCOPE)
    file(GLOB licenses "${vendored}/*.txt")
    set(PHOENIX_FFMPEG_LICENSES "${licenses}" PARENT_SCOPE)
endfunction()

# Puts ffmpeg beside the built Phoenix, so a build runs as an installed copy
# does, and installs it, with its licenses, into the Phoenix component.
function(phoenix_ship_ffmpeg)
    phoenix_find_ffmpeg()
    if(NOT PHOENIX_FFMPEG_PROGRAM)
        return()
    endif()
    get_filename_component(exe "${PHOENIX_FFMPEG_PROGRAM}" NAME)
    if(APPLE)
        set(build_dir "$<TARGET_BUNDLE_CONTENT_DIR:Phoenix>/MacOS")
        set(install_dir "Phoenix.app/Contents/MacOS")
        set(license_dir "Phoenix.app/Contents/Resources/licenses/ffmpeg")
    else()
        set(build_dir "$<TARGET_FILE_DIR:Phoenix>")
        set(install_dir "${PHOENIX_INSTALL_BINDIR}")
        if(WIN32)
            set(license_dir "licenses/ffmpeg")
        else()
            set(license_dir "${CMAKE_INSTALL_DATADIR}/doc/phoenix/ffmpeg")
        endif()
    endif()
    add_custom_command(TARGET Phoenix POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PHOENIX_FFMPEG_PROGRAM}" "${build_dir}/${exe}"
        COMMENT "Copying ffmpeg beside Phoenix")
    install(PROGRAMS "${PHOENIX_FFMPEG_PROGRAM}" DESTINATION "${install_dir}" COMPONENT Phoenix)
    if(PHOENIX_FFMPEG_LICENSES)
        install(FILES ${PHOENIX_FFMPEG_LICENSES} DESTINATION "${license_dir}" COMPONENT Phoenix)
    endif()
endfunction()
