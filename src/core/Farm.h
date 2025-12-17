/**
 * TOS Miner - Farm Coordinator
 *
 * Manages multiple miners and coordinates work distribution
 */

#pragma once

#include "Miner.h"
#include "Types.h"
#include "WorkPackage.h"
#include <memory>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

namespace tos {

/**
 * Solution found callback
 */
using FarmSolutionCallback = std::function<void(const Solution&, const std::string& jobId)>;

/**
 * Farm class
 *
 * Coordinates multiple miners across different devices
 */
class Farm {
public:
    /**
     * Constructor
     */
    Farm();

    /**
     * Destructor
     */
    ~Farm();

    // Non-copyable
    Farm(const Farm&) = delete;
    Farm& operator=(const Farm&) = delete;

    /**
     * Add a miner to the farm
     *
     * @param miner Miner instance to add (takes ownership)
     */
    void addMiner(std::unique_ptr<Miner> miner);

    /**
     * Get number of miners
     */
    size_t minerCount() const { return m_miners.size(); }

    /**
     * Start all miners
     *
     * @return true if at least one miner started successfully
     */
    bool start();

    /**
     * Stop all miners
     */
    void stop();

    /**
     * Check if farm is running
     */
    bool isRunning() const { return m_running; }

    /**
     * Pause all miners
     */
    void pause();

    /**
     * Resume all miners
     */
    void resume();

    /**
     * Check if farm is paused
     */
    bool isPaused() const { return m_paused; }

    /**
     * Set new work for all miners
     *
     * @param work Work package to mine
     */
    void setWork(const WorkPackage& work);

    /**
     * Get current work package
     */
    const WorkPackage& getWork() const { return m_currentWork; }

    /**
     * Set solution callback
     *
     * @param callback Function to call when any miner finds a solution
     */
    void setSolutionCallback(FarmSolutionCallback callback);

    /**
     * Get combined hash rate from all miners
     */
    HashRate getHashRate() const;

    /**
     * Get hash rate for specific miner
     *
     * @param index Miner index
     */
    HashRate getMinerHashRate(unsigned index) const;

    /**
     * Get mining statistics (returns copyable snapshot)
     */
    MiningStatsSnapshot getStats() const { return m_stats.snapshot(); }

    /**
     * Reset all statistics
     */
    void resetStats();

    /**
     * Get list of all devices in farm
     */
    std::vector<DeviceDescriptor> getDevices() const;

    /**
     * Record accepted share
     */
    void recordAcceptedShare() { m_stats.acceptedShares++; }

    /**
     * Record rejected share
     */
    void recordRejectedShare() { m_stats.rejectedShares++; }

    /**
     * Record stale share
     */
    void recordStaleShare() { m_stats.staleShares++; }

    /**
     * Enumerate available mining devices
     *
     * @param enumCPU Include CPU devices
     * @param enumOpenCL Include OpenCL devices
     * @param enumCUDA Include CUDA devices
     * @return List of available devices
     */
    static std::vector<DeviceDescriptor> enumDevices(
        bool enumCPU = true,
        bool enumOpenCL = true,
        bool enumCUDA = true
    );

private:
    /**
     * Internal solution handler
     */
    void onSolution(const Solution& solution, const std::string& jobId);

private:
    // List of miners
    std::vector<std::unique_ptr<Miner>> m_miners;
    mutable std::mutex m_minersMutex;

    // Running state
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};

    // Current work package
    WorkPackage m_currentWork;
    mutable std::mutex m_workMutex;

    // Solution callback
    FarmSolutionCallback m_solutionCallback;
    std::mutex m_callbackMutex;

    // Statistics
    MiningStats m_stats;

    // Timing
    std::chrono::steady_clock::time_point m_startTime;
};

}  // namespace tos
