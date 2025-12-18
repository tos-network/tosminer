/**
 * TOS Miner - JSON-RPC API Server Implementation
 */

#include "ApiServer.h"
#include "Version.h"
#include "util/Log.h"
#include "util/GpuMonitor.h"
#include <sstream>
#include <iomanip>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace tos {

ApiServer::ApiServer(unsigned port, Farm& farm, StratumClient& stratum)
    : m_port(port)
    , m_farm(farm)
    , m_stratum(stratum)
{
}

ApiServer::~ApiServer() {
    stop();
}

bool ApiServer::start() {
    if (m_running || m_port == 0) {
        return false;
    }

    try {
        m_acceptor = std::make_unique<tcp::acceptor>(
            m_io, tcp::endpoint(tcp::v4(), m_port)
        );
        m_acceptor->set_option(asio::socket_base::reuse_address(true));

        m_running = true;
        m_thread = std::thread([this]() { acceptLoop(); });

        Log::info("API server started on port " + std::to_string(m_port));
        return true;
    } catch (const std::exception& e) {
        Log::error("Failed to start API server: " + std::string(e.what()));
        return false;
    }
}

void ApiServer::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;
    m_io.stop();

    if (m_acceptor) {
        boost::system::error_code ec;
        m_acceptor->close(ec);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    Log::info("API server stopped");
}

void ApiServer::acceptLoop() {
    while (m_running) {
        try {
            auto socket = std::make_shared<tcp::socket>(m_io);
            boost::system::error_code ec;
            m_acceptor->accept(*socket, ec);

            if (ec) {
                if (m_running) {
                    Log::debug("API accept error: " + ec.message());
                }
                continue;
            }

            // Handle client in detached thread (simple approach)
            std::thread([this, socket]() {
                handleClient(socket);
            }).detach();

        } catch (const std::exception& e) {
            if (m_running) {
                Log::debug("API server error: " + std::string(e.what()));
            }
        }
    }
}

void ApiServer::handleClient(std::shared_ptr<tcp::socket> socket) {
    try {
        // Read request
        asio::streambuf buffer;
        boost::system::error_code ec;
        asio::read_until(*socket, buffer, "\r\n\r\n", ec);

        if (ec) {
            return;
        }

        std::istream is(&buffer);
        std::string request;
        std::getline(is, request);

        // Generate response
        std::string response = handleRequest(request);

        // Send response
        asio::write(*socket, asio::buffer(response), ec);

        // Close connection
        socket->shutdown(tcp::socket::shutdown_both, ec);
        socket->close(ec);

    } catch (...) {
        // Ignore client errors
    }
}

std::string ApiServer::handleRequest(const std::string& request) {
    // Parse HTTP request line: "GET /path HTTP/1.1"
    std::string method, path;
    std::istringstream iss(request);
    iss >> method >> path;

    // Only handle GET requests
    if (method != "GET") {
        return createResponse(405, R"({"error":"Method not allowed"})");
    }

    // Route to appropriate handler
    json result;
    if (path == "/" || path == "/status") {
        result = getStatus();
    } else if (path == "/stats") {
        result = getStats();
    } else if (path == "/devices") {
        result = getDevices();
    } else if (path == "/health") {
        result = getHealth();
    } else {
        return createResponse(404, R"({"error":"Not found"})");
    }

    return createResponse(200, result.dump(2));
}

json ApiServer::getStatus() {
    auto hr = m_farm.getHashRate();
    auto stats = m_farm.getStats();

    json status;
    status["version"] = VERSION_STRING;
    status["uptime"] = static_cast<uint64_t>(hr.duration);
    status["mining"] = m_farm.isRunning();
    status["paused"] = m_farm.isPaused();
    status["connected"] = m_stratum.isConnected();
    status["authorized"] = m_stratum.isAuthorized();

    // Hash rate with appropriate unit (use EMA for stable display)
    double displayRate = hr.effectiveRate();
    if (displayRate >= 1000000) {
        status["hashrate"] = std::to_string(displayRate / 1000000) + " MH/s";
    } else if (displayRate >= 1000) {
        status["hashrate"] = std::to_string(displayRate / 1000) + " KH/s";
    } else {
        status["hashrate"] = std::to_string(displayRate) + " H/s";
    }
    status["hashrate_raw"] = displayRate;
    status["hashrate_instant"] = hr.rate;
    status["hashrate_ema"] = hr.emaRate;

    status["shares"] = {
        {"accepted", stats.acceptedShares},
        {"rejected", stats.rejectedShares},
        {"stale", stats.staleShares}
    };

    status["difficulty"] = m_stratum.getDifficulty();
    status["miners"] = m_farm.minerCount();
    status["active_miners"] = m_farm.activeMinerCount();

    return status;
}

