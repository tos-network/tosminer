/**
 * TOS Miner - Base Miner Interface
 *
 * Abstract base class for all mining backends (CPU, OpenCL, CUDA)
 */

#pragma once

#include "Types.h"
#include "WorkPackage.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace tos {

// Forward declaration
class Farm;

/**
 * Device descriptor
 *
 * Represents a mining device (GPU or CPU)
 */
struct DeviceDescriptor {
    // Device type
    MinerType type;

    // Unique identifier within type (e.g., GPU index)
    unsigned index;

    // Display name
    std::string name;

    // Total memory in bytes
    size_t totalMemory;

    // For GPU: compute units / multiprocessors
    unsigned computeUnits;

    // For OpenCL
    std::string clPlatformName;
    unsigned clPlatformIndex;
    unsigned clDeviceIndex;

    // For CUDA
    int cudaDeviceIndex;
    int cudaComputeCapabilityMajor;
    int cudaComputeCapabilityMinor;

    DeviceDescriptor()
        : type(MinerType::CPU)
        , index(0)
        , totalMemory(0)
        , computeUnits(0)
        , clPlatformIndex(0)
        , clDeviceIndex(0)
        , cudaDeviceIndex(-1)
        , cudaComputeCapabilityMajor(0)
        , cudaComputeCapabilityMinor(0)
    {}
};

/**
 * Hash rate structure
 */
struct HashRate {
    double rate;        // Hashes per second
    uint64_t count;     // Total hashes computed
    double duration;    // Time in seconds

    HashRate() : rate(0), count(0), duration(0) {}
    HashRate(double r, uint64_t c, double d) : rate(r), count(c), duration(d) {}
};

/**
 * Solution callback type
 */
using SolutionCallback = std::function<void(const Solution&, const std::string&)>;

/**
 * Miner base class
 *
 * Provides common functionality for all mining backends
 */
class Miner {
public:
    /**
     * Constructor
     *
     * @param index Miner index within the farm
     * @param device Device to mine on
     */
    Miner(unsigned index, const DeviceDescriptor& device);

    /**
     * Destructor
     */
    virtual ~Miner();

    // Non-copyable
    Miner(const Miner&) = delete;
    Miner& operator=(const Miner&) = delete;

    /**
     * Initialize the miner
     *
     * @return true if initialization successful
     */
    virtual bool init() = 0;

    /**
     * Start mining
     */
    void start();

    /**
     * Stop mining
     */
    void stop();

    /**
     * Check if miner is running
     */
    bool isRunning() const { return m_running; }

    /**
     * Set work package
     *
     * @param work New work to mine
     */
    void setWork(const WorkPackage& work);

    /**
     * Set solution callback
     *
     * @param callback Function to call when solution found
     */
    void setSolutionCallback(SolutionCallback callback);

    /**
     * Get current hash rate
     */
    HashRate getHashRate() const;

    /**
     * Reset hash counters
     */
    void resetHashCount();

    /**
     * Get device descriptor
     */
    const DeviceDescriptor& getDevice() const { return m_device; }

    /**
     * Get miner index
     */
    unsigned getIndex() const { return m_index; }

    /**
     * Get miner name (for logging)
     */
    virtual std::string getName() const;

    /**
     * Pause mining (keep resources allocated)
     */
    void pause();

    /**
     * Resume mining after pause
     */
    void resume();

    /**
     * Check if paused
     */
    bool isPaused() const { return m_paused; }

protected:
    /**
     * Main mining loop - implemented by subclasses
     *
     * Should check m_running and m_paused flags
     * Should call updateHashCount() periodically
     * Should call submitSolution() when solution found
     */
    virtual void mineLoop() = 0;

    /**
     * Update hash count
     *
     * @param count Number of hashes computed
     */
    void updateHashCount(uint64_t count);

    /**
     * Submit a found solution (after verification)
     *
     * @param solution The valid solution
     */
    void submitSolution(const Solution& solution);

    /**
     * Verify and submit a solution
     * Computes hash on CPU to verify before submitting
     *
     * @param nonce The nonce to verify
     * @return true if solution was valid and submitted
     */
    bool verifySolution(uint64_t nonce);

    /**
     * Get current work package (thread-safe copy)
     */
    WorkPackage getWork() const;

    /**
     * Check if new work is available
     */
    bool hasNewWork() const { return m_newWork; }

    /**
     * Clear new work flag
     */
    void clearNewWorkFlag() { m_newWork = false; }

protected:
    // Miner index
    unsigned m_index;

    // Device descriptor
    DeviceDescriptor m_device;

    // Running state
    std::atomic<bool> m_running{false};

    // Paused state
    std::atomic<bool> m_paused{false};

    // Mining thread
    std::thread m_thread;

    // Current work package
    WorkPackage m_work;
    mutable std::mutex m_workMutex;

    // New work available flag
    std::atomic<bool> m_newWork{false};

    // Hash counting
    std::atomic<uint64_t> m_hashCount{0};
    std::chrono::steady_clock::time_point m_startTime;

    // Solution callback
    SolutionCallback m_solutionCallback;
    std::mutex m_callbackMutex;

    // Error recovery
    std::atomic<unsigned> m_consecutiveErrors{0};
    static constexpr unsigned MAX_CONSECUTIVE_ERRORS = 10;

    /**
     * Record an error occurred
     * @return true if should attempt recovery (reinit), false otherwise
     */
    bool recordError();

    /**
     * Reset error counter (call after successful operation)
     */
    void clearErrors() { m_consecutiveErrors = 0; }
};

}  // namespace tos
