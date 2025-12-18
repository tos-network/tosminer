/**
 * TOS Miner - Stratum Client Implementation
 *
 * Complete Stratum protocol implementation with JSON-RPC
 */

#include "StratumClient.h"
#include "Version.h"
#include "util/Log.h"
#include <boost/asio.hpp>
#ifdef WITH_TLS
#include <boost/asio/ssl.hpp>
#endif
#include <sstream>
#include <regex>
#include <iomanip>
#include <cstring>
#include <cmath>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace tos {

StratumClient::StratumClient() {
    m_target.fill(0xFF);  // Default to max target (difficulty 1)
}

StratumClient::~StratumClient() {
    disconnect();
}

bool StratumClient::connect(const std::string& host, unsigned port, bool useTls) {
    if (m_running) {
        disconnect();
    }

#ifdef WITH_TLS
    m_useTls = useTls;
#else
    if (useTls) {
        m_lastError = "TLS not supported (built without WITH_TLS)";
        Log::error(m_lastError);
        return false;
    }
#endif

    // Set up primary pool
    if (m_pools.empty()) {
        m_pools.emplace_back(host, port, m_user, m_pass, useTls);
    } else {
        m_pools[0] = PoolEndpoint(host, port, m_user, m_pass, useTls);
    }
    m_currentPoolIndex = 0;

    m_state = StratumState::Connecting;
    m_running = true;
    m_reconnectAttempts = 0;

    m_thread = std::thread(&StratumClient::ioThread, this);
    return true;
}

bool StratumClient::isTlsSupported() {
#ifdef WITH_TLS
    return true;
#else
    return false;
#endif
}

bool StratumClient::connectUrl(const std::string& url) {
    // Parse URL: stratum+tcp://host:port or stratum+ssl://host:port
    std::regex urlRegex(R"(stratum\+(tcp|ssl)://([^:]+):(\d+))");
    std::smatch match;

    if (!std::regex_match(url, match, urlRegex)) {
        m_lastError = "Invalid URL format. Expected: stratum+tcp://host:port or stratum+ssl://host:port";
        return false;
    }

    std::string protocol = match[1].str();
    std::string host = match[2].str();
    unsigned port = std::stoul(match[3].str());
    bool useTls = (protocol == "ssl");

    if (useTls) {
        Log::info("Using TLS/SSL connection");
    }

    return connect(host, port, useTls);
}

void StratumClient::addFailover(const std::string& host, unsigned port, bool useTls) {
    m_pools.emplace_back(host, port, m_user, m_pass, useTls);
}

void StratumClient::disconnect() {
    m_running = false;
    m_state = StratumState::Disconnected;

    // Cancel all timers
    if (m_keepaliveTimer) {
        m_keepaliveTimer->cancel();
    }
    if (m_reconnectTimer) {
        m_reconnectTimer->cancel();
    }
    if (m_requestTimeoutTimer) {
        m_requestTimeoutTimer->cancel();
    }
    if (m_workTimeoutTimer) {
        m_workTimeoutTimer->cancel();
    }

#ifdef WITH_TLS
    if (m_sslSocket) {
        boost::system::error_code ec;
        // Perform SSL shutdown (non-blocking, ignore errors on disconnect)
        m_sslSocket->lowest_layer().cancel(ec);
        m_sslSocket->lowest_layer().shutdown(tcp::socket::shutdown_both, ec);
        m_sslSocket->lowest_layer().close(ec);
        m_sslSocket.reset();
    }
#endif

    if (m_socket) {
        boost::system::error_code ec;
        m_socket->shutdown(tcp::socket::shutdown_both, ec);
        m_socket->close(ec);
    }

    m_io.stop();

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_io.restart();

#ifdef WITH_TLS
    m_sslContext.reset();
#endif

    notifyConnectionChange(false);
}

unsigned StratumClient::gracefulDisconnect(unsigned timeoutMs) {
    if (m_state == StratumState::Disconnected) {
        return 0;
    }

    // Wait for pending share submissions to complete
    size_t initialPending = pendingRequestCount();
    if (initialPending > 0) {
        Log::info("Waiting for " + std::to_string(initialPending) + " pending share(s) to complete...");
    }

    unsigned waited = 0;
    const unsigned checkInterval = 100;  // Check every 100ms

    while (waited < timeoutMs) {
        size_t pending = pendingRequestCount();
        if (pending == 0) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(checkInterval));
        waited += checkInterval;
    }

    size_t remaining = pendingRequestCount();
    unsigned completed = static_cast<unsigned>(initialPending - remaining);

    if (remaining > 0) {
        Log::warning("Timeout waiting for " + std::to_string(remaining) +
                    " pending request(s), disconnecting anyway");
    } else if (initialPending > 0) {
        Log::info("All pending requests completed");
    }

    disconnect();
    return completed;
}

size_t StratumClient::pendingRequestCount() const {
    Guard lock(m_requestMutex);
    return m_pendingRequests.size();
}

void StratumClient::setCredentials(const std::string& user, const std::string& pass) {
    m_user = user;
    m_pass = pass;

    // Update credentials in all pool endpoints
    for (auto& pool : m_pools) {
        pool.user = user;
        pool.pass = pass;
    }
}

