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

    double rate = (duration > 0) ? (static_cast<double>(count) / duration) : 0;
    return HashRate(rate, count, duration);
}

void Miner::resetHashCount() {
    m_hashCount = 0;
    m_startTime = std::chrono::steady_clock::now();
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
        return false;
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

        Log::info(getName() + ": Verified solution nonce=" + std::to_string(nonce));
        submitSolution(solution);
        return true;
    } else {
        // Invalid solution - GPU reported false positive
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

}  // namespace tos
