/**
 * TOS Miner - GPU Monitoring Interface
 *
 * Provides unified access to GPU temperature, power, and fan speed
 * through NVML (NVIDIA) and ADL (AMD) libraries.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace tos {

/**
 * GPU monitoring data for a single device
 */
struct GpuStats {
    int deviceIndex{-1};          // Device index
    std::string name;             // Device name

    // Temperature (Celsius)
    int temperature{-1};          // GPU core temperature
    int temperatureHotspot{-1};   // Hotspot/junction temperature (if available)
    int temperatureMemory{-1};    // Memory temperature (if available)

    // Power (Watts)
    int powerUsage{-1};           // Current power draw
    int powerLimit{-1};           // Power limit

    // Fan
    int fanSpeed{-1};             // Fan speed percentage (0-100)

    // Memory
    uint64_t memoryUsed{0};       // Used VRAM in bytes
    uint64_t memoryTotal{0};      // Total VRAM in bytes

    // Clock speeds (MHz)
    int clockCore{-1};            // GPU core clock
    int clockMemory{-1};          // Memory clock

    // Utilization (%)
    int gpuUtilization{-1};       // GPU utilization
    int memoryUtilization{-1};    // Memory utilization

    // PCIe
    int pcieTxThroughput{-1};     // TX throughput (KB/s)
    int pcieRxThroughput{-1};     // RX throughput (KB/s)

    // Status
    bool throttling{false};       // Is the GPU throttling?
    std::string throttleReason;   // Reason for throttling

    // Validity
    bool valid{false};            // Is this data valid?

    // Helper methods
    bool isOverheating(int threshold = 85) const {
        return temperature >= threshold;
    }

    double memoryUsagePercent() const {
        if (memoryTotal == 0) return 0;
        return static_cast<double>(memoryUsed) / memoryTotal * 100.0;
    }
};

/**
 * Abstract base class for GPU monitoring backends
 */
class GpuMonitorBackend {
public:
    virtual ~GpuMonitorBackend() = default;

    /**
     * Initialize the monitoring library
     * @return true if successful
     */
    virtual bool init() = 0;

    /**
     * Shutdown the monitoring library
     */
    virtual void shutdown() = 0;

    /**
     * Check if backend is available
     */
    virtual bool isAvailable() const = 0;

    /**
     * Get number of devices
     */
    virtual int getDeviceCount() const = 0;

    /**
     * Get stats for a specific device
     * @param deviceIndex Device index
     * @return GPU statistics
     */
    virtual GpuStats getStats(int deviceIndex) = 0;

    /**
     * Get stats for all devices
     */
    virtual std::vector<GpuStats> getAllStats() = 0;

    /**
     * Get backend name
     */
    virtual std::string getName() const = 0;
};

#ifdef WITH_CUDA
/**
 * NVIDIA NVML backend
 */
class NvmlMonitor : public GpuMonitorBackend {
public:
    NvmlMonitor();
    ~NvmlMonitor() override;

    bool init() override;
    void shutdown() override;
    bool isAvailable() const override;
    int getDeviceCount() const override;
    GpuStats getStats(int deviceIndex) override;
    std::vector<GpuStats> getAllStats() override;
    std::string getName() const override { return "NVML"; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
#endif

#ifdef WITH_OPENCL
/**
 * AMD ADL backend (for AMD GPUs via OpenCL)
 *
 * Note: ADL is more complex to implement due to various library versions.
 * This is a simplified implementation using sysfs on Linux.
 */
class AmdMonitor : public GpuMonitorBackend {
public:
    AmdMonitor();
    ~AmdMonitor() override;

    bool init() override;
    void shutdown() override;
    bool isAvailable() const override;
    int getDeviceCount() const override;
    GpuStats getStats(int deviceIndex) override;
    std::vector<GpuStats> getAllStats() override;
    std::string getName() const override { return "AMD"; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
#endif

/**
 * Unified GPU Monitor
 *
 * Automatically detects and uses appropriate backends.
 */
class GpuMonitor {
public:
    /**
     * Get singleton instance
     */
    static GpuMonitor& instance();

    /**
     * Initialize all available backends
     * @return true if at least one backend initialized
     */
    bool init();

    /**
     * Shutdown all backends
     */
    void shutdown();

    /**
     * Check if monitoring is available
     */
    bool isAvailable() const;

    /**
     * Get stats for NVIDIA device by CUDA index
     */
    GpuStats getNvidiaStats(int cudaIndex);

    /**
     * Get stats for AMD device by OpenCL index
     */
    GpuStats getAmdStats(int clIndex);

    /**
     * Get stats for all monitored devices
     */
    std::vector<GpuStats> getAllStats();

    /**
     * Check if any GPU is overheating
     */
    bool anyOverheating(int threshold = 85);

    // Prevent copying
    GpuMonitor(const GpuMonitor&) = delete;
    GpuMonitor& operator=(const GpuMonitor&) = delete;

private:
    GpuMonitor();
    ~GpuMonitor();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace tos
