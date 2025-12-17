/**
 * TOS Miner - Farm Implementation
 */

#include "Farm.h"
#include "util/Log.h"
#include <sstream>

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

    int started = 0;
    for (auto& miner : m_miners) {
        // Set solution callback for this miner
        miner->setSolutionCallback([this](const Solution& sol, const std::string& jobId) {
            onSolution(sol, jobId);
        });

        // Initialize and start
        if (miner->init()) {
            miner->start();
            started++;
        } else {
            Log::error("Failed to initialize " + miner->getName());
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
    {
        Guard lock(m_workMutex);
        m_currentWork = work;
    }

    Guard lock(m_minersMutex);
    for (auto& miner : m_miners) {
        miner->setWork(work);
    }

    std::ostringstream ss;
    ss << "New work: job=" << work.jobId
       << " height=" << work.height;
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

    for (const auto& miner : m_miners) {
        auto hr = miner->getHashRate();
        totalCount += hr.count;
        totalRate += hr.rate;
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

}  // namespace tos