void StratumClient::submitSolution(const Solution& solution, const std::string& jobId) {
    if (m_state != StratumState::Authorized) {
        Log::warning("Cannot submit: not authorized");
        return;
    }

    // Get current work for extranonce calculation
    WorkPackage work;
    {
        Guard lock(m_workMutex);
        work = m_currentWork;
    }

    // Build submission params
    // Standard Stratum format: ["worker", "job_id", "extranonce2", "ntime", "nonce"]
    // TOS simplified format: ["worker", "job_id", "extranonce2", "nonce_hex"]
    json params = json::array();
    params.push_back(m_user);
    params.push_back(jobId);

    // Extranonce2 calculation:
    // Each device has a unique extranonce2 based on its index in the nonce space.
    // The found nonce = startNonce + deviceOffset + localOffset
    // extranonce2 value = deviceOffset + localOffset = nonce - startNonce
    uint64_t en2Value = solution.nonce - work.startNonce;

    // Format as hex string with proper size (extraNonce2Size bytes, little-endian)
    std::ostringstream en2Hex;
    for (unsigned i = 0; i < m_extraNonce2Size; i++) {
        en2Hex << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>((en2Value >> (i * 8)) & 0xFF);
    }
    params.push_back(en2Hex.str());

    // Nonce as hex (big-endian, 16 hex chars for 64-bit nonce)
    // Must match TOS protocol: nonce at bytes 40-47 in big-endian
    std::ostringstream nonceHex;
    for (int i = 0; i < 8; i++) {
        nonceHex << std::hex << std::setw(2) << std::setfill('0')
                 << static_cast<int>((solution.nonce >> ((7 - i) * 8)) & 0xFF);
    }
    params.push_back(nonceHex.str());

    uint64_t reqId = sendRequest("mining.submit", params);
    Log::info("Submitting share (job=" + jobId + ", dev=" + std::to_string(solution.deviceIndex) +
              ", en2=" + en2Hex.str() + ", nonce=" + nonceHex.str() + ")");

    // Track this as a submit request for response handling
    {
        Guard lock(m_requestMutex);
        m_pendingRequests[reqId] = {"mining.submit", std::chrono::steady_clock::now()};
    }
}

void StratumClient::setWorkCallback(WorkCallback callback) {
    Guard lock(m_callbackMutex);
    m_workCallback = std::move(callback);
}

void StratumClient::setShareCallback(ShareCallback callback) {
    Guard lock(m_callbackMutex);
    m_shareCallback = std::move(callback);
}

void StratumClient::setConnectionCallback(ConnectionCallback callback) {
    Guard lock(m_callbackMutex);
    m_connectionCallback = std::move(callback);
}

void StratumClient::ioThread() {
    try {
        // Create timers
        m_keepaliveTimer = std::make_unique<asio::steady_timer>(m_io);
        m_reconnectTimer = std::make_unique<asio::steady_timer>(m_io);
        m_requestTimeoutTimer = std::make_unique<asio::steady_timer>(m_io);
        m_workTimeoutTimer = std::make_unique<asio::steady_timer>(m_io);

        // Initialize last work time
        m_lastWorkTime = std::chrono::steady_clock::now();

        // Start connection
        doConnect();

        // Schedule request timeout cleanup
        scheduleRequestTimeout();

        // Run IO loop
        m_io.run();

    } catch (const std::exception& e) {
        m_lastError = e.what();
        Log::error("Stratum IO error: " + m_lastError);
        m_state = StratumState::Disconnected;
    }
}

void StratumClient::doConnect() {
    if (!m_running) return;

    if (m_pools.empty()) {
        m_lastError = "No pool configured";
        Log::error(m_lastError);
        return;
    }

    const auto& pool = m_pools[m_currentPoolIndex];

#ifdef WITH_TLS
    m_useTls = pool.useTls;
    std::string protocol = m_useTls ? "TLS" : "TCP";
    Log::info("Connecting to " + pool.host + ":" + std::to_string(pool.port) + " (" + protocol + ")...");
#else
    Log::info("Connecting to " + pool.host + ":" + std::to_string(pool.port) + "...");
#endif

    try {
        // Resolve hostname
        tcp::resolver resolver(m_io);
        auto endpoints = resolver.resolve(pool.host, std::to_string(pool.port));

#ifdef WITH_TLS
        if (m_useTls) {
            // Initialize SSL context
            m_sslContext = std::make_unique<asio::ssl::context>(asio::ssl::context::tlsv12_client);

            // Use default certificate verification paths
            m_sslContext->set_default_verify_paths();

            // Set verification mode based on strict setting
            if (m_tlsStrictVerify) {
                // Strict mode: verify peer certificate properly
                m_sslContext->set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);

                m_sslContext->set_verify_callback([](bool preverified, asio::ssl::verify_context& ctx) {
                    // Log certificate info
                    char subject[256];
                    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
                    if (cert) {
                        X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
                        Log::debug("Certificate: " + std::string(subject) +
                                   " (preverified=" + (preverified ? "yes" : "no") + ")");
                    }
                    // In strict mode, only accept if preverified by OpenSSL
                    if (!preverified) {
                        int err = X509_STORE_CTX_get_error(ctx.native_handle());
                        Log::warning("TLS certificate verification failed: " +
                                   std::string(X509_verify_cert_error_string(err)));
                    }
                    return preverified;  // Respect OpenSSL's verification result
                });

                Log::info("TLS strict verification enabled");
            } else {
                // Permissive mode: accept any certificate (pools often use self-signed)
                m_sslContext->set_verify_mode(asio::ssl::verify_peer);

                m_sslContext->set_verify_callback([](bool preverified, asio::ssl::verify_context& ctx) {
                    // Log certificate info for debugging
                    char subject[256];
                    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
                    if (cert) {
                        X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
                        Log::debug("Certificate: " + std::string(subject));
                    }
                    return true;  // Accept any certificate
                });

                Log::debug("TLS permissive mode (accepting any certificate)");
            }

            // Create SSL stream
            m_sslSocket = std::make_unique<asio::ssl::stream<tcp::socket>>(m_io, *m_sslContext);

            // Set SNI hostname (required by some servers)
            SSL_set_tlsext_host_name(m_sslSocket->native_handle(), pool.host.c_str());

            // Async connect
            asio::async_connect(m_sslSocket->lowest_layer(), endpoints,
                [this](const boost::system::error_code& ec, const tcp::endpoint&) {
                    handleConnect(ec);
                });
        } else
#endif
        {
            // Plain TCP connection
            m_socket = std::make_unique<tcp::socket>(m_io);

            asio::async_connect(*m_socket, endpoints,
                [this](const boost::system::error_code& ec, const tcp::endpoint&) {
                    handleConnect(ec);
                });
        }

    } catch (const std::exception& e) {
        m_lastError = e.what();
        Log::error("Connection setup failed: " + m_lastError);
        handleReconnect();
    }
}

