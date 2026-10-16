# The web view that shows HTML extension panels (src/gui/web_view*).
#
# On Windows it is WebView2, the Edge runtime Windows ships with: its SDK
# (a header and a static loader) is downloaded from the NuGet package
# pinned below. On macOS it is WKWebView, from the system's WebKit. Linux
# builds go without (the panel says so). -DPHOENIX_WEBVIEW=OFF builds
# without it anywhere.

option(PHOENIX_WEBVIEW "Show HTML extension panels in a web view" ON)

set(PHOENIX_WEBVIEW2_VERSION "1.0.4191.47")
set(PHOENIX_WEBVIEW2_SHA256 "f492bbf547d0da329553b6727435b677579b1e9f91cc9e4a1ad029366d5f23d0")

if(APPLE AND PHOENIX_WEBVIEW)
    enable_language(OBJCXX)
endif()

# Adds the web view's sources and libraries to \a target, or nothing when
# there is none for this platform.
function(phoenix_add_web_view target)
    if(NOT PHOENIX_WEBVIEW)
        return()
    endif()
    if(WIN32 AND MSVC)
        phoenix_qt_target(qt_target)
        if(qt_target STREQUAL "windows-arm64")
            set(arch "arm64")
        else()
            set(arch "x64")
        endif()
        FetchContent_Declare(webview2
            URL "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${PHOENIX_WEBVIEW2_VERSION}/microsoft.web.webview2.${PHOENIX_WEBVIEW2_VERSION}.nupkg"
            URL_HASH SHA256=${PHOENIX_WEBVIEW2_SHA256}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            DOWNLOAD_NAME webview2.zip
        )
        FetchContent_MakeAvailable(webview2)
        target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/gui/web_view_win.cpp)
        target_include_directories(${target} PRIVATE "${webview2_SOURCE_DIR}/build/native/include")
        target_link_libraries(${target} PRIVATE
            "${webview2_SOURCE_DIR}/build/native/${arch}/WebView2LoaderStatic.lib" version advapi32 ole32 shell32)
        target_compile_definitions(${target} PRIVATE PHOENIX_WEBVIEW)
    elseif(APPLE)
        target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/gui/web_view_mac.mm)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/src/gui/web_view_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(${target} PRIVATE "-framework WebKit" "-framework AppKit")
        target_compile_definitions(${target} PRIVATE PHOENIX_WEBVIEW)
    endif()
endfunction()
