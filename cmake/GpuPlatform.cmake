# Which of Dawn's prebuilt release packages is for the machine being built
# for (cmake/gpu.cmake downloads it): windows-latest, macos-latest (arm64),
# macos-15-intel or ubuntu-latest, all x64 but the first macOS one. Empty
# where Dawn publishes none (arm64 Windows and Linux).
function(phoenix_dawn_package out_var)
    set(package "")
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" arch)
    if(WIN32)
        # The generator's platform decides, as an x64 machine builds for arm64.
        if(NOT (CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$" OR
                (NOT CMAKE_GENERATOR_PLATFORM AND arch MATCHES "arm64|aarch64")))
            set(package windows-latest)
        endif()
    elseif(APPLE)
        if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" OR (NOT CMAKE_OSX_ARCHITECTURES AND arch MATCHES "x86_64|amd64"))
            set(package macos-15-intel)
        else()
            set(package macos-latest)
        endif()
    elseif(UNIX AND NOT EMSCRIPTEN AND arch MATCHES "x86_64|amd64")
        set(package ubuntu-latest)
    endif()
    set(${out_var} "${package}" PARENT_SCOPE)
endfunction()