void StratumClient::handleConnect(const boost::system::error_code& ec) {
    if (!m_running) return;

    if (ec) {
        m_lastError = ec.message();
        Log::error("Failed to connect to pool: " + m_lastError);
        handleReconnect();
        return;
    }

    const auto& pool = m_pools[m_currentPoolIndex];

#ifdef WITH_TLS
    if (m_useTls) {
        Log::info("TCP connected, starting TLS handshake...");

        // Perform SSL handshake
        m_sslSocket->async_handshake(asio::ssl::stream_base::client,
            [this](const boost::system::error_code& ec) {
                handleHandshake(ec);
            });
        return;
    }
#endif

    // Plain TCP - connection complete
    Log::info("Connected to " + pool.host + ":" + std::to_string(pool.port));
    m_state = StratumState::Connected;
    m_reconnectAttempts = 0;

    notifyConnectionChange(true);

    // Start reading
    startRead();

    // Subscribe to mining
    subscribe();

    // Schedule keepalive
    scheduleKeepalive();

    // Schedule work timeout monitoring
    scheduleWorkTimeout();
}

#ifdef WITH_TLS
void StratumClient::handleHandshake(const boost::system::error_code& ec) {
    if (!m_running) return;

    if (ec) {
        m_lastError = "TLS handshake failed: " + ec.message();
        Log::error(m_lastError);
        handleReconnect();
        return;
    }

    const auto& pool = m_pools[m_currentPoolIndex];
    Log::info("TLS connection established to " + pool.host + ":" + std::to_string(pool.port));
    m_state = StratumState::Connected;
    m_reconnectAttempts = 0;

    notifyConnectionChange(true);

    // Start reading
    startRead();

    // Subscribe to mining
    subscribe();

    // Schedule keepalive
    scheduleKeepalive();

    // Schedule work timeout monitoring
    scheduleWorkTimeout();
}
#endif

void StratumClient::startRead() {
    if (!m_running) return;

    // Note: m_readBuffer has max_size=MAX_LINE_LENGTH set at construction.
    // If the buffer fills without finding a newline, async_read_until returns
    // asio::error::not_found, which is handled in handleRead().

#ifdef WITH_TLS
    if (m_useTls) {
        if (!m_sslSocket) return;

        asio::async_read_until(*m_sslSocket, m_readBuffer, '\n',
            [this](const boost::system::error_code& ec, size_t bytes) {
                handleRead(ec, bytes);
            });
    } else
#endif
    {
        if (!m_socket) return;

        asio::async_read_until(*m_socket, m_readBuffer, '\n',
            [this](const boost::system::error_code& ec, size_t bytes) {
                handleRead(ec, bytes);
            });
    }
}

void StratumClient::handleRead(const boost::system::error_code& ec, size_t bytes) {
    if (!m_running) return;

    if (ec) {
        if (ec != asio::error::operation_aborted) {
            // Check if buffer overflow (no newline found within MAX_LINE_LENGTH)
            if (ec == asio::error::not_found) {
                m_lastError = "Buffer overflow: no newline within " +
                              std::to_string(MAX_LINE_LENGTH) + " bytes, disconnecting";
            } else {
                m_lastError = ec.message();
            }
            Log::error("Read error: " + m_lastError);
            // Clear buffer to avoid immediate not_found on reconnect.
            m_readBuffer.consume(m_readBuffer.size());
            m_state = StratumState::Disconnected;
            notifyConnectionChange(false);
            handleReconnect();
        }
        return;
    }

    // Additional sanity check for line length (should not trigger with streambuf max_size)
    if (bytes > MAX_LINE_LENGTH) {
        m_lastError = "Line too long (" + std::to_string(bytes) + " bytes), disconnecting";
        Log::error(m_lastError);
        m_state = StratumState::Disconnected;
        notifyConnectionChange(false);
        handleReconnect();
        return;
    }

    // Extract line from buffer
    std::istream is(&m_readBuffer);
    std::string line;
    std::getline(is, line);

    // Remove trailing CR if present
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    // Process the message
    if (!line.empty()) {
        processLine(line);
    }

    // Continue reading
    if (m_running) {
#ifdef WITH_TLS
        if (m_useTls ? (m_sslSocket != nullptr) : (m_socket != nullptr)) {
            startRead();
        }
#else
        if (m_socket) {
            startRead();
        }
#endif
    }
}

