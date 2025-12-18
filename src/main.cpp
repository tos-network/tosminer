/**
 * TOS Miner - Main Entry Point
 *
 * GPU miner for TOS Hash V3 algorithm
 */

#include "MinerCLI.h"
#include "core/Farm.h"
#include "core/Miner.h"
#include "toshash/TosHash.h"
#include "stratum/StratumClient.h"
#include "api/ApiServer.h"
#include "util/Log.h"

#ifdef WITH_OPENCL
#include "opencl/CLMiner.h"
#endif

#ifdef WITH_CUDA
#include "cuda/CUDAMiner.h"
#endif

#include <iostream>
#include <iomanip>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

using namespace tos;

// Global flag for graceful shutdown
static std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        Log::info("Shutdown requested...");
        g_running = false;
    }
}

void listDevices() {
    std::cout << "\n=== Available Mining Devices ===\n\n";

    // CPU
    std::cout << "CPU:\n";
    std::cout << "  [0] CPU (" << std::thread::hardware_concurrency() << " threads)\n";

#ifdef WITH_OPENCL
    std::cout << "\nOpenCL Devices:\n";
    auto clDevices = CLMiner::enumDevices();
    if (clDevices.empty()) {
        std::cout << "  None found\n";
    } else {
        for (const auto& dev : clDevices) {
            std::cout << "  [" << dev.index << "] " << dev.name
                      << " (" << (dev.totalMemory / (1024 * 1024)) << " MB, "
                      << dev.computeUnits << " CUs)\n";
            std::cout << "       Platform: " << dev.clPlatformName << "\n";
        }
    }
#else
    std::cout << "\nOpenCL: Not compiled\n";
#endif

#ifdef WITH_CUDA
    std::cout << "\nCUDA Devices:\n";
    auto cuDevices = CUDAMiner::enumDevices();
    if (cuDevices.empty()) {
        std::cout << "  None found\n";
    } else {
        for (const auto& dev : cuDevices) {
            std::cout << "  [" << dev.index << "] " << dev.name
                      << " (SM " << dev.cudaComputeCapabilityMajor << "."
                      << dev.cudaComputeCapabilityMinor << ", "
                      << (dev.totalMemory / (1024 * 1024)) << " MB)\n";
        }
    }
#else
    std::cout << "\nCUDA: Not compiled\n";
#endif

    std::cout << std::endl;
}

void runBenchmark(const MinerConfig& config) {
    Log::info("Starting benchmark...");

    TosHash hasher;

    // CPU benchmark
    Log::info("Running CPU benchmark (" + std::to_string(config.benchmarkIterations) + " iterations)...");

    double hashRate = hasher.benchmark(config.benchmarkIterations);

    std::cout << "\n=== Benchmark Results ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "CPU Hash Rate: " << hashRate << " H/s\n";

    // Time per hash
    double usPerHash = 1000000.0 / hashRate;
    std::cout << "Time per hash: " << usPerHash << " µs\n";

    // GPU benchmark would go here
#ifdef WITH_OPENCL
    if (config.useOpenCL) {
        Log::info("OpenCL benchmark not yet implemented");
    }
#endif

#ifdef WITH_CUDA
    if (config.useCUDA) {
        Log::info("CUDA benchmark not yet implemented");
    }
#endif

    std::cout << std::endl;
}

