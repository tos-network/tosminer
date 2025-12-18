/**
 * TOS Miner - CUDA Miner Implementation
 */

#ifdef WITH_CUDA

#include "CUDAMiner.h"
#include "core/WorkPackage.h"
#include "util/Log.h"
#include <sstream>
#include <chrono>
#include <thread>

// External kernel launch functions (defined in .cu file)
extern "C" {
    cudaError_t toshash_set_header(const uint8_t* header);
    cudaError_t toshash_set_target(const uint8_t* target);
}

// Kernel declaration
extern "C" __global__ void toshash_search(uint32_t* g_output, uint64_t start_nonce);

namespace tos {

// Static members
unsigned CUDAMiner::s_gridSizeMultiplier = 0;  // 0 = auto-tune based on GPU
unsigned CUDAMiner::s_blockSize = 1;  // Threads per block (1 for 64KB shared memory per thread)

CUDAMiner::CUDAMiner(unsigned index, const DeviceDescriptor& device)
    : Miner(index, device)
    , m_currentStream(0)
    , m_batchCount(0)
    , m_gridSize(0)
    , m_blockSize(1)
{
    // Initialize pointers to null
    for (unsigned i = 0; i < c_numStreams; i++) {
        m_streams[i] = nullptr;
        d_output[i] = nullptr;
        m_output[i] = nullptr;
        m_batchNonce[i] = 0;
    }
}

CUDAMiner::~CUDAMiner() {
    stop();
    freeBuffers();
}

bool CUDAMiner::init() {
    cudaError_t err;

    // Set device
    err = cudaSetDevice(m_device.cudaDeviceIndex);
    if (err != cudaSuccess) {
        Log::error(getName() + ": Failed to set CUDA device: " + cudaGetErrorString(err));
        return false;
    }

    // Get device properties
    cudaDeviceProp props;
    err = cudaGetDeviceProperties(&props, m_device.cudaDeviceIndex);
    if (err != cudaSuccess) {
        Log::error(getName() + ": Failed to get device properties: " + cudaGetErrorString(err));
        return false;
    }

    std::ostringstream ss;
    ss << getName() << ": " << props.name
       << " (SM " << props.major << "." << props.minor
       << ", " << (props.totalGlobalMem / (1024 * 1024)) << "MB"
       << ", " << props.multiProcessorCount << " SMs"
       << ", shared mem: " << (props.sharedMemPerBlock / 1024) << "KB)";
    Log::info(ss.str());

    // Check shared memory is sufficient (64KB for scratchpad)
    if (props.sharedMemPerBlock < 65536) {
        Log::error(getName() + ": Insufficient shared memory (need 64KB)");
        return false;
    }

    // Create multiple streams for pipelining
    for (unsigned i = 0; i < c_numStreams; i++) {
        err = cudaStreamCreate(&m_streams[i]);
        if (err != cudaSuccess) {
            Log::error(getName() + ": Failed to create CUDA stream " + std::to_string(i) + ": " + cudaGetErrorString(err));
            return false;
        }
    }

    // Allocate buffers
    if (!allocateBuffers()) {
        return false;
    }

    // Calculate optimal grid and block sizes based on device capabilities
    // TOS Hash requires 64KB shared memory per thread, so blockSize = 1
    // Grid size should scale with number of SMs and available memory

    m_blockSize = s_blockSize;  // Fixed at 1 due to shared memory requirement

    if (s_gridSizeMultiplier > 0) {
        // User specified grid size
        m_gridSize = s_gridSizeMultiplier;
    } else {
        // Auto-tune: scale grid size based on SMs
        // Each SM can run multiple blocks concurrently
        // For 64KB shared memory kernels, occupancy is limited
        // Typical: 1-2 blocks per SM depending on registers

        // Base calculation: SMs * blocks_per_SM * occupancy_factor
        unsigned blocksPerSM = 1;  // Conservative for 64KB shared memory
        if (props.sharedMemPerMultiprocessor >= 65536 * 2) {
            blocksPerSM = 2;  // Can fit 2 blocks if SM has 128KB+
        }

        // Scale by compute capability - newer GPUs can handle more
        unsigned smScaleFactor = 1;
        if (props.major >= 7) {
            smScaleFactor = 4;  // Volta and newer
        } else if (props.major >= 6) {
            smScaleFactor = 2;  // Pascal
        }

        // Calculate grid size: enough work to keep GPU saturated
        // Formula: SMs * blocksPerSM * scaleFactor * batchMultiplier
        unsigned batchMultiplier = 256;  // Process 256 batches worth per kernel launch
        m_gridSize = props.multiProcessorCount * blocksPerSM * smScaleFactor * batchMultiplier;

        // Ensure minimum grid size for small GPUs
        if (m_gridSize < 4096) {
            m_gridSize = 4096;
        }

        // Cap at reasonable maximum to avoid excessive memory/launch overhead
        if (m_gridSize > 65536) {
            m_gridSize = 65536;
        }
    }

    Log::info(getName() + ": Initialized with " + std::to_string(c_numStreams) +
              " streams (grid: " + std::to_string(m_gridSize) +
              ", block: " + std::to_string(m_blockSize) +
              ", SMs: " + std::to_string(props.multiProcessorCount) + ")");

    return true;
}

std::string CUDAMiner::getName() const {
    return "CU" + std::to_string(m_index);
}

bool CUDAMiner::allocateBuffers() {
    cudaError_t err;

    for (unsigned i = 0; i < c_numStreams; i++) {
        // Allocate device output buffer
        err = cudaMalloc(&d_output[i], OUTPUT_SIZE);
        if (err != cudaSuccess) {
            Log::error(getName() + ": Failed to allocate device buffer " + std::to_string(i) + ": " + cudaGetErrorString(err));
            return false;
        }

        // Allocate pinned host memory for async transfers
        err = cudaMallocHost(&m_output[i], OUTPUT_SIZE);
        if (err != cudaSuccess) {
            Log::error(getName() + ": Failed to allocate pinned host buffer " + std::to_string(i) + ": " + cudaGetErrorString(err));
            return false;
        }
    }

    Log::info(getName() + ": Buffers allocated (" + std::to_string(c_numStreams) + " streams)");
    return true;
}

void CUDAMiner::freeBuffers() {
    for (unsigned i = 0; i < c_numStreams; i++) {
        if (d_output[i]) {
            cudaFree(d_output[i]);
            d_output[i] = nullptr;
        }
        if (m_output[i]) {
            cudaFreeHost(m_output[i]);
            m_output[i] = nullptr;
        }
        if (m_streams[i]) {
            cudaStreamDestroy(m_streams[i]);
            m_streams[i] = nullptr;
        }
    }
}

void CUDAMiner::mineLoop() {
    uint64_t nonce = 0;
    uint64_t batchSize = m_gridSize * m_blockSize;

    // Set device for this thread
    cudaSetDevice(m_device.cudaDeviceIndex);

    // Reset state
    m_currentStream = 0;
    m_batchCount = 0;

    while (m_running) {
        // Check for pause
        if (m_paused) {
            // Wait for all streams to complete before pausing
            for (unsigned i = 0; i < c_numStreams; i++) {
                cudaStreamSynchronize(m_streams[i]);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            m_batchCount = 0;
            continue;
        }

        // Check for new work
        if (hasNewWork()) {
            clearNewWorkFlag();
            WorkPackage work = getWork();

            if (!work.valid) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // Wait for all streams to complete before switching work
            for (unsigned i = 0; i < c_numStreams; i++) {
                cudaStreamSynchronize(m_streams[i]);
            }

            // Upload new header and target to constant memory
            cudaError_t err;

            err = toshash_set_header(work.header.data());
            if (err != cudaSuccess) {
                Log::error(getName() + ": Failed to set header: " + cudaGetErrorString(err));
                continue;
            }

            err = toshash_set_target(work.target.data());
            if (err != cudaSuccess) {
                Log::error(getName() + ": Failed to set target: " + cudaGetErrorString(err));
                continue;
            }

            // Get device-specific starting nonce (non-overlapping range)
            nonce = work.getDeviceStartNonce(m_index);
            m_currentStream = 0;
            m_batchCount = 0;
        }

        // Multi-stream pipeline:
        // - Launch new batch on current stream
        // - If we have c_numStreams batches in flight, wait for oldest and process

        unsigned streamIdx = m_currentStream;

        // If we have filled all streams, wait for current stream to complete
        // (it's the oldest in the ring buffer)
        if (m_batchCount >= c_numStreams) {
            cudaError_t err = cudaStreamSynchronize(m_streams[streamIdx]);
            if (err != cudaSuccess) {
                Log::error(getName() + ": Stream sync failed: " + cudaGetErrorString(err));

                // Track errors and attempt recovery if needed
                if (recordError()) {
                    Log::warning(getName() + ": Attempting recovery...");
                    freeBuffers();
                    if (!init()) {
                        Log::error(getName() + ": Recovery failed, stopping");
                        m_running = false;
                        break;
                    }
                    Log::info(getName() + ": Recovery successful");
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                m_batchCount = 0;
                continue;
            }

            // Clear error counter on successful operation
            clearErrors();

            // Process results from this stream (oldest batch)
            processSolutions(streamIdx, m_batchNonce[streamIdx]);

            // Update hash count
            updateHashCount(batchSize);
        }

        // Launch new batch on this stream
        if (!launchBatch(nonce, streamIdx)) {
            // Kernel launch failed - track error and attempt recovery if needed
            if (recordError()) {
                Log::warning(getName() + ": Attempting recovery after launch failure...");
                freeBuffers();
                if (!init()) {
                    Log::error(getName() + ": Recovery failed, stopping");
                    m_running = false;
                    break;
                }
                Log::info(getName() + ": Recovery successful");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            m_batchCount = 0;
            continue;
        }
        m_batchNonce[streamIdx] = nonce;

        // Advance to next stream and nonce
        m_currentStream = (m_currentStream + 1) % c_numStreams;
        nonce += batchSize;
        m_batchCount++;
    }

    // Drain remaining batches on exit
    for (unsigned i = 0; i < c_numStreams; i++) {
        cudaStreamSynchronize(m_streams[i]);
    }
}

bool CUDAMiner::launchBatch(uint64_t startNonce, unsigned streamIdx) {
    cudaError_t err;

    // Clear output buffer (async)
    err = cudaMemsetAsync(d_output[streamIdx], 0, sizeof(uint32_t), m_streams[streamIdx]);
    if (err != cudaSuccess) {
        Log::error(getName() + ": cudaMemsetAsync failed: " + cudaGetErrorString(err));
        return false;
    }

    // Launch kernel
    toshash_search<<<m_gridSize, m_blockSize, 0, m_streams[streamIdx]>>>(d_output[streamIdx], startNonce);

    // Check for kernel launch errors
    err = cudaGetLastError();
    if (err != cudaSuccess) {
        Log::error(getName() + ": Kernel launch failed: " + cudaGetErrorString(err));
        return false;
    }

    // Async copy results back to pinned host memory
    err = cudaMemcpyAsync(m_output[streamIdx], d_output[streamIdx],
                          OUTPUT_SIZE, cudaMemcpyDeviceToHost, m_streams[streamIdx]);
    if (err != cudaSuccess) {
        Log::error(getName() + ": cudaMemcpyAsync failed: " + cudaGetErrorString(err));
        return false;
    }

    return true;
}

uint32_t CUDAMiner::readResults(unsigned streamIdx) {
    return m_output[streamIdx][0];  // Solution count
}

void CUDAMiner::processSolutions(unsigned streamIdx, uint64_t startNonce) {
    uint32_t solutionCount = readResults(streamIdx);

    // Bounds check - cap solution count to prevent buffer overflow
    if (solutionCount > MAX_OUTPUTS) {
        Log::warning(getName() + ": GPU returned invalid solution count " +
                    std::to_string(solutionCount) + ", capping to " + std::to_string(MAX_OUTPUTS));
        solutionCount = MAX_OUTPUTS;
    }

    if (solutionCount > 0) {
        for (uint32_t i = 0; i < solutionCount; i++) {
            uint64_t solNonce = m_output[streamIdx][1 + i * 2] |
                               (static_cast<uint64_t>(m_output[streamIdx][1 + i * 2 + 1]) << 32);

            // Basic sanity check on nonce value
            if (solNonce == 0 || solNonce == UINT64_MAX) {
                Log::warning(getName() + ": Suspicious nonce value " + std::to_string(solNonce) + ", skipping");
                continue;
            }

            // Verify on CPU before submitting
            verifySolution(solNonce);
        }
    }
}

std::vector<DeviceDescriptor> CUDAMiner::enumDevices() {
    std::vector<DeviceDescriptor> devices;

    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);

    if (err != cudaSuccess || deviceCount == 0) {
        return devices;
    }

    for (int i = 0; i < deviceCount; i++) {
        cudaDeviceProp props;
        err = cudaGetDeviceProperties(&props, i);

        if (err != cudaSuccess) {
            continue;
        }

        DeviceDescriptor desc;
        desc.type = MinerType::CUDA;
        desc.index = static_cast<unsigned>(i);
        desc.name = props.name;
        desc.totalMemory = props.totalGlobalMem;
        desc.computeUnits = props.multiProcessorCount;
        desc.cudaDeviceIndex = i;
        desc.cudaComputeCapabilityMajor = props.major;
        desc.cudaComputeCapabilityMinor = props.minor;

        devices.push_back(desc);
    }

    return devices;
}

}  // namespace tos

#endif  // WITH_CUDA