void StratumClient::processLine(const std::string& line) {
    Log::debug("Recv: " + line);

    try {
        json msg = json::parse(line);

        // Check if it's a response (has "id" and no "method")
        if (msg.contains("id") && !msg["id"].is_null() && !msg.contains("method")) {
            handleResponse(msg);
        }
        // Otherwise it's a notification (has "method")
        else if (msg.contains("method")) {
            handleNotification(msg);
        }
        else {
            Log::warning("Unknown message format: " + line);
        }

    } catch (const json::exception& e) {
        Log::error("JSON parse error: " + std::string(e.what()));
    }
}

void StratumClient::handleResponse(const json& response) {
    uint64_t id = response["id"].get<uint64_t>();

    // Find the pending request
    std::string method;
    {
        Guard lock(m_requestMutex);
        auto it = m_pendingRequests.find(id);
        if (it != m_pendingRequests.end()) {
            method = it->second.method;
            m_pendingRequests.erase(it);
        }
    }

    // Check for error
    bool hasError = response.contains("error") && !response["error"].is_null();
    std::string errorMsg;
    if (hasError) {
        if (response["error"].is_array() && response["error"].size() > 1) {
            errorMsg = response["error"][1].get<std::string>();
        } else if (response["error"].is_string()) {
            errorMsg = response["error"].get<std::string>();
        } else if (response["error"].is_object() && response["error"].contains("message")) {
            errorMsg = response["error"]["message"].get<std::string>();
        } else {
            errorMsg = "Unknown error";
        }
    }

    // Handle based on method type
    if (method == "mining.subscribe") {
        if (hasError) {
            Log::error("Subscription failed: " + errorMsg);
            handleReconnect();
        } else {
            // Parse subscription result
            // Format 1: [[["mining.notify", "id"], ["mining.set_difficulty", "id"]], extranonce1, extranonce2_size]
            // Format 2: [["mining.notify", "id"], extranonce1, extranonce2_size]
            const auto& result = response["result"];
            if (result.is_array() && result.size() >= 2) {
                // Extract session ID from subscriptions array
                if (result[0].is_array() && !result[0].empty()) {
                    // Check if it's nested (array of arrays) or flat
                    if (result[0][0].is_array()) {
                        // Nested format: [[["mining.notify", "id"], ...]]
                        // Get session_id from first subscription pair
                        if (result[0][0].size() >= 2 && result[0][0][1].is_string()) {
                            m_sessionId = result[0][0][1].get<std::string>();
                        }
                    } else if (result[0][0].is_string() && result[0].size() >= 2) {
                        // Flat format: [["mining.notify", "id"], ...]
                        if (result[0][1].is_string()) {
                            m_sessionId = result[0][1].get<std::string>();
                        }
                    }
                }
                m_extraNonce1 = result[1].get<std::string>();
                if (result.size() >= 3) {
                    m_extraNonce2Size = result[2].get<unsigned>();

                    // Validate extranonce2_size: minimum 4 bytes to prevent nonce space exhaustion
                    // With <4 bytes, miners will rapidly collide on nonces
                    constexpr unsigned MIN_EXTRANONCE2_SIZE = 4;
                    constexpr unsigned MAX_EXTRANONCE2_SIZE = 8;  // Maximum we can handle in uint64_t
                    if (m_extraNonce2Size < MIN_EXTRANONCE2_SIZE) {
                        Log::warning("Pool extranonce2_size=" + std::to_string(m_extraNonce2Size) +
                                     " is too small, using minimum of " + std::to_string(MIN_EXTRANONCE2_SIZE));
                        m_extraNonce2Size = MIN_EXTRANONCE2_SIZE;
                    } else if (m_extraNonce2Size > MAX_EXTRANONCE2_SIZE) {
                        Log::warning("Pool extranonce2_size=" + std::to_string(m_extraNonce2Size) +
                                     " exceeds maximum, using " + std::to_string(MAX_EXTRANONCE2_SIZE));
                        m_extraNonce2Size = MAX_EXTRANONCE2_SIZE;
                    }
                }
            }

            Log::info("Subscribed (session=" + m_sessionId + ", extranonce1=" + m_extraNonce1 +
                      ", extranonce2_size=" + std::to_string(m_extraNonce2Size) + ")");
            m_state = StratumState::Subscribed;

            // Now authorize
            authorize();
        }
    }
    else if (method == "mining.authorize" || method == "eth_submitLogin") {
        if (hasError) {
            Log::error("Authorization failed: " + errorMsg);
            handleReconnect();
        } else {
            // eth_submitLogin returns true on success, mining.authorize returns bool result
            bool authorized = true;
            if (response.contains("result")) {
                if (response["result"].is_boolean()) {
                    authorized = response["result"].get<bool>();
                }
            }
            if (authorized) {
                Log::info("Authorized with pool as " + m_user);
                m_state = StratumState::Authorized;
            } else {
                Log::error("Authorization rejected");
                handleReconnect();
            }
        }
    }
    else if (method == "mining.submit") {
        Guard lock(m_callbackMutex);
        if (hasError) {
            Log::warning("Share rejected: " + errorMsg);
            m_rejectedShares++;
            if (m_shareCallback) {
                m_shareCallback(false, errorMsg);
            }
        } else {
            bool accepted = response["result"].get<bool>();
            if (accepted) {
                Log::info("Share accepted!");
                m_acceptedShares++;
                if (m_shareCallback) {
                    m_shareCallback(true, "");
                }
            } else {
                Log::warning("Share rejected");
                m_rejectedShares++;
                if (m_shareCallback) {
                    m_shareCallback(false, "rejected");
                }
            }
        }
    }
}

