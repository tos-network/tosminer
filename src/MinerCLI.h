/**
 * TOS Miner - CLI Argument Parsing
 */

#pragma once

#include "core/Types.h"
#include <string>
#include <vector>

namespace tos {

/**
 * Mining mode
 */
enum class MiningMode {
    Stratum,      // Connect to pool via stratum
    Benchmark,    // Run benchmark
    ListDevices   // List available devices
};

/**
 * CLI configuration
 */
struct MinerConfig {
    // Mining mode
    MiningMode mode = MiningMode::Benchmark;

    // Pool connection
    std::string poolUrl;
    std::string user;
    std::string password;

    // Device selection
    bool useCPU = false;
    bool useOpenCL = true;
    bool useCUDA = true;
    std::vector<unsigned> openclDevices;
    std::vector<unsigned> cudaDevices;

    // Performance tuning
    std::string tuningProfile = "default";  // Tuning profile name
    unsigned openclGlobalWorkSize = 16384;
    unsigned openclLocalWorkSize = 1;
    unsigned cudaGridSize = 16384;
    unsigned cudaBlockSize = 1;

    // Benchmark options
    uint64_t benchmarkIterations = 1000;

    // TLS options
    bool tlsStrict = false;  // Strict certificate verification

    // API/Monitoring
    unsigned apiPort = 0;    // 0 = disabled, otherwise JSON-RPC port

    // Stratum protocol variant
    std::string stratumProtocol = "stratum";  // stratum, ethproxy, ethereumstratum

    // Logging
    bool verbose = false;
    bool quiet = false;

    // Help
    bool showHelp = false;
    bool showVersion = false;
};

/**
 * MinerCLI class
 *
 * Parses command line arguments
 */
class MinerCLI {
public:
    /**
     * Parse command line arguments
     *
     * @param argc Argument count
     * @param argv Argument values
     * @return Parsed configuration
     */
    static MinerConfig parse(int argc, char* argv[]);

    /**
     * Print help message
     */
    static void printHelp();

    /**
     * Print version
     */
    static void printVersion();

private:
    /**
     * Parse device list string (e.g., "0,1,2")
     */
    static std::vector<unsigned> parseDeviceList(const std::string& str);
};

}  // namespace tos
