/**
 * TOS Miner - Farm Implementation
 */

#include "Farm.h"
#include "util/Log.h"
#include <sstream>
#include <future>
#include <vector>

#ifdef WITH_OPENCL
// OpenCL device enumeration will be in CLMiner
#endif

#ifdef WITH_CUDA
// CUDA device enumeration will be in CUDAMiner
#endif

namespace tos {

Farm::Farm() {
}

Farm::~Farm() {
    stop();
}

void Farm::addMiner(std::unique_ptr<Miner> miner) {
    Guard lock(m_minersMutex);
    m_miners.push_back(std::move(miner));
}

size_t Farm::activeMinerCount() const {
    Guard lock(m_failedMinersMutex);
    return m_miners.size() - m_failedMiners.size();
}

bool Farm::isMinerFailed(unsigned index) const {
    Guard lock(m_failedMinersMutex);
    return m_failedMiners.find(index) != m_failedMiners.end();
}

void Farm::markMinerFailed(unsigned index) {
    {
        Guard lock(m_failedMinersMutex);
        if (m_failedMiners.find(index) != m_failedMiners.end()) {
            return;  // Already marked as failed
        }
        m_failedMiners.insert(index);
    }

    Guard lock(m_minersMutex);
    if (index < m_miners.size()) {
        Log::warning("Miner " + m_miners[index]->getName() + " marked as failed, isolating from work distribution");
        m_miners[index]->pause();
    }
}

unsigned Farm::recoverFailedMiners() {
    std::vector<unsigned> toRecover;

    {
        Guard lock(m_failedMinersMutex);
        for (unsigned idx : m_failedMiners) {
            toRecover.push_back(idx);
        }
    }

    if (toRecover.empty()) {
        return 0;
    }

    unsigned recovered = 0;
    Guard lock(m_minersMutex);

    for (unsigned idx : toRecover) {
        if (idx >= m_miners.size()) continue;

        auto& miner = m_miners[idx];
        Log::info("Attempting to recover " + miner->getName() + "...");

        // Stop, reinit, and restart
        miner->stop();

        if (miner->init()) {
            miner->setSolutionCallback([this](const Solution& sol, const std::string& jobId) {
                onSolution(sol, jobId);
            });

            // Give it current work
            {
                Guard workLock(m_workMutex);
                if (m_currentWork.valid) {
                    miner->setWork(m_currentWork);
                }
            }

            miner->start();

            {
                Guard failLock(m_failedMinersMutex);
                m_failedMiners.erase(idx);
            }

            Log::info(miner->getName() + " recovered successfully");
            recovered++;
        } else {
            Log::error("Failed to recover " + miner->getName());
        }
    }

    return recovered;
}

bool Farm::start() {
    if (m_running) {
        return true;
    }

    Guard lock(m_minersMutex);

    if (m_miners.empty()) {
        Log::error("No miners to start");
        return false;
    }

    Log::info("Starting farm with " + std::to_string(m_miners.size()) + " miner(s)");

    m_startTime = std::chrono::steady_clock::now();
    m_stats.reset();

    // Set solution callback for all miners first
    for (auto& miner : m_miners) {
        miner->setSolutionCallback([this](const Solution& sol, const std::string& jobId) {
            onSolution(sol, jobId);
        });
    }

    // Initialize miners in parallel for faster startup with multiple GPUs
    std::vector<std::future<bool>> initFutures;
    initFutures.reserve(m_miners.size());

    Log::info("Initializing " + std::to_string(m_miners.size()) + " device(s) in parallel...");

    for (size_t i = 0; i < m_miners.size(); i++) {
        initFutures.push_back(std::async(std::launch::async, [this, i]() {
            return m_miners[i]->init();
        }));
    }

    // Wait for all initializations and start successful ones
    int started = 0;
    for (size_t i = 0; i < m_miners.size(); i++) {
        bool success = initFutures[i].get();
        if (success) {
            m_miners[i]->start();
            started++;
            Log::info(m_miners[i]->getName() + " initialized successfully");
        } else {
            Log::error("Failed to initialize " + m_miners[i]->getName());
        }
    }

    if (started > 0) {
        m_running = true;
        m_paused = false;
        Log::info("Farm started with " + std::to_string(started) + " active miner(s)");
        return true;
    }

    Log::error("Failed to start any miners");
    return false;
}

void Farm::stop() {
    if (!m_running) {
        return;
    }

    Log::info("Stopping farm...");

    m_running = false;
    m_paused = false;

    Guard lock(m_minersMutex);
    for (auto& miner : m_miners) {
        miner->stop();
    }

    Log::info("Farm stopped");
}

void Farm::pause() {
    if (!m_running || m_paused) {
        return;
    }

    m_paused = true;

    Guard lock(m_minersMutex);
    for (auto& miner : m_miners) {
        miner->pause();
    }

    Log::info("Farm paused");
}

void Farm::resume() {
    if (!m_running || !m_paused) {
        return;
    }

    Guard lock(m_minersMutex);
    for (auto& miner : m_miners) {
        miner->resume();
    }

    m_paused = false;
    Log::info("Farm resumed");
}

void Farm::setWork(const WorkPackage& work) {
    Guard lock(m_minersMutex);

    // Count active (non-failed) miners
    unsigned activeCount = 0;
    {
        Guard failLock(m_failedMinersMutex);
        activeCount = static_cast<unsigned>(m_miners.size() - m_failedMiners.size());
    }

    if (activeCount == 0) {
        Log::warning("No active miners to receive work");
        return;
    }

    // Create a copy with totalDevices set to active miner count
    WorkPackage distributedWork = work;
    distributedWork.totalDevices = activeCount;

    {
        Guard workLock(m_workMutex);
        // Save current work as fallback before replacing (if it was valid)
        if (m_currentWork.valid) {
            m_previousWork = m_currentWork;
        }
        m_currentWork = distributedWork;
    }

    // Only distribute to non-failed miners
    for (size_t i = 0; i < m_miners.size(); i++) {
        if (!isMinerFailed(static_cast<unsigned>(i))) {
            m_miners[i]->setWork(distributedWork);
        }
    }

    std::ostringstream ss;
    ss << "New work: job=" << work.jobId
       << " height=" << work.height
       << " active_devices=" << activeCount;
    if (activeCount < m_miners.size()) {
        ss << " (total=" << m_miners.size() << ", failed=" << (m_miners.size() - activeCount) << ")";
    }
    Log::info(ss.str());
}

void Farm::setSolutionCallback(FarmSolutionCallback callback) {
    Guard lock(m_callbackMutex);
    m_solutionCallback = std::move(callback);
}

HashRate Farm::getHashRate() const {
    Guard lock(m_minersMutex);

    auto now = std::chrono::steady_clock::now();
    double totalDuration = std::chrono::duration<double>(now - m_startTime).count();

    uint64_t totalCount = 0;
    double totalRate = 0;

    for (size_t i = 0; i < m_miners.size(); i++) {
        // Only count active miners
        if (!isMinerFailed(static_cast<unsigned>(i))) {
            auto hr = m_miners[i]->getHashRate();
            totalCount += hr.count;
            totalRate += hr.rate;
        }
    }

    return HashRate(totalRate, totalCount, totalDuration);
}

HashRate Farm::getMinerHashRate(unsigned index) const {
    Guard lock(m_minersMutex);

    if (index < m_miners.size()) {
        return m_miners[index]->getHashRate();
    }

    return HashRate();
}

void Farm::resetStats() {
    m_stats.reset();
    m_startTime = std::chrono::steady_clock::now();

    Guard lock(m_minersMutex);
    for (auto& miner : m_miners) {
        miner->resetHashCount();
    }
}

std::vector<DeviceDescriptor> Farm::getDevices() const {
    Guard lock(m_minersMutex);

    std::vector<DeviceDescriptor> devices;
    devices.reserve(m_miners.size());

    for (const auto& miner : m_miners) {
        devices.push_back(miner->getDevice());
    }

    return devices;
}

void Farm::onSolution(const Solution& solution, const std::string& jobId) {
    std::ostringstream ss;
    ss << "Solution found! nonce=" << solution.nonce
       << " job=" << jobId;
    Log::info(ss.str());

    Guard lock(m_callbackMutex);
    if (m_solutionCallback) {
        m_solutionCallback(solution, jobId);
    }
}

std::vector<DeviceDescriptor> Farm::enumDevices(bool enumCPU, bool enumOpenCL, bool enumCUDA) {
    std::vector<DeviceDescriptor> devices;

    // CPU enumeration (simple - just count threads)
    if (enumCPU) {
        DeviceDescriptor cpu;
        cpu.type = MinerType::CPU;
        cpu.index = 0;
        cpu.name = "CPU";
        cpu.computeUnits = std::thread::hardware_concurrency();
        devices.push_back(cpu);
    }

    // OpenCL enumeration would be added by CLMiner::enumDevices()
#ifdef WITH_OPENCL
    if (enumOpenCL) {
        // Will be implemented in CLMiner.cpp
        // auto clDevices = CLMiner::enumDevices();
        // devices.insert(devices.end(), clDevices.begin(), clDevices.end());
    }
#endif

    // CUDA enumeration would be added by CUDAMiner::enumDevices()
#ifdef WITH_CUDA
    if (enumCUDA) {
        // Will be implemented in CUDAMiner.cpp
        // auto cuDevices = CUDAMiner::enumDevices();
        // devices.insert(devices.end(), cuDevices.begin(), cuDevices.end());
    }
#endif

    return devices;
}

bool Farm::hasFallbackWork() const {
    Guard lock(m_workMutex);
    return m_previousWork.valid && !m_previousWork.isStale(FALLBACK_WORK_MAX_AGE);
}

WorkPackage Farm::getFallbackWork() const {
    Guard lock(m_workMutex);
    if (m_previousWork.valid && !m_previousWork.isStale(FALLBACK_WORK_MAX_AGE)) {
        return m_previousWork;
    }
    return WorkPackage();  // Return invalid work
}

bool Farm::activateFallbackWork() {
    Guard lock(m_minersMutex);
    Guard workLock(m_workMutex);

    // Only activate if current work is invalid and fallback is available
    if (m_currentWork.valid) {
        return false;  // Current work is fine
    }

    if (!m_previousWork.valid || m_previousWork.isStale(FALLBACK_WORK_MAX_AGE)) {
        return false;  // No valid fallback
    }

    Log::warning("Activating fallback work (job=" + m_previousWork.jobId +
                 ", age=" + std::to_string(m_previousWork.getAgeSeconds()) + "s)");

    // Distribute fallback work to miners
    for (size_t i = 0; i < m_miners.size(); i++) {
        if (!isMinerFailed(static_cast<unsigned>(i))) {
            m_miners[i]->setWork(m_previousWork);
        }
    }

    // Move fallback to current (don't keep using same fallback)
    m_currentWork = m_previousWork;
    m_previousWork.valid = false;

    return true;
}

}  // namespace tos
