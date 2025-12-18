/**
 * TOS Miner - CPU Miner
 *
 * CPU mining implementation using TosHash
 */

#pragma once

#include "core/Miner.h"
#include "toshash/TosHash.h"
#include <vector>
#include <thread>

namespace tos {

/**
 * CPU Miner class
 *
 * Implements TOS Hash V3 mining on CPU
 */
class CPUMiner : public Miner {
public:
    /**
     * Constructor
     *
     * @param index Miner index
     * @param device Device descriptor
     */
    CPUMiner(unsigned index, const DeviceDescriptor& device);

    /**
     * Destructor
     */
    ~CPUMiner() override;

    /**
     * Initialize CPU miner
     */
    bool init() override;

    /**
     * Get miner name
     */
    std::string getName() const override;

    /**
     * Enumerate available CPU devices
     * Returns a single device representing the CPU with thread count
     */
    static std::vector<DeviceDescriptor> enumDevices();

    /**
     * Set number of mining threads
     * @param threads Number of threads (0 = auto-detect)
     */
    static void setThreadCount(unsigned threads) {
        s_threadCount = threads;
    }

    /**
     * Get configured thread count
     */
    static unsigned getThreadCount() {
        return s_threadCount;
    }

protected:
    /**
     * Main mining loop
     */
    void mineLoop() override;

private:
    // TosHash instance for CPU hashing
    TosHash m_hasher;

    // Scratchpad for hash computation (64KB)
    ScratchPad m_scratch;

    // Batch size for hash count updates
    static constexpr uint64_t BATCH_SIZE = 1024;

    // Static configuration
    static unsigned s_threadCount;
};

}  // namespace tos
