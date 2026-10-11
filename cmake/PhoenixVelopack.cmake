# Velopack, which installs Phoenix and keeps it up to date.
#
# Its C library (Rust underneath, with a C++ header over it) is downloaded
# from the Velopack release pinned below and linked statically. The app
# side is src/gui/updater.cpp; the installer is made by tools/release/
# and the release workflow. -DPHOENIX_UPDATER=OFF builds without it.
#
# The vpk tool that packs a release must be the same version:
#   dotnet tool install -g vpk --version <PHOENIX_VELOPACK_VERSION>

option(PHOENIX_UPDATER "Build in the Velopack auto-updater" ON)

set(PHOENIX_VELOPACK_VERSION "1.2.161")
set(PHOENIX_VELOPACK_SHA256 "31de8f2cf05a47bd5328a9f98b0c41aec1582156f6075f1058b26941f7f17c20")

# Adds the velopack imported target, or turns PHOENIX_UPDATER off when
# there is no Velopack library for this platform.
function(phoenix_add_velopack)
    if(NOT PHOENIX_UPDATER)
        return()
    endif()

    phoenix_qt_target(target)
    if(target STREQUAL "windows-x64")
        set(lib "velopack_libc_win_x64_msvc.lib")
    elseif(target STREQUAL "windows-arm64")
        set(lib "velopack_libc_win_arm64_msvc.lib")
    elseif(target STREQUAL "macos")
        if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" OR
           (NOT CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64"))
            set(lib "velopack_libc_osx_x64_gnu.a")
        else()
            set(lib "velopack_libc_osx_arm64_gnu.a")
        endif()
    elseif(target STREQUAL "linux-x64")
        set(lib "velopack_libc_linux_x64_gnu.a")
    elseif(target STREQUAL "linux-arm64")
        set(lib "velopack_libc_linux_arm64_gnu.a")
    endif()

    if(WIN32 AND NOT MSVC)
        message(STATUS "Velopack: no library for this Windows toolchain; building without the updater")
        set(PHOENIX_UPDATER OFF PARENT_SCOPE)
        return()
    endif()

    FetchContent_Declare(velopack_libc
        URL "https://github.com/velopack/velopack/releases/download/${PHOENIX_VELOPACK_VERSION}/velopack_libc_${PHOENIX_VELOPACK_VERSION}.zip"
        URL_HASH SHA256=${PHOENIX_VELOPACK_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(velopack_libc)

    add_library(velopack STATIC IMPORTED GLOBAL)
    set_target_properties(velopack PROPERTIES
        IMPORTED_LOCATION "${velopack_libc_SOURCE_DIR}/lib-static/${lib}"
        INTERFACE_INCLUDE_DIRECTORIES "${velopack_libc_SOURCE_DIR}/include"
    )

    # What the Rust standard library and Velopack's dependencies call into.
    if(WIN32)
        set_property(TARGET velopack PROPERTY INTERFACE_LINK_LIBRARIES
            ws2_32 userenv bcrypt ntdll advapi32 crypt32 secur32 ncrypt
            ole32 oleaut32 shell32 shlwapi propsys runtimeobject user32 version)
        # The library is built against the static C runtime and Phoenix
        # against the DLL one; one runtime is enough.
        set_property(TARGET velopack PROPERTY INTERFACE_LINK_OPTIONS
            "/NODEFAULTLIB:libcmt" "/NODEFAULTLIB:libcmtd")
    elseif(APPLE)
        set_property(TARGET velopack PROPERTY INTERFACE_LINK_LIBRARIES
            "-framework CoreFoundation" "-framework Security"
            "-framework SystemConfiguration" "-framework AppKit")
    else()
        set_property(TARGET velopack PROPERTY INTERFACE_LINK_LIBRARIES
            ${CMAKE_DL_LIBS} pthread m)
    endif()
endfunction()
