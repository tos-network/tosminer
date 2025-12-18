/**
 * TOS Miner - JSON-RPC API Server
 *
 * Simple HTTP server for monitoring miner status
 */

#pragma once

#include "core/Farm.h"
#include "stratum/StratumClient.h"
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <thread>
#include <memory>
#include <string>

namespace tos {

using json = nlohmann::json;

/**
 * Simple JSON-RPC API Server
 *
 * Provides HTTP endpoints for monitoring:
 * - GET /           - Basic status
 * - GET /stats      - Mining statistics
 * - GET /devices    - Device information
 * - GET /health     - Device health status
 */
class ApiServer {
public:
    /**
     * Constructor
     *
     * @param port Port to listen on
     * @param farm Reference to mining farm
     * @param stratum Reference to stratum client
     */
    ApiServer(unsigned port, Farm& farm, StratumClient& stratum);

    /**
     * Destructor
     */
    ~ApiServer();

    // Non-copyable
    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    /**
     * Start the API server
     *
     * @return true if started successfully
     */
    bool start();

    /**
     * Stop the API server
     */
    void stop();

    /**
     * Check if server is running
     */
    bool isRunning() const { return m_running; }

    /**
     * Get port number
     */
    unsigned getPort() const { return m_port; }

private:
    /**
     * Accept loop
     */
    void acceptLoop();

    /**
     * Handle a client connection
     */
    void handleClient(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    /**
     * Parse HTTP request and return response
     */
    std::string handleRequest(const std::string& request);

    /**
     * Get basic status JSON
     */
    json getStatus();

    /**
     * Get mining statistics JSON
     */
    json getStats();

    /**
     * Get device information JSON
     */
    json getDevices();

    /**
     * Get device health JSON
     */
    json getHealth();

    /**
     * Create HTTP response
     */
    std::string createResponse(int status, const std::string& body);

private:
    unsigned m_port;
    Farm& m_farm;
    StratumClient& m_stratum;

    boost::asio::io_context m_io;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> m_acceptor;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

}  // namespace tos
