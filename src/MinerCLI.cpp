/**
 * TOS Miner - CLI Implementation
 */

#include "MinerCLI.h"
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
    ;

    po::options_description device("Device options");
    device.add_options()
        ("list-devices,L", "List available mining devices")
        ("opencl,G", "Use OpenCL devices")
        ("cuda,U", "Use CUDA devices")
        ("cpu,C", "Use CPU mining")
        ("opencl-devices", po::value<std::string>(), "OpenCL device indices (e.g., 0,1,2)")
        ("cuda-devices", po::value<std::string>(), "CUDA device indices (e.g., 0,1)")
    ;

    po::options_description performance("Performance options");
    performance.add_options()
        ("opencl-global-work", po::value<unsigned>()->default_value(16384),
         "OpenCL global work size")
        ("opencl-local-work", po::value<unsigned>()->default_value(1),
         "OpenCL local work size")
        ("cuda-grid", po::value<unsigned>()->default_value(16384),
         "CUDA grid size")
        ("cuda-block", po::value<unsigned>()->default_value(1),
         "CUDA block size")
    ;

    po::options_description benchmark("Benchmark options");
    benchmark.add_options()
        ("benchmark,M", "Run benchmark")
        ("benchmark-iterations", po::value<uint64_t>()->default_value(1000),
         "Number of benchmark iterations")
    ;

    po::options_description all("TOS Miner Options");
    all.add(general).add(mining).add(device).add(performance).add(benchmark);

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

        // Performance options
        config.openclGlobalWorkSize = vm["opencl-global-work"].as<unsigned>();
        config.openclLocalWorkSize = vm["opencl-local-work"].as<unsigned>();
        config.cudaGridSize = vm["cuda-grid"].as<unsigned>();
        config.cudaBlockSize = vm["cuda-block"].as<unsigned>();

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
  -P, --pool URL            Pool URL (stratum+tcp://host:port)
  -u, --user USER           Pool username (wallet.worker)
  -p, --password PASS       Pool password (default: x)

Device Options:
  -L, --list-devices        List available mining devices
  -G, --opencl              Use OpenCL (GPU) mining
  -U, --cuda                Use CUDA (NVIDIA GPU) mining
  -C, --cpu                 Use CPU mining
  --opencl-devices LIST     Comma-separated OpenCL device indices
  --cuda-devices LIST       Comma-separated CUDA device indices

Performance Options:
  --opencl-global-work N    OpenCL global work size (default: 16384)
  --opencl-local-work N     OpenCL local work size (default: 1)
  --cuda-grid N             CUDA grid size (default: 16384)
  --cuda-block N            CUDA block size (default: 1)

Benchmark Options:
  -M, --benchmark           Run benchmark mode
  --benchmark-iterations N  Number of iterations (default: 1000)

Examples:
  tosminer --benchmark                     Run benchmark
  tosminer -L                              List devices
  tosminer -G -P stratum+tcp://pool:3333 -u wallet.worker
                                           Mine with OpenCL

)" << std::endl;
}

void MinerCLI::printVersion() {
    std::cout << "TOS Miner v1.0.0" << std::endl;
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