json ApiServer::getStats() {
    auto hr = m_farm.getHashRate();
    auto stats = m_farm.getStats();

    json result;
    result["hashrate"] = hr.effectiveRate();  // EMA rate for stability
    result["hashrate_instant"] = hr.rate;
    result["hashrate_ema"] = hr.emaRate;
    result["hashes"] = hr.count;
    result["duration"] = hr.duration;
    result["accepted"] = stats.acceptedShares;
    result["rejected"] = stats.rejectedShares;
    result["stale"] = stats.staleShares;

    // Calculate efficiency
    uint64_t total = stats.acceptedShares + stats.rejectedShares + stats.staleShares;
    result["efficiency"] = total > 0 ?
        static_cast<double>(stats.acceptedShares) / total * 100.0 : 100.0;

    // Pool stats
    result["pool"] = {
        {"connected", m_stratum.isConnected()},
        {"difficulty", m_stratum.getDifficulty()},
        {"accepted", m_stratum.getAcceptedShares()},
        {"rejected", m_stratum.getRejectedShares()}
    };

    return result;
}

json ApiServer::getDevices() {
    json devices = json::array();
    auto descriptors = m_farm.getDevices();

    for (size_t i = 0; i < descriptors.size(); i++) {
        const auto& dev = descriptors[i];
        auto hr = m_farm.getMinerHashRate(static_cast<unsigned>(i));

        json device;
        device["index"] = dev.index;
        device["name"] = dev.name;
        device["type"] = dev.type == MinerType::CPU ? "CPU" :
                         dev.type == MinerType::OpenCL ? "OpenCL" : "CUDA";
        device["hashrate"] = hr.effectiveRate();  // EMA rate
        device["hashrate_instant"] = hr.rate;
        device["hashrate_ema"] = hr.emaRate;
        device["hashes"] = hr.count;
        device["memory_mb"] = dev.totalMemory / (1024 * 1024);
        device["compute_units"] = dev.computeUnits;
        device["failed"] = m_farm.isMinerFailed(static_cast<unsigned>(i));

        // Add GPU monitoring data if available
        GpuStats gpuStats;
        if (dev.type == MinerType::CUDA) {
            gpuStats = GpuMonitor::instance().getNvidiaStats(dev.cudaDeviceIndex);
        } else if (dev.type == MinerType::OpenCL) {
            gpuStats = GpuMonitor::instance().getAmdStats(dev.clDeviceIndex);
        }

        if (gpuStats.valid) {
            if (gpuStats.temperature >= 0) {
                device["temperature"] = gpuStats.temperature;
            }
            if (gpuStats.fanSpeed >= 0) {
                device["fan_speed"] = gpuStats.fanSpeed;
            }
            if (gpuStats.powerUsage >= 0) {
                device["power_usage"] = gpuStats.powerUsage;
            }
            if (gpuStats.clockCore >= 0) {
                device["clock_core"] = gpuStats.clockCore;
            }
            if (gpuStats.gpuUtilization >= 0) {
                device["gpu_utilization"] = gpuStats.gpuUtilization;
            }
        }

        devices.push_back(device);
    }

    return devices;
}

json ApiServer::getHealth() {
    json health;
    health["overall"] = "healthy";  // Will be downgraded if any device is unhealthy

    json devices = json::array();
    auto descriptors = m_farm.getDevices();

    bool anyUnhealthy = false;
    bool anyDegraded = false;
    bool anyOverheating = false;

    // Temperature thresholds
    constexpr int TEMP_WARNING = 80;   // Start warning
    constexpr int TEMP_CRITICAL = 90;  // Critical temperature

    for (size_t i = 0; i < descriptors.size(); i++) {
        const auto& dev = descriptors[i];

        json device;
        device["index"] = dev.index;
        device["name"] = dev.name;

        std::string status = "healthy";

        if (m_farm.isMinerFailed(static_cast<unsigned>(i))) {
            status = "failed";
            anyUnhealthy = true;
        }

        // Check GPU temperature
        GpuStats gpuStats;
        if (dev.type == MinerType::CUDA) {
            gpuStats = GpuMonitor::instance().getNvidiaStats(dev.cudaDeviceIndex);
        } else if (dev.type == MinerType::OpenCL) {
            gpuStats = GpuMonitor::instance().getAmdStats(dev.clDeviceIndex);
        }

        if (gpuStats.valid && gpuStats.temperature >= 0) {
            device["temperature"] = gpuStats.temperature;

            if (gpuStats.temperature >= TEMP_CRITICAL) {
                if (status == "healthy") status = "critical";
                anyUnhealthy = true;
                anyOverheating = true;
                device["temperature_status"] = "critical";
            } else if (gpuStats.temperature >= TEMP_WARNING) {
                if (status == "healthy") status = "warning";
                anyDegraded = true;
                device["temperature_status"] = "warning";
            } else {
                device["temperature_status"] = "normal";
            }
        }

        device["status"] = status;
        devices.push_back(device);
    }

    if (anyUnhealthy) {
        health["overall"] = "unhealthy";
    } else if (anyDegraded) {
        health["overall"] = "degraded";
    }

    if (anyOverheating) {
        health["overheating"] = true;
        health["warning"] = "One or more GPUs are overheating!";
    }

    health["devices"] = devices;
    health["active_miners"] = m_farm.activeMinerCount();
    health["total_miners"] = m_farm.minerCount();

    return health;
}

std::string ApiServer::createResponse(int status, const std::string& body) {
    std::string statusText;
    switch (status) {
        case 200: statusText = "OK"; break;
        case 404: statusText = "Not Found"; break;
        case 405: statusText = "Method Not Allowed"; break;
        default: statusText = "Error"; break;
    }

    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << statusText << "\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "\r\n";
    response << body;

    return response.str();
}

}  // namespace tos
