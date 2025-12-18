/**
 * TOS Miner - Stratum Client
 *
 * Pool connection using Stratum protocol with proper JSON-RPC handling
 * Supports both stratum+tcp:// and stratum+ssl:// connections
 */

#pragma once

#include "core/Types.h"
#include "core/WorkPackage.h"
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#ifdef WITH_TLS
#include <boost/asio/ssl.hpp>
#endif
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <map>
#include <chrono>

namespace tos {

using json = nlohmann::json;

/**
 * Stratum protocol variants
 */
enum class StratumProtocol {
    Stratum,          // Standard stratum (TOS native)
    EthProxy,         // ETHPROXY - simplified proxy protocol
    EthereumStratum,  // ETHEREUMSTRATUM - Nicehash variant
    StratumV2         // Stratum V2 (future)
};

/**
 * Convert string to protocol enum
 */
inline StratumProtocol parseStratumProtocol(const std::string& str) {
    if (str == "ethproxy" || str == "ETHPROXY") return StratumProtocol::EthProxy;
    if (str == "ethereumstratum" || str == "ETHEREUMSTRATUM") return StratumProtocol::EthereumStratum;
    if (str == "stratumv2" || str == "stratum2") return StratumProtocol::StratumV2;
    return StratumProtocol::Stratum;  // default
}

/**
 * Connection state
 */
enum class StratumState {
    Disconnected,
    Connecting,
    Connected,
    Subscribed,
    Authorized
};

/**
 * Pool endpoint
 */
struct PoolEndpoint {
    std::string host;
    unsigned port;
    std::string user;
    std::string pass;
    bool useTls;  // Use SSL/TLS encryption

    PoolEndpoint() : port(0), useTls(false) {}
    PoolEndpoint(const std::string& h, unsigned p, const std::string& u, const std::string& pw, bool tls = false)
        : host(h), port(p), user(u), pass(pw), useTls(tls) {}
};

/**
 * Stratum client callbacks
 */
using WorkCallback = std::function<void(const WorkPackage&)>;
using ShareCallback = std::function<void(bool accepted, const std::string& reason)>;
using ConnectionCallback = std::function<void(bool connected)>;

/**
 * Pending request for tracking responses
 */
struct PendingRequest {
    std::string method;
    std::chrono::steady_clock::time_point timestamp;
};

/**
 * Stratum Client class
 *
 * Handles pool communication via Stratum protocol with:
 * - Proper JSON-RPC message handling
 * - Automatic reconnection
 * - Connection keepalive
 * - Pool failover support
 * - Difficulty adjustment
 */
class StratumClient {
public:
    /**
     * Constructor
     */
    StratumClient();

    /**
     * Destructor
     */
    ~StratumClient();

    /**
     * Connect to pool
     *
     * @param host Pool hostname
     * @param port Pool port
     * @param useTls Use SSL/TLS encryption
     * @return true if connection initiated
     */
    bool connect(const std::string& host, unsigned port, bool useTls = false);

    /**
     * Connect to pool from URL
     *
     * @param url Pool URL (stratum+tcp://host:port or stratum+ssl://host:port)
     * @return true if connection initiated
     */
    bool connectUrl(const std::string& url);

    /**
     * Add failover pool
     *
     * @param host Pool hostname
     * @param port Pool port
     * @param useTls Use SSL/TLS encryption
     */
    void addFailover(const std::string& host, unsigned port, bool useTls = false);

    /**
     * Check if TLS is supported
     */
    static bool isTlsSupported();

    /**
     * Set TLS certificate verification mode
     *
     * @param strict If true, verify server certificates (reject invalid/self-signed)
     *               If false, accept any certificate (insecure, but common for pools)
     */
    void setTlsVerification(bool strict) { m_tlsStrictVerify = strict; }

    /**
     * Set stratum protocol variant
     */
    void setProtocol(StratumProtocol protocol) { m_protocol = protocol; }

    /**
     * Get current protocol
     */
    StratumProtocol getProtocol() const { return m_protocol; }