void StratumClient::handleNotification(const json& notification) {
    std::string method = notification["method"].get<std::string>();
    const auto& params = notification["params"];

    if (method == "mining.notify") {
        handleMiningNotify(params);
    }
    else if (method == "mining.set_difficulty" || method == "mining.set_target") {
        handleSetDifficulty(params);
    }
    else if (method == "client.show_message") {
        // Pool wants to show a message to the miner
        if (params.is_array() && !params.empty()) {
            Log::info("Pool message: " + params[0].get<std::string>());
        }
    }
    else if (method == "client.reconnect") {
        // Pool requests reconnection
        Log::info("Pool requested reconnect");
        handleReconnect();
    }
    else {
        Log::debug("Unknown notification: " + method);
    }
}

uint64_t StratumClient::sendRequest(const std::string& method, const json& params) {
#ifdef WITH_TLS
    if (m_useTls) {
        if (!m_sslSocket) return 0;
    } else
#endif
    {
        if (!m_socket) return 0;
    }

    uint64_t id = m_requestId++;

    json request = {
        {"id", id},
        {"method", method},
        {"params", params}
    };

    std::string msg = request.dump() + "\n";
    Log::debug("Send: " + msg);

    // Thread-safe socket write (multiple miners may call submitSolution concurrently)
    boost::system::error_code ec;
    {
        Guard lock(m_sendMutex);
#ifdef WITH_TLS
        if (m_useTls) {
            asio::write(*m_sslSocket, asio::buffer(msg), ec);
        } else
#endif
        {
            asio::write(*m_socket, asio::buffer(msg), ec);
        }
    }

    if (ec) {
        Log::error("Send error: " + ec.message());
        return 0;
    }

    return id;
}

void StratumClient::subscribe() {
    json params = json::array();

    switch (m_protocol) {
        case StratumProtocol::EthProxy:
            // ETHPROXY doesn't use subscribe, go straight to login
            m_state = StratumState::Subscribed;
            authorize();
            return;

        case StratumProtocol::EthereumStratum:
            // ETHEREUMSTRATUM uses extended subscribe
            params.push_back(MINER_VERSION);
            params.push_back("EthereumStratum/1.0.0");
            break;

        case StratumProtocol::StratumV2:
            // Stratum V2 will be handled separately
            Log::warning("Stratum V2 not yet fully implemented, falling back to V1");
            [[fallthrough]];

        case StratumProtocol::Stratum:
        default:
            params.push_back(MINER_VERSION);
            break;
    }

    uint64_t id = sendRequest("mining.subscribe", params);
    {
        Guard lock(m_requestMutex);
        m_pendingRequests[id] = {"mining.subscribe", std::chrono::steady_clock::now()};
    }
}

void StratumClient::authorize() {
    const auto& pool = m_pools[m_currentPoolIndex];
    json params = json::array();
    std::string user = pool.user.empty() ? m_user : pool.user;
    std::string pass = pool.pass.empty() ? m_pass : pool.pass;

    std::string method;
    switch (m_protocol) {
        case StratumProtocol::EthProxy:
            // ETHPROXY uses eth_submitLogin
            method = "eth_submitLogin";
            params.push_back(user);
            if (!pass.empty() && pass != "x") {
                params.push_back(pass);
            }
            break;

        case StratumProtocol::EthereumStratum:
        case StratumProtocol::StratumV2:
        case StratumProtocol::Stratum:
        default:
            method = "mining.authorize";
            params.push_back(user);
            params.push_back(pass);
            break;
    }

    uint64_t id = sendRequest(method, params);
    {
        Guard lock(m_requestMutex);
        m_pendingRequests[id] = {method, std::chrono::steady_clock::now()};
    }
}

