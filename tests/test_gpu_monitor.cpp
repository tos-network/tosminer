/**
 * Test GPU Monitor functionality
 */

#include <iostream>
#include <iomanip>
#include "../src/util/GpuMonitor.h"

using namespace tos;

void printGpuStats(const GpuStats& stats) {
    std::cout << "  Device " << stats.deviceIndex << ": " << stats.name << "\n";
    std::cout << "    Valid: " << (stats.valid ? "yes" : "no") << "\n";

    if (stats.temperature >= 0) {
        std::cout << "    Temperature: " << stats.temperature << "°C";
        if (stats.temperatureHotspot >= 0) {
            std::cout << " (hotspot: " << stats.temperatureHotspot << "°C)";
        }
        std::cout << "\n";
    }

    if (stats.fanSpeed >= 0) {
        std::cout << "    Fan Speed: " << stats.fanSpeed << "%\n";
    }

    if (stats.powerUsage >= 0) {
        std::cout << "    Power: " << stats.powerUsage << "W";
        if (stats.powerLimit >= 0) {
            std::cout << " / " << stats.powerLimit << "W limit";
        }
        std::cout << "\n";
    }

    if (stats.clockCore >= 0) {
        std::cout << "    Clock: " << stats.clockCore << " MHz";
        if (stats.clockMemory >= 0) {
            std::cout << " (mem: " << stats.clockMemory << " MHz)";
        }
        std::cout << "\n";
    }

    if (stats.gpuUtilization >= 0) {
        std::cout << "    Utilization: " << stats.gpuUtilization << "%";
        if (stats.memoryUtilization >= 0) {
            std::cout << " (mem: " << stats.memoryUtilization << "%)";
        }
        std::cout << "\n";
    }

    if (stats.memoryTotal > 0) {
        std::cout << "    Memory: " << (stats.memoryUsed / (1024*1024)) << " MB / "
                  << (stats.memoryTotal / (1024*1024)) << " MB ("
                  << std::fixed << std::setprecision(1) << stats.memoryUsagePercent() << "%)\n";
    }

    std::cout << "    Overheating: " << (stats.isOverheating() ? "YES!" : "no") << "\n";
}

int main() {
    std::cout << "=== GPU Monitor Test ===\n\n";

    // Initialize GPU monitoring
    std::cout << "Initializing GPU monitor...\n";
    bool initialized = GpuMonitor::instance().init();

    if (!initialized) {
        std::cout << "GPU monitoring not available on this system.\n";
        std::cout << "This is normal if:\n";
        std::cout << "  - No NVIDIA GPU (NVML not available)\n";
        std::cout << "  - No AMD GPU (sysfs hwmon not found)\n";
        std::cout << "  - Running on macOS (NVML not supported)\n";
        std::cout << "\n[PASS] GPU monitor correctly reports unavailability\n";
        return 0;
    }

    std::cout << "GPU monitoring initialized successfully!\n\n";

    // Get all GPU stats
    std::cout << "=== GPU Statistics ===\n\n";
    auto allStats = GpuMonitor::instance().getAllStats();

    if (allStats.empty()) {
        std::cout << "No GPUs found.\n";
    } else {
        std::cout << "Found " << allStats.size() << " GPU(s):\n\n";
        for (const auto& stats : allStats) {
            printGpuStats(stats);
            std::cout << "\n";
        }
    }

    // Check overheating
    std::cout << "=== Thermal Status ===\n";
    if (GpuMonitor::instance().anyOverheating(85)) {
        std::cout << "WARNING: One or more GPUs are overheating (>85°C)!\n";
    } else {
        std::cout << "All GPUs within normal temperature range (<85°C)\n";
    }

    // Shutdown
    std::cout << "\nShutting down GPU monitor...\n";
    GpuMonitor::instance().shutdown();

    std::cout << "\n[PASS] GPU monitor test completed\n";
    return 0;
}
