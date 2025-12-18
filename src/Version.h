/**
 * TOS Miner - Version Information
 */

#pragma once

#include <string>

namespace tos {

// Version components
constexpr int VERSION_MAJOR = 1;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 0;

// Version string for display
constexpr const char* VERSION_STRING = "1.0.0";

// Full version with name for Stratum
constexpr const char* MINER_VERSION = "tosminer/1.0.0";

// User agent string
inline std::string getUserAgent() {
    return std::string(MINER_VERSION);
}

// Get version with optional build info
inline std::string getVersionString() {
    std::string version = "TOS Miner v" + std::string(VERSION_STRING);
#ifdef WITH_OPENCL
    version += " +OpenCL";
#endif
#ifdef WITH_CUDA
    version += " +CUDA";
#endif
#ifdef WITH_TLS
    version += " +TLS";
#endif
    return version;
}

}  // namespace tos