void StratumClient::handleMiningNotify(const json& params) {
    // TOS Stratum mining.notify format:
    // params: [job_id, header_hash, target, height, clean_jobs]
    // Or full header format:
    // params: [job_id, prev_hash, coinbase1, coinbase2, merkle_branches[], version, nbits, ntime, clean_jobs]

    if (!params.is_array() || params.size() < 2) {
        Log::error("Invalid mining.notify params");
        return;
    }

    WorkPackage work;
    work.valid = false;

    try {
        work.jobId = params[0].get<std::string>();
        bool poolSentTarget = false;

        // Check which format we're receiving
        if (params.size() >= 5 && params[4].is_boolean()) {
            // Simplified TOS format: [job_id, header_hex, target_hex, height, clean_jobs]
            std::string headerHex = params[1].get<std::string>();
            std::string targetHex = params[2].get<std::string>();
            work.height = params[3].get<uint64_t>();
            bool cleanJobs = params[4].get<bool>();

            // Parse header (should be INPUT_SIZE * 2 hex chars = 224 chars)
            if (headerHex.length() >= INPUT_SIZE * 2) {
                if (!hexToBytes(headerHex, work.header.data(), INPUT_SIZE)) {
                    Log::error("Failed to parse header hex");
                    return;
                }
            } else {
                // Header might be shorter, pad with zeros
                work.header.fill(0);
                hexToBytes(headerHex, work.header.data(), headerHex.length() / 2);
            }

            // Parse target - ALWAYS use pool-sent target when available (full 256-bit)
            if (targetHex.length() >= HASH_SIZE * 2) {
                if (!hexToBytes(targetHex, work.target.data(), HASH_SIZE)) {
                    Log::error("Failed to parse target hex");
                    return;
                }
                poolSentTarget = true;
                Log::debug("Using pool-sent target (256-bit)");
            } else if (!targetHex.empty()) {
                // Partial target hex - pad with zeros on the right (most significant bytes first)
                work.target.fill(0);
                hexToBytes(targetHex, work.target.data(), targetHex.length() / 2);
                poolSentTarget = true;
                Log::debug("Using pool-sent partial target");
            } else {
                // No target in notification - use difficulty-based target
                Guard lock(m_targetMutex);
                work.target = m_target;
            }

            if (cleanJobs) {
                Log::info("New job (clean): " + work.jobId);
            }

        } else {
            // Standard Stratum format - construct header from parts
            // For now, use a simplified approach
            std::string prevHash = params[1].get<std::string>();

            work.header.fill(0);
            hexToBytes(prevHash, work.header.data(), std::min(prevHash.length() / 2, (size_t)32));

            // Use current target from difficulty
            Guard lock(m_targetMutex);
            work.target = m_target;
            work.height = 0;
        }

        // Update pool target flag
        {
            Guard lock(m_targetMutex);
            m_hasPoolTarget = poolSentTarget;
            if (poolSentTarget) {
                m_target = work.target;  // Store for reference
            }
        }

        // Set extranonce info for proper nonce allocation
        work.extraNonce1 = m_extraNonce1;
        work.extraNonce2Size = m_extraNonce2Size;

        // Set start nonce (extraNonce1 becomes base)
        work.startNonce = 0;
        if (!m_extraNonce1.empty()) {
            // Use extraNonce1 as part of starting nonce (little-endian)
            uint64_t en1 = 0;
            hexToBytes(m_extraNonce1, reinterpret_cast<uint8_t*>(&en1),
                      std::min(m_extraNonce1.length() / 2, sizeof(uint64_t)));
            work.startNonce = en1;
        }

        // Total devices will be set by Farm when distributing work
        // Default to 1 for now
        work.totalDevices = 1;

        // Set receive timestamp for stale work detection
        work.receivedTime = std::chrono::steady_clock::now();
        work.valid = true;

        // Reset work timeout - we received new work
        m_lastWorkTime = work.receivedTime;
        scheduleWorkTimeout();

        // Check if previous work was stale (for logging purposes)
        {
            Guard lock(m_workMutex);
            if (m_currentWork.valid && m_currentWork.jobId != work.jobId) {
                unsigned oldWorkAge = m_currentWork.getAgeSeconds();
                if (oldWorkAge > 30) {
                    Log::warning("Previous job " + m_currentWork.jobId +
                                " was " + std::to_string(oldWorkAge) + "s old");
                }
            }
            m_currentWork = work;
        }

        {
            Guard lock(m_callbackMutex);
            if (m_workCallback) {
                m_workCallback(work);
            }
        }

        Log::info("New job: " + work.jobId + " (height=" + std::to_string(work.height) + ")");

    } catch (const std::exception& e) {
        Log::error("Error parsing work notification: " + std::string(e.what()));
    }
}

void StratumClient::handleSetDifficulty(const json& params) {
    if (!params.is_array() || params.empty()) {
        Log::error("Invalid set_difficulty params");
        return;
    }

    double difficulty = params[0].get<double>();
    m_difficulty = difficulty;

    // Calculate difficulty-derived target
    Hash256 diffTarget;
    difficultyToTarget(difficulty, diffTarget);

    // Only update stored target if pool hasn't sent explicit target
    // Pool-sent targets take precedence over difficulty-derived targets
    {
        Guard lock(m_targetMutex);
        if (!m_hasPoolTarget) {
            m_target = diffTarget;
            Log::info("Difficulty set to " + std::to_string(difficulty) + " (using derived target)");
        } else {
            Log::info("Difficulty set to " + std::to_string(difficulty) + " (keeping pool target)");
        }
    }

    // Update current work's target only if pool hasn't sent explicit target
    {
        Guard lock(m_workMutex);
        if (m_currentWork.valid && !m_hasPoolTarget) {
            Guard targetLock(m_targetMutex);
            m_currentWork.target = m_target;
        }
    }
}