void runMining(const MinerConfig& config) {
    Log::info("Starting TOS Miner...");

    Farm farm;
    StratumClient stratum;

    // Set up stratum callbacks
    stratum.setWorkCallback([&farm](const WorkPackage& work) {
        farm.setWork(work);
    });

    stratum.setShareCallback([&farm](bool accepted, const std::string& reason) {
        if (accepted) {
            Log::info("Share accepted");
            farm.recordAcceptedShare();
        } else {
            Log::warning("Share rejected: " + reason);
            farm.recordRejectedShare();
        }
    });

    // Configure TLS
    stratum.setTlsVerification(config.tlsStrict);

    // Configure protocol variant
    stratum.setProtocol(parseStratumProtocol(config.stratumProtocol));

    // Connect to pool
    stratum.setCredentials(config.user, config.password);
    if (!stratum.connectUrl(config.poolUrl)) {
        Log::error("Failed to connect to pool: " + stratum.getLastError());
        return;
    }

    // Wait for authorization
    int timeout = 10;
    while (!stratum.isAuthorized() && timeout > 0 && g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        timeout--;
    }

    if (!stratum.isAuthorized()) {
        Log::error("Failed to authorize with pool");
        return;
    }

    // Add miners to farm
#ifdef WITH_OPENCL
    if (config.useOpenCL) {
        CLMiner::setGlobalWorkSizeMultiplier(config.openclGlobalWorkSize);
        CLMiner::setLocalWorkSize(config.openclLocalWorkSize);

        auto devices = CLMiner::enumDevices();
        for (const auto& dev : devices) {
            // Check if device is in selection list (or list is empty = all)
            if (config.openclDevices.empty() ||
                std::find(config.openclDevices.begin(), config.openclDevices.end(),
                         dev.index) != config.openclDevices.end()) {
                farm.addMiner(std::make_unique<CLMiner>(dev.index, dev));
            }
        }
    }
#endif

#ifdef WITH_CUDA
    if (config.useCUDA) {
        CUDAMiner::setGridSizeMultiplier(config.cudaGridSize);
        CUDAMiner::setBlockSize(config.cudaBlockSize);

        auto devices = CUDAMiner::enumDevices();
        for (const auto& dev : devices) {
            if (config.cudaDevices.empty() ||
                std::find(config.cudaDevices.begin(), config.cudaDevices.end(),
                         dev.index) != config.cudaDevices.end()) {
                farm.addMiner(std::make_unique<CUDAMiner>(dev.index, dev));
            }
        }
    }
#endif

    if (farm.minerCount() == 0) {
        Log::error("No mining devices available");
        return;
    }

    // Set solution callback
    farm.setSolutionCallback([&stratum](const Solution& sol, const std::string& jobId) {
        stratum.submitSolution(sol, jobId);
    });

    // Start mining
    if (!farm.start()) {
        Log::error("Failed to start mining");
        return;
    }

    // Start API server if configured
    std::unique_ptr<ApiServer> apiServer;
    if (config.apiPort > 0) {
        apiServer = std::make_unique<ApiServer>(config.apiPort, farm, stratum);
        if (!apiServer->start()) {
            Log::warning("Failed to start API server, continuing without it");
            apiServer.reset();
        }
    }

    // Main loop - print stats periodically
    auto lastStats = std::chrono::steady_clock::now();

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - lastStats).count();

        if (elapsed >= 10.0) {  // Print stats every 10 seconds
            lastStats = now;

            auto hr = farm.getHashRate();
            auto stats = farm.getStats();

            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2);

            if (hr.rate >= 1000000) {
                ss << (hr.rate / 1000000) << " MH/s";
            } else if (hr.rate >= 1000) {
                ss << (hr.rate / 1000) << " KH/s";
            } else {
                ss << hr.rate << " H/s";
            }

            ss << " | A:" << stats.acceptedShares
               << " R:" << stats.rejectedShares
               << " S:" << stats.staleShares;

            Log::info(ss.str());
        }
    }

    // Graceful shutdown
    Log::info("Shutting down...");

    // Stop API server first
    if (apiServer) {
        apiServer->stop();
    }

    // Stop miners (they might still be submitting solutions)
    farm.stop();

    // Wait for pending share submissions with 5 second timeout
    stratum.gracefulDisconnect(5000);

    Log::info("Shutdown complete");
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Parse command line
    MinerConfig config = MinerCLI::parse(argc, argv);

    // Handle help/version
    if (config.showHelp) {
        MinerCLI::printHelp();
        return 0;
    }

    if (config.showVersion) {
        MinerCLI::printVersion();
        return 0;
    }

    // Configure logging
    if (config.verbose) {
        Log::setLevel(LogLevel::Debug);
    } else if (config.quiet) {
        Log::setLevel(LogLevel::Error);
    }

    Log::setShowTimestamp(true);

    // Run appropriate mode
    switch (config.mode) {
        case MiningMode::ListDevices:
            listDevices();
            break;

        case MiningMode::Benchmark:
            runBenchmark(config);
            break;

        case MiningMode::Stratum:
            if (config.poolUrl.empty()) {
                Log::error("Pool URL required for mining. Use -P stratum+tcp://host:port");
                return 1;
            }
            if (config.user.empty()) {
                Log::error("Username required for mining. Use -u wallet.worker");
                return 1;
            }
            runMining(config);
            break;
    }

    return 0;
}
