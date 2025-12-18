/**
 * TOS Miner - Base Miner Implementation
 */

#include "Miner.h"
#include "toshash/TosHash.h"
#include "util/Log.h"
#include <sstream>
#include <cstring>

namespace tos {

// Thread-local TosHash instance and scratchpad for verification
// (avoids allocation overhead on every verification)
thread_local TosHash t_hasher;
thread_local ScratchPad t_scratch;

Miner::Miner(unsigned index, const DeviceDescriptor& device)
    : m_index(index)
    , m_device(device)
{
}

Miner::~Miner() {
    stop();
}

void Miner::start() {
    if (m_running) {
        return;
    }

    m_running = true;
    m_paused = false;
    m_hashCount = 0;
    m_startTime = std::chrono::steady_clock::now();

    m_thread = std::thread([this]() {
        Log::info(getName() + " started");
        try {
            mineLoop();
        } catch (const std::exception& e) {
            Log::error(getName() + " error: " + e.what());
        }
        Log::info(getName() + " stopped");
    });
}

void Miner::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;
    m_paused = false;

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void Miner::setWork(const WorkPackage& work) {
    bool jobChanged = false;
    {
        Guard lock(m_workMutex);
        // Check if job ID changed (new work)
        jobChanged = (work.jobId != m_work.jobId);
        m_work = work;
    }

    // Clear submitted nonces when starting a new job
    if (jobChanged) {
        clearSubmittedNonces();
    }

    m_newWork = true;
}

void Miner::setSolutionCallback(SolutionCallback callback) {
    Guard lock(m_callbackMutex);
    m_solutionCallback = std::move(callback);
}

HashRate Miner::getHashRate() const {
    auto now = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(now - m_startTime).count();
    uint64_t count = m_hashCount.load();

    // Calculate instantaneous rate
    double instantRate = (duration > 0) ? (static_cast<double>(count) / duration) : 0;

    // Get EMA rate (thread-safe)
    double emaRate;
    {
        SpinGuard lock(m_hashRateLock);
        emaRate = m_hashRateCalc.getEmaRate();
    }

    return HashRate(instantRate, emaRate, count, duration);
}

void Miner::resetHashCount() {
    m_hashCount = 0;
    m_startTime = std::chrono::steady_clock::now();

    // Reset EMA calculator
    SpinGuard lock(m_hashRateLock);
    m_hashRateCalc.reset();
}

std::string Miner::getName() const {
    std::ostringstream ss;
    switch (m_device.type) {
        case MinerType::CPU:
            ss << "CPU";
            break;
        case MinerType::OpenCL:
            ss << "CL";
            break;
        case MinerType::CUDA:
            ss << "CU";
            break;
        default:
            ss << "??";
            break;
    }
    ss << m_index;
    return ss.str();
}

void Miner::pause() {
    m_paused = true;
}

void Miner::resume() {
    m_paused = false;
}

void Miner::updateHashCount(uint64_t count) {
    m_hashCount += count;

    // Update EMA calculator (thread-safe with SpinLock)
    SpinGuard lock(m_hashRateLock);
    m_hashRateCalc.update(m_hashCount.load());
}

void Miner::submitSolution(const Solution& solution) {
    Guard lock(m_callbackMutex);
    if (m_solutionCallback) {
        m_solutionCallback(solution, m_work.jobId);
    }
}

bool Miner::verifySolution(uint64_t nonce) {
    // Get current work
    WorkPackage work = getWork();
    if (!work.valid) {
        return false;
    }

    // Check for duplicate before expensive verification
    if (isDuplicateNonce(nonce)) {
        Log::warning(getName() + ": Duplicate nonce " + std::to_string(nonce) + " (GPU fault?)");
        {
            Guard lock(m_healthMutex);
            m_health.duplicateSolutions++;
        }
        return false;
    }

    // Validate nonce is within device's allocated range
    if (work.totalDevices > 1) {
        uint64_t deviceStart = work.getDeviceStartNonce(m_index);
        uint64_t rangeSize = UINT64_MAX / work.totalDevices;
        uint64_t deviceEnd = deviceStart + rangeSize;

        // Check if nonce is outside this device's range
        if (nonce < deviceStart || (deviceEnd > deviceStart && nonce >= deviceEnd)) {
            Log::warning(getName() + ": Nonce " + std::to_string(nonce) +
                        " outside device range [" + std::to_string(deviceStart) +
                        ", " + std::to_string(deviceEnd) + ") - possible GPU fault");
            return false;
        }
    }

    // Prepare input with nonce
    std::array<uint8_t, INPUT_SIZE> input;
    std::memcpy(input.data(), work.header.data(), INPUT_SIZE);

    // Set nonce in last 8 bytes (little-endian)
    for (int i = 0; i < 8; i++) {
        input[INPUT_SIZE - 8 + i] = static_cast<uint8_t>(nonce >> (i * 8));
    }

    // Compute hash using thread-local hasher
    Hash256 hash;
    t_hasher.hash(input.data(), hash.data(), t_scratch);

    // Check if hash meets target
    if (meetsTarget(hash, work.target)) {
        // Valid solution - submit it
        Solution solution;
        solution.nonce = nonce;
        solution.hash = hash;
        solution.deviceIndex = m_index;  // Track which device found it

        // Record this nonce to prevent duplicate submissions
        recordSubmittedNonce(nonce);

        // Update health metrics
        recordValidSolution();

        Log::info(getName() + ": Verified solution nonce=" + std::to_string(nonce));
        submitSolution(solution);
        return true;
    } else {
        // Invalid solution - GPU reported false positive
        recordInvalidSolution();
        Log::warning(getName() + ": Invalid solution discarded (nonce=" + std::to_string(nonce) + ")");
        return false;
    }
}

WorkPackage Miner::getWork() const {
    Guard lock(m_workMutex);
    return m_work;
}

bool Miner::recordError() {
    unsigned errors = ++m_consecutiveErrors;
    if (errors >= MAX_CONSECUTIVE_ERRORS) {
        Log::error(getName() + ": Too many consecutive errors (" +
                   std::to_string(errors) + "), needs recovery");
        m_consecutiveErrors = 0;
        return true;  // Request recovery
    }
    return false;
}

bool Miner::isDuplicateNonce(uint64_t nonce) {
    Guard lock(m_submittedNoncesMutex);
    return m_submittedNonces.find(nonce) != m_submittedNonces.end();
}

void Miner::recordSubmittedNonce(uint64_t nonce) {
    Guard lock(m_submittedNoncesMutex);

    // If we've hit the limit, clear half the oldest entries
    // (simple approach - clear all and start fresh)
    if (m_submittedNonces.size() >= MAX_SUBMITTED_NONCES) {
        m_submittedNonces.clear();
    }

    m_submittedNonces.insert(nonce);
}

void Miner::clearSubmittedNonces() {
    Guard lock(m_submittedNoncesMutex);
    m_submittedNonces.clear();
}

DeviceHealth Miner::getHealth() const {
    Guard lock(m_healthMutex);
    return m_health;
}

void Miner::recordValidSolution() {
    Guard lock(m_healthMutex);
    m_health.validSolutions++;
    m_health.lastSolutionTime = std::chrono::steady_clock::now();
    updateHealthStatus();
}

void Miner::recordInvalidSolution() {
    Guard lock(m_healthMutex);
    m_health.invalidSolutions++;
    updateHealthStatus();
}

void Miner::recordHardwareError() {
    Guard lock(m_healthMutex);
    m_health.hardwareErrors++;
    updateHealthStatus();
}

void Miner::updateHealthStatus() {
    // Must be called with m_healthMutex held

    // Update current hash rate
    auto hr = getHashRate();
    m_health.currentHashRate = hr.rate;

    // Track peak hash rate
    if (hr.rate > m_health.peakHashRate) {
        m_health.peakHashRate = hr.rate;
    }

    // Detect significant hash rate drops
    if (m_health.peakHashRate > 0 &&
        m_health.currentHashRate < m_health.peakHashRate * HASHRATE_DROP_THRESHOLD) {
        m_health.hashRateDrops++;
    }

    // Update last hash update time
    m_health.lastHashUpdate = std::chrono::steady_clock::now();

    // Determine health status based on metrics
    double validity = m_health.getValidityRate();
    uint64_t totalSolutions = m_health.validSolutions + m_health.invalidSolutions;

    // Need some solutions before making judgments
    if (totalSolutions < 5) {
        m_health.status = HealthStatus::Healthy;
        return;
    }

    // Check for failure conditions
    if (m_health.hardwareErrors > 50 || validity < 0.5) {
        m_health.status = HealthStatus::Failed;
        Log::error(getName() + ": Device marked as FAILED (validity=" +
                   std::to_string(validity * 100) + "%, errors=" +
                   std::to_string(m_health.hardwareErrors) + ")");
    }
    // Check for unhealthy conditions
    else if (validity < VALIDITY_THRESHOLD_UNHEALTHY || m_health.hardwareErrors > 20) {
        m_health.status = HealthStatus::Unhealthy;
        Log::warning(getName() + ": Device health UNHEALTHY (validity=" +
                     std::to_string(validity * 100) + "%)");
    }
    // Check for degraded conditions
    else if (validity < VALIDITY_THRESHOLD_DEGRADED || m_health.hardwareErrors > 5) {
        m_health.status = HealthStatus::Degraded;
        Log::debug(getName() + ": Device health degraded (validity=" +
                   std::to_string(validity * 100) + "%)");
    }
    else {
        m_health.status = HealthStatus::Healthy;
    }
}

}  // namespace tos