void StratumClient::handleReconnect() {
    if (!m_running || !m_autoReconnect) return;

    m_state = StratumState::Disconnected;

    // Close current socket(s)
#ifdef WITH_TLS
    if (m_sslSocket) {
        boost::system::error_code ec;
        m_sslSocket->lowest_layer().close(ec);
        m_sslSocket.reset();
    }
    m_sslContext.reset();
#endif

    if (m_socket) {
        boost::system::error_code ec;
        m_socket->close(ec);
    }

    // Clear pending requests
    {
        Guard lock(m_requestMutex);
        m_pendingRequests.clear();
    }

    m_reconnectAttempts++;

    // Check if we should try failover pool
    if (m_reconnectAttempts >= MAX_RECONNECT_ATTEMPTS / 2 && m_pools.size() > 1) {
        m_currentPoolIndex = (m_currentPoolIndex + 1) % m_pools.size();
        Log::info("Switching to failover pool " + std::to_string(m_currentPoolIndex + 1) +
                  "/" + std::to_string(m_pools.size()));
        m_reconnectAttempts = 0;
    }

    if (m_reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
        Log::error("Max reconnect attempts reached");
        m_running = false;
        return;
    }

    // Exponential backoff with jitter
    unsigned delay = m_reconnectDelay * (1 << std::min(m_reconnectAttempts, 5u));
    Log::info("Reconnecting in " + std::to_string(delay) + " seconds...");

    m_reconnectTimer->expires_after(std::chrono::seconds(delay));
    m_reconnectTimer->async_wait([this](const boost::system::error_code& ec) {
        if (!ec && m_running) {
            m_state = StratumState::Connecting;
            doConnect();
        }
    });
}

void StratumClient::scheduleKeepalive() {
    if (!m_running || !m_keepaliveTimer) return;

    m_keepaliveTimer->expires_after(std::chrono::seconds(KEEPALIVE_INTERVAL));
    m_keepaliveTimer->async_wait([this](const boost::system::error_code& ec) {
        sendKeepalive(ec);
    });
}

void StratumClient::sendKeepalive(const boost::system::error_code& ec) {
    if (ec || !m_running) return;

    // Send a simple request to keep connection alive
    // Some pools support mining.ping, others just ignore unknown methods
    if (m_state == StratumState::Authorized) {
        json params = json::array();
        sendRequest("mining.ping", params);
    }

    // Schedule next keepalive
    scheduleKeepalive();
}

void StratumClient::difficultyToTarget(double difficulty, Hash256& target) {
    // Pool difficulty (pdiff) formula:
    // base_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000
    // target = base_target / difficulty
    //
    // In big-endian byte order:
    // - Bytes 0-3: 0x00 (always zero)
    // - Bytes 4-5: 0xFF, 0xFF (0xFFFF)
    // - Bytes 6-31: 0x00 (26 zero bytes)
    //
    // This equals 0xFFFF * 2^208
    //
    // Known pdiff test vectors:
    // - difficulty 1:     0x00000000FFFF0000...00
    // - difficulty 1.5:   0x00000000AAAA0000...00
    // - difficulty 2:     0x000000007FFF8000...00
    // - difficulty 256:   0x0000000000FFFF00...00

    target.fill(0);

    if (difficulty <= 0) {
        target.fill(0xFF);
        return;
    }

    if (difficulty < 1.0) {
        // Difficulty < 1 means target > base
        // For safety, just use base target
        target[4] = 0xFF;
        target[5] = 0xFF;
        return;
    }

    // Clamp difficulty to prevent overflow/precision loss in fixed-point arithmetic
    // Max safe value: 2^52 (double mantissa precision) / 2^32 = 2^20 ≈ 1M
    // We use 1e15 as a practical upper bound which is well within double precision
    // and produces a near-zero target (essentially impossible to find)
    constexpr double MAX_SAFE_DIFFICULTY = 1e15;
    if (difficulty > MAX_SAFE_DIFFICULTY) {
        Log::warning("Difficulty " + std::to_string(difficulty) +
                     " exceeds safe limit, clamping to " + std::to_string(MAX_SAFE_DIFFICULTY));
        difficulty = MAX_SAFE_DIFFICULTY;
    }

#if defined(__SIZEOF_INT128__)
    // Use fixed-point arithmetic to handle fractional difficulties correctly
    // Scale difficulty by 2^32 to preserve fractional precision
    //
    // target = base / difficulty
    //        = (base * 2^32) / (difficulty * 2^32)
    //
    // The scaled dividend is base * 2^32 = 0xFFFF << 240 (a 36-byte number)
    // The scaled divisor is difficulty * 2^32 (fits in ~96 bits for reasonable difficulties)
    //
    // Due to the 2^32 scaling, quotient bytes are shifted by 4 positions,
    // so we process 36 dividend bytes but offset the output by 4.

    __uint128_t diffScaled = static_cast<__uint128_t>(difficulty * 4294967296.0);  // 2^32
    if (diffScaled == 0) diffScaled = 1;

    __uint128_t remainder = 0;

    for (int i = 0; i < 36; i++) {
        // Dividend bytes: 00 at 0-3, FF at 4-5, 00 at 6-35
        uint8_t dividendByte = (i == 4 || i == 5) ? 0xFF : 0;

        remainder = (remainder << 8) | dividendByte;

        __uint128_t q = remainder / diffScaled;

        // Output position is shifted by 4 due to 2^32 scaling
        int outputPos = i - 4;
        if (outputPos >= 0 && outputPos < 32) {
            target[outputPos] = (q > 255) ? 255 : static_cast<uint8_t>(q);
        }

        remainder = remainder % diffScaled;
    }
#else
    // Fallback for systems without 128-bit integers
    // Use double precision arithmetic (handles fractional difficulties naturally)
    //
    // quotient = 0xFFFF / difficulty
    // This represents the value that needs to be placed starting at byte 4

    double quotient = 65535.0 / difficulty;

    // Extract bytes using positional scaling
    // byte[i] = floor(quotient * 2^(8*i - 40)) % 256
    for (int i = 4; i < 32; i++) {
        int bitShift = 8 * i - 40;
        double scaled;
        if (bitShift >= 0) {
            scaled = quotient * std::pow(2.0, bitShift);
        } else {
            scaled = quotient / std::pow(2.0, -bitShift);
        }

        double byteVal = std::fmod(std::floor(scaled), 256.0);
        if (byteVal < 0) byteVal = 0;
        if (byteVal > 255) byteVal = 255;
        target[i] = static_cast<uint8_t>(byteVal);
    }
#endif

    // Verify we have a non-zero target
    bool allZero = true;
    for (int i = 0; i < 32; i++) {
        if (target[i] != 0) {
            allZero = false;
            break;
        }
    }
    if (allZero) {
        target[31] = 1;  // Minimum target
    }
}

