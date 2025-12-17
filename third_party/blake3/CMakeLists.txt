# Blake3 Library CMake configuration
# This fetches and builds Blake3 from the official repository

include(FetchContent)

FetchContent_Declare(
    blake3_src
    GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
    GIT_TAG        1.5.0
)

FetchContent_MakeAvailable(blake3_src)

# Build Blake3 C library
set(BLAKE3_SOURCES
    ${blake3_src_SOURCE_DIR}/c/blake3.c
    ${blake3_src_SOURCE_DIR}/c/blake3_dispatch.c
    ${blake3_src_SOURCE_DIR}/c/blake3_portable.c
)

# Add platform-specific optimized implementations
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    # x86-64 optimizations
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        list(APPEND BLAKE3_SOURCES
            ${blake3_src_SOURCE_DIR}/c/blake3_sse2_x86-64_unix.S
            ${blake3_src_SOURCE_DIR}/c/blake3_sse41_x86-64_unix.S
            ${blake3_src_SOURCE_DIR}/c/blake3_avx2_x86-64_unix.S
            ${blake3_src_SOURCE_DIR}/c/blake3_avx512_x86-64_unix.S
        )
    elseif(MSVC)
        list(APPEND BLAKE3_SOURCES
            ${blake3_src_SOURCE_DIR}/c/blake3_sse2.c
            ${blake3_src_SOURCE_DIR}/c/blake3_sse41.c
            ${blake3_src_SOURCE_DIR}/c/blake3_avx2.c
            ${blake3_src_SOURCE_DIR}/c/blake3_avx512.c
        )
        # MSVC requires specific flags for AVX
        set_source_files_properties(
            ${blake3_src_SOURCE_DIR}/c/blake3_avx2.c
            PROPERTIES COMPILE_FLAGS "/arch:AVX2"
        )
        set_source_files_properties(
            ${blake3_src_SOURCE_DIR}/c/blake3_avx512.c
            PROPERTIES COMPILE_FLAGS "/arch:AVX512"
        )
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
    # ARM64 NEON optimizations
    list(APPEND BLAKE3_SOURCES
        ${blake3_src_SOURCE_DIR}/c/blake3_neon.c
    )
endif()

# Create library
add_library(blake3 STATIC ${BLAKE3_SOURCES})

target_include_directories(blake3 PUBLIC
    ${blake3_src_SOURCE_DIR}/c
)

# Export the include directory for parent project
set(BLAKE3_INCLUDE_DIR ${blake3_src_SOURCE_DIR}/c PARENT_SCOPE)
