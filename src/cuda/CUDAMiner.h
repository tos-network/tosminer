/**
 * TOS Miner - CUDA Miner
 *
 * GPU mining implementation using NVIDIA CUDA
 */

#pragma once

#ifdef WITH_CUDA

#include "core/Miner.h"
#include <cuda_runtime.h>
#include <vector>

namespace tos {

/**
 * CUDA Miner class
 *
 * Implements TOS Hash V3 mining on NVIDIA GPUs using CUDA
 */
class CUDAMiner : public Miner {
public:
    /**
     * Constructor
     *
     * @param index Miner index
     * @param device Device descriptor
     */
    CUDAMiner(unsigned index, const DeviceDescriptor& device);

    /**
     * Destructor
     */
    ~CUDAMiner() override;

    /**
     * Initialize CUDA context
     */
    bool init() override;

    /**
     * Get miner name
     */
    std::string getName() const override;

    /**
     * Enumerate available CUDA devices
     */
    static std::vector<DeviceDescriptor> enumDevices();

    /**
     * Set grid size multiplier
     */
    static void setGridSizeMultiplier(unsigned multiplier) {
        s_gridSizeMultiplier = multiplier;
    }

    /**
     * Set block size
     */
    static void setBlockSize(unsigned size) {
        s_blockSize = size;
    }

protected:
    /**
     * Main mining loop
     */
    void mineLoop() override;

private:
    /**
     * Allocate GPU buffers
     */
    bool allocateBuffers();

    /**
     * Free GPU buffers
     */
    void freeBuffers();

    /**
     * Launch a batch on specified stream
     *
     * @param startNonce Starting nonce
     * @param streamIndex Which stream to use
     */
    void launchBatch(uint64_t startNonce, unsigned streamIndex);

    /**
     * Read results from specified stream
     *
     * @param streamIndex Which stream to read from
     * @return Number of solutions found
     */
    uint32_t readResults(unsigned streamIndex);

    /**
     * Process solutions from specified stream
     *
     * @param streamIndex Stream containing results
     * @param startNonce Starting nonce for this batch
     */
    void processSolutions(unsigned streamIndex, uint64_t startNonce);

private:
    // Multi-stream constants
    static constexpr unsigned c_numStreams = 2;

    // CUDA streams (multi-stream pipeline)
    cudaStream_t m_streams[c_numStreams];

    // GPU buffers (per stream)
    uint32_t* d_output[c_numStreams];

    // Host-side output buffers (per stream, pinned memory for async transfer)
    uint32_t* m_output[c_numStreams];

    // Batch tracking
    uint64_t m_batchNonce[c_numStreams];  // Starting nonce for each stream's batch
    unsigned m_currentStream = 0;
    uint64_t m_batchCount = 0;

    // Grid and block dimensions
    unsigned m_gridSize;
    unsigned m_blockSize;

    // Maximum solutions per batch
    static constexpr uint32_t MAX_OUTPUTS = 64;
    static constexpr size_t OUTPUT_SIZE = (1 + MAX_OUTPUTS * 2) * sizeof(uint32_t);

    // Static configuration
    static unsigned s_gridSizeMultiplier;
    static unsigned s_blockSize;
};

}  // namespace tos

#endif  // WITH_CUDA
