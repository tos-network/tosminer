/**
 * TOS Miner - OpenCL Miner Implementation
 */

#ifdef WITH_OPENCL

#include "CLMiner.h"
#include "core/WorkPackage.h"
#include "util/Log.h"
#include "toshash_kernel.cl.h"
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cctype>

namespace tos {

// Static members
unsigned CLMiner::s_globalWorkSizeMultiplier = 16384;  // 16K work items by default
unsigned CLMiner::s_localWorkSize = 1;  // 1 work item per workgroup (uses 64KB local memory)

CLMiner::CLMiner(unsigned index, const DeviceDescriptor& device)
    : Miner(index, device)
    , m_globalWorkSize(0)
    , m_localWorkSize(1)
{
}

CLMiner::~CLMiner() {
    stop();
}

bool CLMiner::init() {
    try {
        // Get all platforms
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);

        if (m_device.clPlatformIndex >= platforms.size()) {
            Log::error(getName() + ": Invalid platform index");
            return false;
        }

        cl::Platform& platform = platforms[m_device.clPlatformIndex];

        // Get devices on this platform
        std::vector<cl::Device> devices;
        platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);

        if (m_device.clDeviceIndex >= devices.size()) {
            Log::error(getName() + ": Invalid device index");
            return false;
        }

        cl::Device& device = devices[m_device.clDeviceIndex];

        // Create context and queue
        m_context = cl::Context(device);
        m_queue = cl::CommandQueue(m_context, device);

        // Get device properties
        size_t maxWorkGroupSize = device.getInfo<CL_DEVICE_MAX_WORK_GROUP_SIZE>();
        size_t localMemSize = device.getInfo<CL_DEVICE_LOCAL_MEM_SIZE>();

        std::ostringstream ss;
        ss << getName() << ": " << device.getInfo<CL_DEVICE_NAME>()
           << " (local mem: " << (localMemSize / 1024) << "KB"
           << ", max workgroup: " << maxWorkGroupSize << ")";
        Log::info(ss.str());

        // Check local memory is sufficient for scratchpad (64KB)
        if (localMemSize < 65536) {
            Log::warning(getName() + ": Insufficient local memory, falling back to global memory");
            // Could implement a fallback kernel here
        }

        // Compile kernel
        if (!compileKernel()) {
            return false;
        }

        // Allocate buffers
        if (!allocateBuffers()) {
            return false;
        }

        // Set work sizes
        // Each work item needs full local memory (64KB), so local size = 1
        m_localWorkSize = s_localWorkSize;
        m_globalWorkSize = s_globalWorkSizeMultiplier;

        Log::info(getName() + ": Initialized (global work size: " +
                  std::to_string(m_globalWorkSize) + ")");

        return true;

    } catch (const cl::Error& e) {
        std::ostringstream ss;
        ss << getName() << ": OpenCL error: " << e.what() << " (" << e.err() << ")";
        Log::error(ss.str());
        return false;
    }
}

std::string CLMiner::getName() const {
    return "CL" + std::to_string(m_index);
}

bool CLMiner::compileKernel() {
    try {
        // Get kernel source from embedded header
        std::string source(reinterpret_cast<const char*>(toshash_cl_source), toshash_cl_source_len);

        // Build program with platform-specific options
        m_program = cl::Program(m_context, source);

        // Detect platform for optimization
        std::string buildOptions = "-cl-std=CL1.2";

        std::string platformName = m_device.clPlatformName;
        std::transform(platformName.begin(), platformName.end(), platformName.begin(), ::tolower);

        if (platformName.find("nvidia") != std::string::npos) {
            // NVIDIA optimizations
            buildOptions += " -DPLATFORM_NVIDIA";
            // Limit register usage for better occupancy
            buildOptions += " -cl-nv-maxrregcount=64";
            Log::info(getName() + ": Using NVIDIA optimizations");
        } else if (platformName.find("amd") != std::string::npos ||
                   platformName.find("advanced micro") != std::string::npos) {
            // AMD optimizations - enable media ops for fast rotations
            buildOptions += " -DPLATFORM_AMD";
            Log::info(getName() + ": Using AMD optimizations");
        } else if (platformName.find("intel") != std::string::npos) {
            // Intel optimizations
            buildOptions += " -DPLATFORM_INTEL";
            Log::info(getName() + ": Using Intel optimizations");
        }

        try {
            m_program.build(buildOptions);
        } catch (const cl::Error&) {
            // Get build log
            auto devices = m_context.getInfo<CL_CONTEXT_DEVICES>();
            std::string buildLog = m_program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(devices[0]);
            Log::error(getName() + ": Kernel build failed:\n" + buildLog);
            return false;
        }

        // Create kernels
        m_searchKernel = cl::Kernel(m_program, "toshash_search");
        m_benchmarkKernel = cl::Kernel(m_program, "toshash_benchmark");

        Log::info(getName() + ": Kernel compiled successfully");
        return true;

    } catch (const cl::Error& e) {
        std::ostringstream ss;
        ss << getName() << ": Kernel compilation error: " << e.what() << " (" << e.err() << ")";
        Log::error(ss.str());
        return false;
    }
}

