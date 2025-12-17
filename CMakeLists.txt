cmake_minimum_required(VERSION 3.10)

project(tosminer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(WITH_OPENCL "Build with OpenCL support" ON)
option(WITH_CUDA "Build with CUDA support" ON)
option(WITH_CPU "Build with CPU mining support" ON)
option(WITH_TLS "Build with TLS/SSL support for stratum+ssl" ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find packages
find_package(Threads REQUIRED)

# Find Boost
find_package(Boost 1.65 REQUIRED COMPONENTS system program_options)
if(Boost_FOUND)
    message(STATUS "Boost found: ${Boost_VERSION}")
    include_directories(${Boost_INCLUDE_DIRS})
endif()

# Fetch nlohmann/json for JSON parsing
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Find OpenSSL for TLS support
if(WITH_TLS)
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
        add_definitions(-DWITH_TLS)
        include_directories(${OPENSSL_INCLUDE_DIR})
    else()
        message(WARNING "OpenSSL not found, disabling TLS support")
        set(WITH_TLS OFF)
    endif()
endif()

# Find OpenCL
if(WITH_OPENCL)
    find_package(OpenCL)
    if(OpenCL_FOUND)
        message(STATUS "OpenCL found: ${OpenCL_VERSION_STRING}")
        add_definitions(-DWITH_OPENCL)
        include_directories(${OpenCL_INCLUDE_DIRS})
    else()
        message(WARNING "OpenCL not found, disabling OpenCL support")
        set(WITH_OPENCL OFF)
    endif()
endif()

# Find CUDA
if(WITH_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        find_package(CUDAToolkit)
        if(CUDAToolkit_FOUND)
            message(STATUS "CUDA found: ${CUDAToolkit_VERSION}")
            add_definitions(-DWITH_CUDA)
            set(CMAKE_CUDA_STANDARD 17)
            set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        else()
            message(WARNING "CUDA Toolkit not found, disabling CUDA support")
            set(WITH_CUDA OFF)
        endif()
    else()
        message(WARNING "CUDA compiler not found, disabling CUDA support")
        set(WITH_CUDA OFF)
    endif()
endif()

# Blake3 library
add_subdirectory(third_party/blake3)

# Source files
set(CORE_SOURCES
    src/core/Miner.cpp
    src/core/Farm.cpp
)

set(TOSHASH_SOURCES
    src/toshash/TosHash.cpp
)

set(UTIL_SOURCES
    src/util/Log.cpp
)

set(STRATUM_SOURCES
    src/stratum/StratumClient.cpp
)

set(MAIN_SOURCES
    src/main.cpp
    src/MinerCLI.cpp
)

# OpenCL sources
if(WITH_OPENCL)
    set(OPENCL_SOURCES
        src/opencl/CLMiner.cpp
    )
    # Embed OpenCL kernel as header
    file(READ src/opencl/toshash_kernel.cl TOSHASH_CL_KERNEL HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," TOSHASH_CL_KERNEL ${TOSHASH_CL_KERNEL})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/toshash_kernel.cl.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/toshash_kernel.cl.h
    )
    include_directories(${CMAKE_CURRENT_BINARY_DIR})
endif()

# CUDA sources
if(WITH_CUDA)
    set(CUDA_SOURCES
        src/cuda/CUDAMiner.cpp
        src/cuda/toshash_kernel.cu
    )
endif()

# Combine all sources
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${TOSHASH_SOURCES}
    ${UTIL_SOURCES}
    ${STRATUM_SOURCES}
    ${MAIN_SOURCES}
)

if(WITH_OPENCL)
    list(APPEND ALL_SOURCES ${OPENCL_SOURCES})
endif()

if(WITH_CUDA)
    list(APPEND ALL_SOURCES ${CUDA_SOURCES})
endif()

# Create executable
add_executable(tosminer ${ALL_SOURCES})

# Include directories
target_include_directories(tosminer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${BLAKE3_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(tosminer PRIVATE
    Threads::Threads
    Boost::system
    Boost::program_options
    blake3
    nlohmann_json::nlohmann_json
)

if(WITH_OPENCL)
    target_link_libraries(tosminer PRIVATE ${OpenCL_LIBRARIES})
endif()

if(WITH_CUDA)
    target_link_libraries(tosminer PRIVATE CUDA::cudart)
    set_target_properties(tosminer PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
    )
endif()

if(WITH_TLS)
    target_link_libraries(tosminer PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# Installation
install(TARGETS tosminer
    RUNTIME DESTINATION bin
)

# Print configuration summary
message(STATUS "")
message(STATUS "=== TOS Miner Configuration ===")
message(STATUS "  Version:     ${PROJECT_VERSION}")
message(STATUS "  OpenCL:      ${WITH_OPENCL}")
message(STATUS "  CUDA:        ${WITH_CUDA}")
message(STATUS "  CPU Mining:  ${WITH_CPU}")
message(STATUS "  TLS/SSL:     ${WITH_TLS}")
message(STATUS "")
