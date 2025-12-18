/**
 * TOS Miner - CLI Implementation
 */

#include "MinerCLI.h"
#include "Version.h"
#include "core/TuningProfiles.h"
#include <boost/program_options.hpp>
#include <iostream>
#include <sstream>

namespace po = boost::program_options;

namespace tos {

MinerConfig MinerCLI::parse(int argc, char* argv[]) {
    MinerConfig config;

    po::options_description general("General options");
    general.add_options()
        ("help,h", "Show help message")
        ("version,V", "Show version")
        ("verbose,v", "Verbose output")
        ("quiet,q", "Quiet output (errors only)")
    ;

    po::options_description mining("Mining options");
    mining.add_options()
        ("pool,P", po::value<std::string>(), "Pool URL (stratum+tcp://host:port)")
        ("user,u", po::value<std::string>(), "Pool username (wallet.worker)")
        ("password,p", po::value<std::string>()->default_value("x"), "Pool password")
        ("stratum-protocol", po::value<std::string>()->default_value("stratum"),
         "Stratum protocol: stratum, ethproxy, ethereumstratum")
    ;

    po::options_description tls("TLS options");
    tls.add_options()
        ("tls-no-strict", "Disable strict TLS certificate verification (for self-signed certs)")
    ;

    po::options_description api("API options");
    api.add_options()
        ("api-port", po::value<unsigned>()->default_value(0),
         "JSON-RPC API port (0 = disabled)")
    ;

    po::options_description device("Device options");
    device.add_options()
        ("list-devices,L", "List available mining devices")
        ("opencl,G", "Use OpenCL devices")
        ("cuda,U", "Use CUDA devices")
        ("cpu,C", "Use CPU mining")
        ("cpu-threads,t", po::value<unsigned>()->default_value(0),
         "Number of CPU mining threads (0 = auto-detect all cores)")
        ("opencl-devices", po::value<std::string>(), "OpenCL device indices (e.g., 0,1,2)")
        ("cuda-devices", po::value<std::string>(), "CUDA device indices (e.g., 0,1)")
    ;

    po::options_description performance("Performance options");
    performance.add_options()
        ("profile", po::value<std::string>()->default_value("default"),
         "Tuning profile (default, nvidia-ampere, amd-rdna3, etc.)")
        ("list-profiles", "List available tuning profiles")
        ("opencl-global-work", po::value<unsigned>(),
         "OpenCL global work size (overrides profile)")
        ("opencl-local-work", po::value<unsigned>(),
         "OpenCL local work size (overrides profile)")
        ("cuda-grid", po::value<unsigned>(),
         "CUDA grid size (overrides profile)")
        ("cuda-block", po::value<unsigned>(),
         "CUDA block size (overrides profile)")
    ;

    po::options_description benchmark("Benchmark options");
    benchmark.add_options()
        ("benchmark,M", "Run benchmark")
        ("benchmark-iterations", po::value<uint64_t>()->default_value(1000),
         "Number of benchmark iterations")
    ;

    po::options_description all("TOS Miner Options");
    all.add(general).add(mining).add(tls).add(api).add(device).add(performance).add(benchmark);

    try {
        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, all), vm);
        po::notify(vm);

        // General options
        if (vm.count("help")) {
            config.showHelp = true;
            return config;
        }
        if (vm.count("version")) {
            config.showVersion = true;
            return config;
        }
        config.verbose = vm.count("verbose") > 0;
        config.quiet = vm.count("quiet") > 0;

        // List profiles
        if (vm.count("list-profiles")) {
            std::cout << "\nAvailable tuning profiles:\n";
            TuningProfiles::printProfiles();
            std::cout << std::endl;
            config.showHelp = true;  // Exit after showing
            return config;
        }

        // Mode selection
        if (vm.count("list-devices")) {
            config.mode = MiningMode::ListDevices;
            return config;
        }
        if (vm.count("benchmark")) {
            config.mode = MiningMode::Benchmark;
            if (vm.count("benchmark-iterations")) {
                config.benchmarkIterations = vm["benchmark-iterations"].as<uint64_t>();
            }
        } else if (vm.count("pool")) {
            config.mode = MiningMode::Stratum;
            config.poolUrl = vm["pool"].as<std::string>();
            if (vm.count("user")) {
                config.user = vm["user"].as<std::string>();
            }
            if (vm.count("password")) {
                config.password = vm["password"].as<std::string>();
            }
        }

        // Device selection
        config.useOpenCL = vm.count("opencl") > 0;
        config.useCUDA = vm.count("cuda") > 0;
        config.useCPU = vm.count("cpu") > 0;
        config.cpuThreads = vm["cpu-threads"].as<unsigned>();

        // If no specific device type selected, use all available
        if (!config.useOpenCL && !config.useCUDA && !config.useCPU) {
            config.useOpenCL = true;
            config.useCUDA = true;
        }

        if (vm.count("opencl-devices")) {
            config.openclDevices = parseDeviceList(vm["opencl-devices"].as<std::string>());
        }
        if (vm.count("cuda-devices")) {
            config.cudaDevices = parseDeviceList(vm["cuda-devices"].as<std::string>());
        }

