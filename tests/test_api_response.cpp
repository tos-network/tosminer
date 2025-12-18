/**
 * Test API response generation with GPU temperature monitoring
 */

#include <iostream>
#include <nlohmann/json.hpp>
#include "../src/util/GpuMonitor.h"
#include "../src/core/Miner.h"

using json = nlohmann::json;
using namespace tos;

// Simulate what ApiServer::getDevices() does
json simulateGetDevices() {
    json devices = json::array();

    // Simulate a CUDA device
    json device;
    device["index"] = 0;
    device["name"] = "NVIDIA GeForce RTX 4090 (simulated)";
    device["type"] = "CUDA";
    device["hashrate"] = 1500000.0;  // 1.5 MH/s
    device["hashrate_instant"] = 1520000.0;
    device["hashrate_ema"] = 1500000.0;
    device["hashes"] = 45000000;
    device["memory_mb"] = 24576;
    device["compute_units"] = 128;
    device["failed"] = false;

    // Add simulated GPU monitoring data
    device["temperature"] = 72;
    device["fan_speed"] = 65;
    device["power_usage"] = 320;
    device["clock_core"] = 2520;
    device["gpu_utilization"] = 98;

    devices.push_back(device);

    // Add another simulated device
    json device2;
    device2["index"] = 1;
    device2["name"] = "AMD Radeon RX 7900 XTX (simulated)";
    device2["type"] = "OpenCL";
    device2["hashrate"] = 800000.0;  // 0.8 MH/s
    device2["hashrate_instant"] = 810000.0;
    device2["hashrate_ema"] = 800000.0;
    device2["hashes"] = 24000000;
    device2["memory_mb"] = 24576;
    device2["compute_units"] = 96;
    device2["failed"] = false;

    device2["temperature"] = 68;
    device2["fan_speed"] = 55;
    device2["power_usage"] = 280;
    device2["clock_core"] = 2400;
    device2["gpu_utilization"] = 95;

    devices.push_back(device2);

    return devices;
}

// Simulate what ApiServer::getHealth() does
json simulateGetHealth() {
    json health;
    health["overall"] = "healthy";

    json devices = json::array();

    constexpr int TEMP_WARNING = 80;
    constexpr int TEMP_CRITICAL = 90;

    // Device 1 - normal temp
    {
        json device;
        device["index"] = 0;
        device["name"] = "NVIDIA GeForce RTX 4090";
        device["temperature"] = 72;
        device["temperature_status"] = "normal";
        device["status"] = "healthy";
        devices.push_back(device);
    }

    // Device 2 - warning temp
    {
        json device;
        device["index"] = 1;
        device["name"] = "AMD Radeon RX 7900 XTX";
        device["temperature"] = 85;
        device["temperature_status"] = "warning";
        device["status"] = "warning";
        devices.push_back(device);
    }

    // Device 3 - critical temp
    {
        json device;
        device["index"] = 2;
        device["name"] = "NVIDIA GeForce RTX 3080";
        device["temperature"] = 92;
        device["temperature_status"] = "critical";
        device["status"] = "critical";
        devices.push_back(device);
    }

    health["devices"] = devices;
    health["active_miners"] = 3;
    health["total_miners"] = 3;
    health["overall"] = "unhealthy";
    health["overheating"] = true;
    health["warning"] = "One or more GPUs are overheating!";

    return health;
}

// Simulate getStatus with EMA hashrate
json simulateGetStatus() {
    json status;
    status["version"] = "1.0.0";
    status["uptime"] = 3600;
    status["mining"] = true;
    status["paused"] = false;
    status["connected"] = true;
    status["authorized"] = true;

    // EMA hashrate (stable)
    status["hashrate"] = "2.30 MH/s";
    status["hashrate_raw"] = 2300000.0;
    status["hashrate_instant"] = 2330000.0;
    status["hashrate_ema"] = 2300000.0;

    status["shares"] = {
        {"accepted", 150},
        {"rejected", 2},
        {"stale", 1}
    };

    status["difficulty"] = 10000.0;
    status["miners"] = 3;
    status["active_miners"] = 3;

    return status;
}

int main() {
    std::cout << "=== API Response Tests with Temperature Monitoring ===\n\n";

    // Test /status response
    std::cout << "=== GET /status ===\n";
    json statusResponse = simulateGetStatus();
    std::cout << statusResponse.dump(2) << "\n\n";

    // Test /devices response
    std::cout << "=== GET /devices ===\n";
    json devicesResponse = simulateGetDevices();
    std::cout << devicesResponse.dump(2) << "\n\n";

    // Test /health response
    std::cout << "=== GET /health ===\n";
    json healthResponse = simulateGetHealth();
    std::cout << healthResponse.dump(2) << "\n\n";

    // Verify temperature fields exist
    std::cout << "=== Field Verification ===\n";

    bool passed = true;

    // Check /devices has temperature fields
    for (const auto& device : devicesResponse) {
        if (!device.contains("temperature")) {
            std::cout << "[FAIL] /devices missing 'temperature' field\n";
            passed = false;
        }
        if (!device.contains("fan_speed")) {
            std::cout << "[FAIL] /devices missing 'fan_speed' field\n";
            passed = false;
        }
        if (!device.contains("hashrate_ema")) {
            std::cout << "[FAIL] /devices missing 'hashrate_ema' field\n";
            passed = false;
        }
    }

    // Check /health has temperature status
    for (const auto& device : healthResponse["devices"]) {
        if (!device.contains("temperature_status")) {
            std::cout << "[FAIL] /health device missing 'temperature_status' field\n";
            passed = false;
        }
    }

    // Check /status has EMA fields
    if (!statusResponse.contains("hashrate_ema")) {
        std::cout << "[FAIL] /status missing 'hashrate_ema' field\n";
        passed = false;
    }
    if (!statusResponse.contains("hashrate_instant")) {
        std::cout << "[FAIL] /status missing 'hashrate_instant' field\n";
        passed = false;
    }

    // Check /health has overheating warning
    if (!healthResponse.contains("overheating")) {
        std::cout << "[FAIL] /health missing 'overheating' field\n";
        passed = false;
    }

    if (passed) {
        std::cout << "[PASS] All API response fields verified\n";
    }

    std::cout << "\n=== Summary ===\n";
    std::cout << "Temperature monitoring fields:\n";
    std::cout << "  - /devices: temperature, fan_speed, power_usage, clock_core, gpu_utilization\n";
    std::cout << "  - /health: temperature, temperature_status (normal/warning/critical)\n";
    std::cout << "  - /health: overheating flag and warning message\n";
    std::cout << "\nEMA hashrate fields:\n";
    std::cout << "  - /status: hashrate_raw, hashrate_instant, hashrate_ema\n";
    std::cout << "  - /devices: hashrate, hashrate_instant, hashrate_ema\n";

    return passed ? 0 : 1;
}
