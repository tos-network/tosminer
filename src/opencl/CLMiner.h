/**
 * TOS Miner - OpenCL Miner
 *
 * GPU mining implementation using OpenCL
 */

#pragma once

#ifdef WITH_OPENCL

#include "core/Miner.h"
#include <CL/cl.hpp>
#include <vector>
#include <queue>

namespace tos {

/**
 * Pending batch structure for double buffering
 */
struct PendingBatch {
    uint64_t startNonce;    // Starting nonce for this batch
    unsigned bufferIndex;   // Which buffer was used
    cl::Event event;        // Completion event
};

/**
 * OpenCL Miner class
 *
 * Implements TOS Hash V3 mining on OpenCL-compatible GPUs
 */
class CLMiner : public Miner {
public:
    /**
     * Constructor
     *
     * @param index Miner index
     * @param device Device descriptor
     */
    CLMiner(unsigned index, const DeviceDescriptor& device);

    /**
     * Destructor
     */
    ~CLMiner() override;

    /**
     * Initialize OpenCL context and compile kernel
     */
    bool init() override;

    /**
     * Get miner name
     */
    std::string getName() const override;

    /**
     * Enumerate available OpenCL devices
     */
    static std::vector<DeviceDescriptor> enumDevices();

    /**
     * Set global work size multiplier
     */
    static void setGlobalWorkSizeMultiplier(unsigned multiplier) {
        s_globalWorkSizeMultiplier = multiplier;
    }

    /**
     * Set local work size
     */
    static void setLocalWorkSize(unsigned size) {
        s_localWorkSize = size;
    }

protected:
    /**
     * Main mining loop
     */
    void mineLoop() override;

private:
    /**
     * Compile OpenCL kernel
     */
    bool compileKernel();

    /**
     * Allocate GPU buffers
     */
    bool allocateBuffers();

    /**
     * Enqueue a batch for async execution
     *
     * @param startNonce Starting nonce
     * @param bufferIndex Which buffer to use
     * @param event Output event for completion tracking
     */
    void enqueueBatch(uint64_t startNonce, unsigned bufferIndex, cl::Event& event);

    /**
     * Read results from a completed batch
     *
     * @param bufferIndex Which buffer to read
     * @return Number of solutions found
     */
    uint32_t readBatchResults(unsigned bufferIndex);

    /**
     * Process found solutions
     *
     * @param bufferIndex Buffer containing results
     * @param startNonce Starting nonce for this batch
     */
    void processSolutions(unsigned bufferIndex, uint64_t startNonce);

private:
    // OpenCL objects
    cl::Context m_context;
    cl::CommandQueue m_queue;
    cl::Program m_program;
    cl::Kernel m_searchKernel;
    cl::Kernel m_benchmarkKernel;

    // Double buffering constants
    static constexpr unsigned c_bufferCount = 2;

    // GPU buffers (double buffered)
    cl::Buffer m_outputBuffer[c_bufferCount];   // Solution output buffers
    cl::Buffer m_headerBuffer;   // Block header (constant)
    cl::Buffer m_targetBuffer;   // Target hash (constant)

    // Host-side output buffers (double buffered)
    std::vector<uint32_t> m_output[c_bufferCount];

    // Pending batch queue for async pipeline
    std::queue<PendingBatch> m_pending;

    // Current buffer index
    unsigned m_bufferIndex = 0;

    // Work sizes
    size_t m_globalWorkSize;
    size_t m_localWorkSize;

    // Maximum solutions per batch
    static constexpr uint32_t MAX_OUTPUTS = 64;

    // Static configuration
    static unsigned s_globalWorkSizeMultiplier;
    static unsigned s_localWorkSize;
};

}  // namespace tos

#endif  // WITH_OPENCL