        // Performance options - apply profile first, then allow overrides
        config.tuningProfile = vm["profile"].as<std::string>();
        const auto& profile = TuningProfiles::getProfile(config.tuningProfile);

        // Set defaults from profile
        config.openclGlobalWorkSize = profile.openclGlobalWorkSize;
        config.openclLocalWorkSize = profile.openclLocalWorkSize;
        config.cudaGridSize = profile.cudaGridSize;
        config.cudaBlockSize = profile.cudaBlockSize;

        // Allow manual overrides
        if (vm.count("opencl-global-work")) {
            config.openclGlobalWorkSize = vm["opencl-global-work"].as<unsigned>();
        }
        if (vm.count("opencl-local-work")) {
            config.openclLocalWorkSize = vm["opencl-local-work"].as<unsigned>();
        }
        if (vm.count("cuda-grid")) {
            config.cudaGridSize = vm["cuda-grid"].as<unsigned>();
        }
        if (vm.count("cuda-block")) {
            config.cudaBlockSize = vm["cuda-block"].as<unsigned>();
        }

        // TLS options (strict by default, --tls-no-strict disables)
        config.tlsStrict = vm.count("tls-no-strict") == 0;

        // API options
        config.apiPort = vm["api-port"].as<unsigned>();

        // Stratum protocol
        config.stratumProtocol = vm["stratum-protocol"].as<std::string>();

    } catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        config.showHelp = true;
    }

    return config;
}

void MinerCLI::printHelp() {
    std::cout << R"(
TOS Miner v1.0.0 - GPU miner for TOS Hash V3

Usage: tosminer [OPTIONS]

General Options:
  -h, --help                Show this help message
  -V, --version             Show version
  -v, --verbose             Verbose output
  -q, --quiet               Quiet output (errors only)

Mining Options:
  -P, --pool URL            Pool URL (stratum+tcp://host:port or stratum+ssl://host:port)
  -u, --user USER           Pool username (wallet.worker)
  -p, --password PASS       Pool password (default: x)
  --stratum-protocol PROTO  Protocol variant: stratum, ethproxy, ethereumstratum

TLS Options:
  --tls-no-strict           Disable strict TLS certificate verification
                            (default: strict verification enabled)

API Options:
  --api-port PORT           JSON-RPC API port for monitoring (0 = disabled)

Device Options:
  -L, --list-devices        List available mining devices
  -G, --opencl              Use OpenCL (GPU) mining
  -U, --cuda                Use CUDA (NVIDIA GPU) mining
  -C, --cpu                 Use CPU mining
  -t, --cpu-threads N       Number of CPU threads (0 = auto-detect all cores)
  --opencl-devices LIST     Comma-separated OpenCL device indices
  --cuda-devices LIST       Comma-separated CUDA device indices

Performance Options:
  --profile NAME            Tuning profile (default, nvidia-ampere, amd-rdna3, etc.)
  --list-profiles           List all available tuning profiles
  --opencl-global-work N    OpenCL global work size (overrides profile)
  --opencl-local-work N     OpenCL local work size (overrides profile)
  --cuda-grid N             CUDA grid size (overrides profile)
  --cuda-block N            CUDA block size (overrides profile)

Benchmark Options:
  -M, --benchmark           Run benchmark mode
  --benchmark-iterations N  Number of iterations (default: 1000)

Examples:
  tosminer --benchmark                     Run benchmark
  tosminer -L                              List devices
  tosminer -G -P stratum+tcp://pool:3333 -u wallet.worker
                                           Mine with OpenCL
  tosminer -P stratum+ssl://pool:3334 -u wallet
                                           Mine with TLS (strict verification)
  tosminer -P stratum+ssl://pool:3334 -u wallet --tls-no-strict
                                           Mine with TLS (self-signed certs)
  tosminer -P stratum+tcp://pool:3333 -u wallet --api-port 3000
                                           Mine with monitoring API on port 3000

)" << std::endl;
}

void MinerCLI::printVersion() {
    std::cout << getVersionString() << std::endl;
    std::cout << "TOS Hash V3 GPU/ASIC Mining Software" << std::endl;
    std::cout << std::endl;
    std::cout << "Build options:" << std::endl;
#ifdef WITH_OPENCL
    std::cout << "  OpenCL: enabled" << std::endl;
#else
    std::cout << "  OpenCL: disabled" << std::endl;
#endif
#ifdef WITH_CUDA
    std::cout << "  CUDA: enabled" << std::endl;
#else
    std::cout << "  CUDA: disabled" << std::endl;
#endif
#ifdef WITH_TLS
    std::cout << "  TLS:    enabled" << std::endl;
#else
    std::cout << "  TLS:    disabled" << std::endl;
#endif
}

std::vector<unsigned> MinerCLI::parseDeviceList(const std::string& str) {
    std::vector<unsigned> result;
    std::stringstream ss(str);
    std::string item;

    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(std::stoul(item));
        } catch (...) {
            // Skip invalid entries
        }
    }

    return result;
}

}  // namespace tos