void StratumClient::scheduleRequestTimeout() {
    if (!m_running || !m_requestTimeoutTimer) return;

    m_requestTimeoutTimer->expires_after(std::chrono::seconds(REQUEST_CLEANUP_INTERVAL));
    m_requestTimeoutTimer->async_wait([this](const boost::system::error_code& ec) {
        cleanupTimedOutRequests(ec);
    });
}

void StratumClient::cleanupTimedOutRequests(const boost::system::error_code& ec) {
    if (ec || !m_running) return;

    auto now = std::chrono::steady_clock::now();
    std::vector<uint64_t> timedOut;

    {
        Guard lock(m_requestMutex);
        for (const auto& [id, req] : m_pendingRequests) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - req.timestamp).count();
            if (age >= REQUEST_TIMEOUT) {
                timedOut.push_back(id);
                Log::warning("Request " + std::to_string(id) + " (" + req.method + ") timed out after " +
                           std::to_string(age) + "s");
            }
        }

        // Remove timed out requests
        for (uint64_t id : timedOut) {
            auto it = m_pendingRequests.find(id);
            if (it != m_pendingRequests.end()) {
                // If it was a submit, count as rejected
                if (it->second.method == "mining.submit") {
                    m_rejectedShares++;
                    Guard callbackLock(m_callbackMutex);
                    if (m_shareCallback) {
                        m_shareCallback(false, "timeout");
                    }
                }
                m_pendingRequests.erase(it);
            }
        }
    }

    // If too many timeouts, connection might be dead
    if (timedOut.size() >= 3) {
        Log::error("Multiple request timeouts - connection may be stale");
        handleReconnect();
        return;
    }

    // Schedule next cleanup
    scheduleRequestTimeout();
}

bool StratumClient::hexToBytes(const std::string& hex, uint8_t* bytes, size_t len) {
    if (hex.length() < len * 2) {
        len = hex.length() / 2;
    }

    for (size_t i = 0; i < len; i++) {
        unsigned int byte;
        if (sscanf(hex.c_str() + i * 2, "%02x", &byte) != 1) {
            return false;
        }
        bytes[i] = static_cast<uint8_t>(byte);
    }

    return true;
}

std::string StratumClient::bytesToHex(const uint8_t* bytes, size_t len) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; i++) {
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return ss.str();
}

void StratumClient::notifyConnectionChange(bool connected) {
    Guard lock(m_callbackMutex);
    if (m_connectionCallback) {
        m_connectionCallback(connected);
    }
}

void StratumClient::scheduleWorkTimeout() {
    if (!m_running || !m_workTimeoutTimer) return;

    // Cancel any existing timer
    m_workTimeoutTimer->cancel();

    // Schedule check in WORK_TIMEOUT seconds
    m_workTimeoutTimer->expires_after(std::chrono::seconds(WORK_TIMEOUT));
    m_workTimeoutTimer->async_wait([this](const boost::system::error_code& ec) {
        handleWorkTimeout(ec);
    });
}

void StratumClient::handleWorkTimeout(const boost::system::error_code& ec) {
    if (ec || !m_running) return;

    // Only check if we're authorized (actively mining)
    if (m_state != StratumState::Authorized) {
        // Not yet mining, reschedule
        scheduleWorkTimeout();
        return;
    }

    // Check how long since last work
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastWorkTime).count();

    if (elapsed >= WORK_TIMEOUT) {
        Log::warning("No new work received for " + std::to_string(elapsed) +
                    " seconds, reconnecting...");
        handleReconnect();
        return;
    }

    // Reschedule for remaining time
    unsigned remaining = WORK_TIMEOUT - static_cast<unsigned>(elapsed);
    m_workTimeoutTimer->expires_after(std::chrono::seconds(remaining));
    m_workTimeoutTimer->async_wait([this](const boost::system::error_code& ec) {
        handleWorkTimeout(ec);
    });
}

}  // namespace tos