    /**
     * Check if strict TLS verification is enabled
     */
    bool isTlsStrict() const { return m_tlsStrictVerify; }

    /**
     * Disconnect from pool
     */
    void disconnect();

    /**
     * Graceful disconnect - wait for pending share submissions
     *
     * @param timeoutMs Maximum time to wait in milliseconds
     * @return Number of pending requests that completed
     */
    unsigned gracefulDisconnect(unsigned timeoutMs = 5000);

    /**
     * Get number of pending requests
     */
    size_t pendingRequestCount() const;

    /**
     * Check if connected
     */
    bool isConnected() const { return m_state >= StratumState::Connected; }

    /**
     * Check if authorized
     */
    bool isAuthorized() const { return m_state == StratumState::Authorized; }

    /**
     * Set pool credentials
     *
     * @param user Username (usually wallet.worker)
     * @param pass Password
     */
    void setCredentials(const std::string& user, const std::string& pass);

    /**
     * Submit solution to pool
     *
     * @param solution The found solution
     * @param jobId Job ID from work package
     */
    void submitSolution(const Solution& solution, const std::string& jobId);

    /**
     * Set work callback
     */
    void setWorkCallback(WorkCallback callback);

    /**
     * Set share result callback
     */
    void setShareCallback(ShareCallback callback);

    /**
     * Set connection state callback
     */
    void setConnectionCallback(ConnectionCallback callback);

    /**
     * Get current connection state
     */
    StratumState getState() const { return m_state; }

    /**
     * Get last error message
     */
    const std::string& getLastError() const { return m_lastError; }

    /**
     * Get current difficulty
     */
    double getDifficulty() const { return m_difficulty; }

    /**
     * Get accepted share count
     */
    uint64_t getAcceptedShares() const { return m_acceptedShares; }

    /**
     * Get rejected share count
     */
    uint64_t getRejectedShares() const { return m_rejectedShares; }

    /**
     * Get pool version (if provided by pool)
     */
    const std::string& getPoolVersion() const { return m_poolVersion; }

    /**
     * Enable/disable auto-reconnect
     */
    void setAutoReconnect(bool enable) { m_autoReconnect = enable; }

    /**
     * Set reconnect delay in seconds
     */
    void setReconnectDelay(unsigned seconds) { m_reconnectDelay = seconds; }

private:
    /**
     * IO thread main function
     */
    void ioThread();

    /**
     * Attempt connection to current pool
     */
    void doConnect();

    /**
     * Handle connect result
     */
    void handleConnect(const boost::system::error_code& ec);

#ifdef WITH_TLS
    /**
     * Handle SSL handshake completion
     */
    void handleHandshake(const boost::system::error_code& ec);
#endif

    /**
     * Start reading from socket
     */
    void startRead();

    /**
     * Handle read completion
     */
    void handleRead(const boost::system::error_code& ec, size_t bytes);

    /**
     * Process received line (JSON-RPC message)
     */
    void processLine(const std::string& line);

    /**
     * Handle JSON-RPC response
     */
    void handleResponse(const json& response);

    /**
     * Handle JSON-RPC notification
     */
    void handleNotification(const json& notification);

    /**
     * Send JSON-RPC request
     *
     * @return Request ID
     */
    uint64_t sendRequest(const std::string& method, const json& params);

    /**
     * Subscribe to mining notifications
     */
    void subscribe();

    /**
     * Authorize with pool
     */
    void authorize();

    /**
     * Handle mining.notify
     */
    void handleMiningNotify(const json& params);

    /**
     * Handle mining.set_difficulty / mining.set_target
     */
    void handleSetDifficulty(const json& params);

    /**
     * Handle reconnect after error
     */
    void handleReconnect();

    /**
     * Schedule keepalive
     */
    void scheduleKeepalive();

    /**
     * Send keepalive
     */
    void sendKeepalive(const boost::system::error_code& ec);

    /**
     * Convert difficulty to target (full 256-bit calculation)
     */
    void difficultyToTarget(double difficulty, Hash256& target);

