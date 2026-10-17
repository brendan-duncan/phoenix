# A stand-alone check of the fl.* component library in resources/as3:
# every class compiles and loads with libas3, and the scripts in scripts/
# pass, against the stand-in flash.* classes in stubs/. Not part of the
# Phoenix build:
#
#   cmake -S tests/as3_components -B build-as3c -DPHOENIX_LIBAS3_SOURCE=D:/src/libas3
#   cmake --build build-as3c --config Release
#   build-as3c/Release/as3_components.exe resources/as3 tests/as3_components
cmake_minimum_required(VERSION 3.15)
project(as3_components LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PHOENIX_LIBAS3_SOURCE "" CACHE PATH "libas3's source folder")
if(NOT PHOENIX_LIBAS3_SOURCE)
    message(FATAL_ERROR "Set PHOENIX_LIBAS3_SOURCE to libas3's source folder")
endif()
add_subdirectory(${PHOENIX_LIBAS3_SOURCE} libas3)

add_executable(as3_components main.cpp)
target_link_libraries(as3_components PRIVATE as3_parser)
if(MSVC)
    target_compile_options(as3_components PRIVATE /utf-8 /bigobj)
endif()
