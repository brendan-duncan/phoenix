# Zstandard's decompressor, as one file: zstddeclib.c made from zstd 1.5.7
# by its build/single_file_libs/create_single_file_decoder.sh, and zstd.h,
# both unchanged. Newer Illustrator files compress their own data with it.
add_library(zstd_decompress STATIC zstddeclib.c)
target_include_directories(zstd_decompress PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(zstd_decompress PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(zstd_decompress PRIVATE /W0)
else()
    target_compile_options(zstd_decompress PRIVATE -w)
endif()
