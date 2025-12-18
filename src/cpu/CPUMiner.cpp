/**
 * TOS Miner - CPU Miner Implementation
 */

#include "CPUMiner.h"
#include "util/Log.h"
#include <sstream>
#include <chrono>

namespace tos {

// Static members
unsigned CPUMiner::s_threadCount = 0;  // 0 = auto-detect

CPUMiner::CPUMiner(unsigned index, const DeviceDescriptor& device)
    : Miner(index, device)
    , m_scratch{}
{
}

CPUMiner::~CPUMiner() {
    stop();
}

bool CPUMiner::init() {
    Log::info(getName() + ": Initialized CPU miner");
    return true;
}

std::string CPUMiner::getName() const {
    return "CPU" + std::to_string(m_index);
}

std::vector<DeviceDescriptor> CPUMiner::enumDevices() {
    std::vector<DeviceDescriptor> devices;

    // Get thread count
    unsigned threads = s_threadCount;
    if (threads == 0) {
        threads = std::thread::hardware_concurrency();
        if (threads == 0) {
            threads = 1;  // Fallback to 1 thread
        }
    }

    // Create one device per thread
    for (unsigned i = 0; i < threads; i++) {
        DeviceDescriptor desc;
        desc.type = MinerType::CPU;
        desc.index = i;
        desc.name = "CPU Thread " + std::to_string(i);
        desc.totalMemory = 0;  // Not applicable for CPU
        desc.computeUnits = 1;

        devices.push_back(desc);
    }

    return devices;
}

void CPUMiner::mineLoop() {
    uint64_t nonce = 0;
    uint64_t hashCount = 0;

    while (m_running) {
        // Check for pause
        if (m_paused) {
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

            // Get device-specific starting nonce (non-overlapping range)
            nonce = work.getDeviceStartNonce(m_index);

            // Clear submitted nonces for new job
            clearSubmittedNonces();
        }

        // Get current work
        WorkPackage work = getWork();
        if (!work.valid) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Mine a batch of nonces
        for (uint64_t i = 0; i < BATCH_SIZE && m_running && !hasNewWork(); i++, nonce++) {
            // Compute hash and check against target
            Solution sol = m_hasher.search(work, nonce, m_scratch);

            if (sol.nonce != 0) {
                // Found a solution! Verify and submit
                Log::info(getName() + ": Found solution at nonce " + std::to_string(sol.nonce));

                // Set device index
                sol.deviceIndex = m_index;

                // Verify on CPU (double-check) and submit
                if (verifySolution(sol.nonce)) {
                    Log::info(getName() + ": Solution verified and submitted");
                }
            }
        }

        // Update hash count
        hashCount += BATCH_SIZE;
        updateHashCount(BATCH_SIZE);
    }
}

}  // namespace tos