    /**
     * Schedule request timeout cleanup
     */
    void scheduleRequestTimeout();

    /**
     * Clean up timed out requests
     */
    void cleanupTimedOutRequests(const boost::system::error_code& ec);

    /**
     * Schedule work timeout check
     */
    void scheduleWorkTimeout();

    /**
     * Handle work timeout (no new work received)
     */
    void handleWorkTimeout(const boost::system::error_code& ec);

    /**
     * Convert hex string to bytes
     */
    bool hexToBytes(const std::string& hex, uint8_t* bytes, size_t len);

    /**
     * Convert bytes to hex string
     */
    std::string bytesToHex(const uint8_t* bytes, size_t len);

    /**
     * Notify connection state change
     */
    void notifyConnectionChange(bool connected);

private:
    // ASIO objects
    boost::asio::io_context m_io;
    std::unique_ptr<boost::asio::ip::tcp::socket> m_socket;
#ifdef WITH_TLS
    std::unique_ptr<boost::asio::ssl::context> m_sslContext;
    std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> m_sslSocket;
    bool m_useTls{false};  // Whether current connection uses TLS
#endif
    boost::asio::streambuf m_readBuffer;
    std::unique_ptr<boost::asio::steady_timer> m_keepaliveTimer;
    std::unique_ptr<boost::asio::steady_timer> m_reconnectTimer;
    std::unique_ptr<boost::asio::steady_timer> m_requestTimeoutTimer;
    std::unique_ptr<boost::asio::steady_timer> m_workTimeoutTimer;

    // Socket write mutex (for thread-safe sends from multiple miners)
    std::mutex m_sendMutex;

    // IO thread
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    // Connection state
    std::atomic<StratumState> m_state{StratumState::Disconnected};

    // Pool endpoints (primary + failovers)
    std::vector<PoolEndpoint> m_pools;
    unsigned m_currentPoolIndex{0};

    // Current credentials (for display)
    std::string m_user;
    std::string m_pass;

    // Request tracking
    std::atomic<uint64_t> m_requestId{1};
    std::map<uint64_t, PendingRequest> m_pendingRequests;
    mutable std::mutex m_requestMutex;

    // Callbacks
    WorkCallback m_workCallback;
    ShareCallback m_shareCallback;
    ConnectionCallback m_connectionCallback;
    std::mutex m_callbackMutex;

    // Current work
    WorkPackage m_currentWork;
    std::mutex m_workMutex;

    // Difficulty and target
    std::atomic<double> m_difficulty{1.0};
    Hash256 m_target;
    std::mutex m_targetMutex;
    bool m_hasPoolTarget{false};  // True when pool sent explicit target

    // Statistics
    std::atomic<uint64_t> m_acceptedShares{0};
    std::atomic<uint64_t> m_rejectedShares{0};

    // Error tracking
    std::string m_lastError;

    // Subscription info
    std::string m_sessionId;
    std::string m_extraNonce1;
    unsigned m_extraNonce2Size{4};
    std::string m_poolVersion;  // Pool software version (if provided)

    // Reconnection settings
    std::atomic<bool> m_autoReconnect{true};
    unsigned m_reconnectDelay{5};  // seconds
    unsigned m_reconnectAttempts{0};
    static constexpr unsigned MAX_RECONNECT_ATTEMPTS = 10;

    // TLS settings
    bool m_tlsStrictVerify{false};  // Default: accept any cert (pools often use self-signed)

    // Protocol variant
    StratumProtocol m_protocol{StratumProtocol::Stratum};

    // Keepalive settings
    static constexpr unsigned KEEPALIVE_INTERVAL = 30;  // seconds

    // Request timeout settings
    static constexpr unsigned REQUEST_TIMEOUT = 30;  // seconds
    static constexpr unsigned REQUEST_CLEANUP_INTERVAL = 10;  // seconds

    // Work timeout settings
    static constexpr unsigned WORK_TIMEOUT = 60;  // seconds without new work triggers reconnect
    std::chrono::steady_clock::time_point m_lastWorkTime;
};

}  // namespace tos
