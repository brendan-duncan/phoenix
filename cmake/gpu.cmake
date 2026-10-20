# The GPU stage renderer (PHOENIX_GPU): WebGPU through Dawn, natively and in
# the browser, drawn by the Rive renderer. ThorVG is built too on the
# desktop, for Phoenix gpu-bakeoff.
#
# Natively Dawn comes as Google's prebuilt release package for the platform,
# pinned to one tag, or (PHOENIX_DAWN_FROM_SOURCE) is built from the same
# commit: the Linux package is built on a newer system than the release
# builds on, and needs a newer C++ library than that has. In the browser it
# is Emscripten's emdawnwebgpu port, Dawn's webgpu.h over the browser's
# WebGPU. The target phoenix_webgpu is whichever it is.

include(FetchContent)

if(EMSCRIPTEN)
    add_library(phoenix_webgpu INTERFACE)
    target_compile_options(phoenix_webgpu INTERFACE --use-port=emdawnwebgpu)
    target_link_options(phoenix_webgpu INTERFACE --use-port=emdawnwebgpu)
else()

set(PHOENIX_DAWN_TAG v20261016.190333)
set(PHOENIX_DAWN_COMMIT 67d4d6e64b0755d638f76549794692badfdbc8ab)
option(PHOENIX_DAWN_FROM_SOURCE "Build Dawn from source rather than use its prebuilt package" OFF)
if(PHOENIX_DAWN_FROM_SOURCE)
    FetchContent_Declare(
        dawn_source
        URL https://github.com/google/dawn/archive/${PHOENIX_DAWN_COMMIT}.tar.gz
        URL_HASH SHA256=5e5112b5f43e1d0fd5bb26259a3cfc5d5672c09973fad45c39d67c77ccb8cecf
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    # Only what drawing into a texture and reading it back needs: no window
    # surfaces (X11, Wayland, GLFW), no OpenGL backends, no tools or tests.
    # Its dependencies come by its own script rather than depot_tools.
    set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
    foreach(_off DAWN_BUILD_SAMPLES DAWN_BUILD_TESTS DAWN_BUILD_BENCHMARKS DAWN_BUILD_PROTOBUF DAWN_USE_GLFW
            DAWN_USE_X11 DAWN_USE_WAYLAND DAWN_ENABLE_DESKTOP_GL DAWN_ENABLE_OPENGLES DAWN_ENABLE_INSTALL
            TINT_BUILD_TESTS TINT_BUILD_CMD_TOOLS TINT_BUILD_BENCHMARKS)
        set(${_off} OFF CACHE BOOL "" FORCE)
    endforeach()
    # Dawn's C++ API as a C++20 module is not used, and the check for it
    # fails with some generators.
    set(DAWN_SUPPORTS_CXX_MODULES FALSE CACHE INTERNAL "")
    FetchContent_GetProperties(dawn_source)
    if(NOT dawn_source_POPULATED)
        FetchContent_Populate(dawn_source)
    endif()
    # No Qt in it: Qt's code generators stay off its targets.
    set(_phoenix_automoc ${CMAKE_AUTOMOC})
    set(CMAKE_AUTOMOC OFF)
    add_subdirectory(${dawn_source_SOURCE_DIR} ${dawn_source_BINARY_DIR} EXCLUDE_FROM_ALL)
    set(CMAKE_AUTOMOC ${_phoenix_automoc})
    add_library(phoenix_webgpu INTERFACE)
    target_link_libraries(phoenix_webgpu INTERFACE dawn::webgpu_dawn dawncpp_headers)
else()
include(${CMAKE_CURRENT_LIST_DIR}/GpuPlatform.cmake)
phoenix_dawn_package(_dawn_os)
if(_dawn_os STREQUAL "windows-latest")
    set(_dawn_hash 57402c6aa887e30d3e5710707d6b0677d8292538ab941628c624635022bfa49f)
elseif(_dawn_os STREQUAL "macos-latest")
    set(_dawn_hash 736fccba78b8a2fde8bfd55d045e890933b0a01776aed5a29b9f024735404193)
elseif(_dawn_os STREQUAL "macos-15-intel")
    set(_dawn_hash 277dafe9b6d8c3374f10963d54cec5548c05ab478519f85c9cb335dfbd31dde1)
elseif(_dawn_os STREQUAL "ubuntu-latest")
    set(_dawn_hash 20ff88448def9645ceeff30911e07295aeb433be23e26180c96019b4300bff5a)
else()
    message(FATAL_ERROR "Dawn publishes no package for this machine; configure with -DPHOENIX_GPU=OFF")
endif()

FetchContent_Declare(
    dawn_prebuilt
    URL https://github.com/google/dawn/releases/download/${PHOENIX_DAWN_TAG}/Dawn-${PHOENIX_DAWN_COMMIT}-${_dawn_os}-Release.tar.gz
    URL_HASH SHA256=${_dawn_hash}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(dawn_prebuilt)
if(NOT dawn_prebuilt_POPULATED)
    FetchContent_Populate(dawn_prebuilt)
endif()
# The Linux package keeps its libraries in lib64.
find_package(Dawn CONFIG REQUIRED
    PATHS ${dawn_prebuilt_SOURCE_DIR}/lib/cmake/Dawn ${dawn_prebuilt_SOURCE_DIR}/lib64/cmake/Dawn NO_DEFAULT_PATH)
add_library(phoenix_webgpu INTERFACE)
target_link_libraries(phoenix_webgpu INTERFACE dawn::webgpu_dawn)
endif() # PHOENIX_DAWN_FROM_SOURCE

# ThorVG: the core, the raw image loader and the WebGPU engine.
FetchContent_Declare(
    thorvg
    GIT_REPOSITORY https://github.com/thorvg/thorvg
    GIT_TAG v1.1.3
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(thorvg)
if(NOT thorvg_POPULATED)
    FetchContent_Populate(thorvg)
endif()
set(_tvg ${thorvg_SOURCE_DIR})
file(GLOB _tvg_sources
    ${_tvg}/src/common/*.cpp
    ${_tvg}/src/renderer/*.cpp
    ${_tvg}/src/renderer/gpu_engine/*.cpp
    ${_tvg}/src/renderer/gpu_engine/wg/*.cpp
    ${_tvg}/src/loaders/raw/*.cpp
)
set(_tvg_config ${CMAKE_CURRENT_BINARY_DIR}/thorvg_config)
file(WRITE ${_tvg_config}/config.h
"#define THORVG_VERSION_STRING \"1.1.3\"\n#define THORVG_WG_ENGINE_SUPPORT 1\n#define WIN32_LEAN_AND_MEAN 1\n#define THORVG_LOG_ENABLED 1\n")
add_library(thorvg STATIC ${_tvg_sources})
# No Qt here: keep Qt's code generators off it.
set_target_properties(thorvg PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(thorvg
    PUBLIC ${_tvg}/inc
    PRIVATE ${_tvg_config} ${_tvg}/src/common ${_tvg}/src/renderer ${_tvg}/src/renderer/gpu_engine
            ${_tvg}/src/renderer/gpu_engine/wg ${_tvg}/src/loaders/raw ${_tvg}/src/loaders ${_tvg}/src/savers)
target_compile_definitions(thorvg PUBLIC TVG_STATIC PRIVATE NOMINMAX)
target_compile_features(thorvg PRIVATE cxx_std_20)
target_link_libraries(thorvg PUBLIC phoenix_webgpu)
if(MSVC)
    target_compile_options(thorvg PRIVATE /W0 /utf-8)
endif()
endif() # not EMSCRIPTEN

# The Rive renderer: its core, the WebGPU backend and the math it needs, at
# one commit (as an archive, since a shallow clone cannot name a commit).
# Its WGSL shaders are generated offline and kept in third_party/rive_shaders
# (tools/gpu/generate_rive_shaders.py), as its own build makes them with make.
set(PHOENIX_RIVE_COMMIT d278866b98ed71118da6cf4126751f2a92ccf70c)
FetchContent_Declare(
    rive_runtime
    URL https://github.com/rive-app/rive-runtime/archive/${PHOENIX_RIVE_COMMIT}.tar.gz
    URL_HASH SHA256=1485550604753bb9a2b4b9944ff870d4fb88535715f895b12fb21f60b1e623df
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(rive_runtime)
if(NOT rive_runtime_POPULATED)
    FetchContent_Populate(rive_runtime)
endif()
set(_rive ${rive_runtime_SOURCE_DIR})
# Compatibility patch: this Dawn's Device::GetLimits returns a wgpu::Status,
# where the Rive code expects one that converts to bool.
set(_rive_webgpu ${_rive}/renderer/src/webgpu/render_context_webgpu_impl.cpp)
file(READ ${_rive_webgpu} _rive_text)
string(REPLACE "const bool deviceLimitsValid = m_device.GetLimits(&deviceLimits);"
    "const bool deviceLimitsValid = m_device.GetLimits(&deviceLimits) == wgpu::Status::Success; // Phoenix: Dawn compatibility"
    _rive_patched "${_rive_text}")
if(NOT _rive_patched STREQUAL _rive_text)
    file(WRITE ${_rive_webgpu} "${_rive_patched}")
endif()
# Of paint_outset.cpp, only paintBoundsOutset: the rest needs the shapes of
# Rive's file format, which are left out.
file(READ ${_rive}/src/shapes/paint/paint_outset.cpp _rive_text)
string(FIND "${_rive_text}" "ShapePaintOutset shapePaintOutset(" _rive_cut)
string(SUBSTRING "${_rive_text}" 0 ${_rive_cut} _rive_text)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rive_paint_outset.cpp CONTENT "${_rive_text}} // namespace rive\n" @ONLY)
file(GLOB _rive_sources
    ${_rive}/renderer/src/*.cpp
    ${_rive}/src/math/*.cpp
)
list(APPEND _rive_sources
    ${_rive}/renderer/src/webgpu/render_context_webgpu_impl.cpp
    ${_rive}/src/renderer.cpp
    ${_rive}/src/shapes/paint/color.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/rive_paint_outset.cpp
)
add_library(rive_renderer STATIC ${_rive_sources})
set_target_properties(rive_renderer PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(rive_renderer
    PUBLIC ${_rive}/include ${_rive}/renderer/include
    PRIVATE ${_rive}/renderer/src ${_rive}/decoders/include ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rive_shaders)
# As its own build defines them for Dawn, or for emdawnwebgpu (its "WebGPU
# version 2"), and for MSVC.
if(EMSCRIPTEN)
    target_compile_definitions(rive_renderer PUBLIC RIVE_WEBGPU=2)
else()
    target_compile_definitions(rive_renderer PUBLIC RIVE_DAWN NOMINMAX _USE_MATH_DEFINES
        PRIVATE _CRT_SECURE_NO_WARNINGS _SILENCE_ALL_CXX20_DEPRECATION_WARNINGS)
endif()
target_compile_features(rive_renderer PUBLIC cxx_std_20)
target_link_libraries(rive_renderer PUBLIC phoenix_webgpu)
if(MSVC)
    target_compile_options(rive_renderer PRIVATE /W0 /utf-8 /bigobj /Zc:__cplusplus)
endif()