bool CLMiner::allocateBuffers() {
    try {
        // Output buffers: [count] + [nonce_lo, nonce_hi] * MAX_OUTPUTS (double buffered)
        size_t outputSize = (1 + MAX_OUTPUTS * 2) * sizeof(uint32_t);
        for (unsigned i = 0; i < c_bufferCount; i++) {
            m_outputBuffer[i] = cl::Buffer(m_context, CL_MEM_READ_WRITE, outputSize);
            m_output[i].resize(1 + MAX_OUTPUTS * 2);
        }

        // Header buffer (constant)
        m_headerBuffer = cl::Buffer(m_context, CL_MEM_READ_ONLY, INPUT_SIZE);

        // Target buffer (constant)
        m_targetBuffer = cl::Buffer(m_context, CL_MEM_READ_ONLY, HASH_SIZE);

        Log::info(getName() + ": Buffers allocated (double buffered)");
        return true;

    } catch (const cl::Error& e) {
        std::ostringstream ss;
        ss << getName() << ": Buffer allocation error: " << e.what() << " (" << e.err() << ")";
        Log::error(ss.str());
        return false;
    }
}

void CLMiner::mineLoop() {
    uint64_t nonce = 0;
    uint64_t batchSize = m_globalWorkSize;
    m_bufferIndex = 0;

    // Clear pending queue
    while (!m_pending.empty()) {
        m_pending.pop();
    }

    while (m_running) {
        // Check for pause
        if (m_paused) {
            // Drain pending batches before pausing
            while (!m_pending.empty()) {
                auto& batch = m_pending.front();
                batch.event.wait();
                readBatchResults(batch.bufferIndex);
                m_pending.pop();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

            // Drain pending batches (old work) before switching
            while (!m_pending.empty()) {
                auto& batch = m_pending.front();
                batch.event.wait();
                // Don't process solutions from old work
                m_pending.pop();
            }

            // Upload new header and target
            try {
                m_queue.enqueueWriteBuffer(m_headerBuffer, CL_TRUE, 0, INPUT_SIZE, work.header.data());
                m_queue.enqueueWriteBuffer(m_targetBuffer, CL_TRUE, 0, HASH_SIZE, work.target.data());

                // Get device-specific starting nonce (non-overlapping range)
                nonce = work.getDeviceStartNonce(m_index);
                m_bufferIndex = 0;

            } catch (const cl::Error& e) {
                Log::error(getName() + ": Failed to upload work: " + std::string(e.what()));
                continue;
            }
        }

        try {
            // Double buffering async pipeline:
            // 1. If pending queue is not full, enqueue new batch
            // 2. If pending queue is full, wait for oldest and process results

            // Enqueue new batches while we have buffer space
            while (m_pending.size() < c_bufferCount) {
                PendingBatch batch;
                batch.startNonce = nonce;
                batch.bufferIndex = m_bufferIndex;

                // Enqueue batch and capture completion event
                enqueueBatch(nonce, m_bufferIndex, batch.event);
                m_pending.push(batch);

                // Advance to next buffer and nonce
                m_bufferIndex = (m_bufferIndex + 1) % c_bufferCount;
                nonce += batchSize;
            }

            // Process oldest completed batch
            if (!m_pending.empty()) {
                auto& oldest = m_pending.front();

                // Wait ONLY for this specific batch's event (not queue.finish!)
                // This allows the next batch to continue executing while we process
                oldest.event.wait();

                // Read and process results
                processSolutions(oldest.bufferIndex, oldest.startNonce);

                // Update hash count
                updateHashCount(batchSize);

                m_pending.pop();
            }

        } catch (const cl::Error& e) {
            Log::error(getName() + ": Mining error: " + std::string(e.what()));
            // Clear pending queue on error
            while (!m_pending.empty()) {
                m_pending.pop();
            }

            // Track errors and attempt recovery if needed
            if (recordError()) {
                Log::warning(getName() + ": Attempting recovery...");
                // Try to reinitialize
                try {
                    if (!compileKernel() || !allocateBuffers()) {
                        Log::error(getName() + ": Recovery failed, stopping");
                        m_running = false;
                        break;
                    }
                    Log::info(getName() + ": Recovery successful");
                } catch (...) {
                    Log::error(getName() + ": Recovery failed with exception");
                    m_running = false;
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Clear error counter on successful batch
        clearErrors();
    }

    // Drain pending batches on exit
    while (!m_pending.empty()) {
        try {
            m_queue.finish();
        } catch (...) {}
        m_pending.pop();
    }
}

void CLMiner::enqueueBatch(uint64_t startNonce, unsigned bufferIndex, cl::Event& completionEvent) {
    // Clear output buffer (async)
    uint32_t zero = 0;
    m_queue.enqueueWriteBuffer(m_outputBuffer[bufferIndex], CL_FALSE, 0, sizeof(uint32_t), &zero);

    // Set kernel arguments
    m_searchKernel.setArg(0, m_outputBuffer[bufferIndex]);
    m_searchKernel.setArg(1, m_headerBuffer);
    m_searchKernel.setArg(2, m_targetBuffer);
    m_searchKernel.setArg(3, startNonce);
    m_searchKernel.setArg(4, MAX_OUTPUTS);

    // Execute kernel (async)
    cl::Event kernelEvent;
    m_queue.enqueueNDRangeKernel(
        m_searchKernel,
        cl::NullRange,
        cl::NDRange(m_globalWorkSize),
        cl::NDRange(m_localWorkSize),
        nullptr,
        &kernelEvent
    );

    // Enqueue async read of results (depends on kernel completion)
    // The event returned here is what we wait on to know results are ready
    std::vector<cl::Event> waitList = {kernelEvent};
    m_queue.enqueueReadBuffer(m_outputBuffer[bufferIndex], CL_FALSE, 0,
                              m_output[bufferIndex].size() * sizeof(uint32_t),
                              m_output[bufferIndex].data(),
                              &waitList,
                              &completionEvent);
}

uint32_t CLMiner::readBatchResults(unsigned bufferIndex) {
    return m_output[bufferIndex][0];  // Solution count
}

void CLMiner::processSolutions(unsigned bufferIndex, uint64_t startNonce) {
    uint32_t solutionCount = readBatchResults(bufferIndex);

    if (solutionCount > 0) {
        for (uint32_t i = 0; i < solutionCount && i < MAX_OUTPUTS; i++) {
            uint64_t solNonce = m_output[bufferIndex][1 + i * 2] |
                               (static_cast<uint64_t>(m_output[bufferIndex][1 + i * 2 + 1]) << 32);

            // Verify on CPU before submitting
            verifySolution(solNonce);
        }
    }
}

std::vector<DeviceDescriptor> CLMiner::enumDevices() {
    std::vector<DeviceDescriptor> devices;

    try {
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);

        unsigned deviceIndex = 0;

        for (unsigned p = 0; p < platforms.size(); p++) {
            std::vector<cl::Device> platformDevices;
            try {
                platforms[p].getDevices(CL_DEVICE_TYPE_GPU, &platformDevices);
            } catch (const cl::Error&) {
                continue;  // No GPUs on this platform
            }

            std::string platformName = platforms[p].getInfo<CL_PLATFORM_NAME>();

            for (unsigned d = 0; d < platformDevices.size(); d++) {
                DeviceDescriptor desc;
                desc.type = MinerType::OpenCL;
                desc.index = deviceIndex++;
                desc.name = platformDevices[d].getInfo<CL_DEVICE_NAME>();
                desc.totalMemory = platformDevices[d].getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
                desc.computeUnits = platformDevices[d].getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>();
                desc.clPlatformName = platformName;
                desc.clPlatformIndex = p;
                desc.clDeviceIndex = d;

                devices.push_back(desc);
            }
        }
    } catch (const cl::Error& e) {
        Log::warning("OpenCL enumeration error: " + std::string(e.what()));
    }

    return devices;
}

}  // namespace tos

#endif  // WITH_OPENCL
