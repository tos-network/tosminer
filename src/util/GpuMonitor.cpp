/**
 * TOS Miner - GPU Monitoring Implementation
 */

#include "GpuMonitor.h"
#include "Log.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

#ifdef WITH_CUDA
#include <cuda_runtime.h>
// NVML is dynamically loaded to avoid hard dependency
#ifdef __linux__
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
#endif

namespace tos {

// ============================================================================
// NVML Backend (NVIDIA)
// ============================================================================

#ifdef WITH_CUDA

// NVML types (copied from nvml.h to avoid dependency)
typedef void* nvmlDevice_t;
typedef int nvmlReturn_t;
typedef int nvmlTemperatureSensors_t;
typedef int nvmlClockType_t;
typedef int nvmlClockId_t;

struct nvmlUtilization_t {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

#define NVML_SUCCESS 0
#define NVML_TEMPERATURE_GPU 0
#define NVML_CLOCK_GRAPHICS 0
#define NVML_CLOCK_MEM 2

// Function pointer types
typedef nvmlReturn_t (*nvmlInit_t)(void);
typedef nvmlReturn_t (*nvmlShutdown_t)(void);
typedef nvmlReturn_t (*nvmlDeviceGetCount_t)(unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetHandleByIndex_t)(unsigned int, nvmlDevice_t*);
typedef nvmlReturn_t (*nvmlDeviceGetName_t)(nvmlDevice_t, char*, unsigned int);
typedef nvmlReturn_t (*nvmlDeviceGetTemperature_t)(nvmlDevice_t, nvmlTemperatureSensors_t, unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetPowerUsage_t)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetEnforcedPowerLimit_t)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetFanSpeed_t)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetMemoryInfo_t)(nvmlDevice_t, nvmlMemory_t*);
typedef nvmlReturn_t (*nvmlDeviceGetClockInfo_t)(nvmlDevice_t, nvmlClockType_t, unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetUtilizationRates_t)(nvmlDevice_t, nvmlUtilization_t*);

struct NvmlMonitor::Impl {
    void* libHandle{nullptr};
    bool initialized{false};
    unsigned int deviceCount{0};
    std::vector<nvmlDevice_t> devices;

    // Function pointers
    nvmlInit_t nvmlInit{nullptr};
    nvmlShutdown_t nvmlShutdown{nullptr};
    nvmlDeviceGetCount_t nvmlDeviceGetCount{nullptr};
    nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex{nullptr};
    nvmlDeviceGetName_t nvmlDeviceGetName{nullptr};
    nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature{nullptr};
    nvmlDeviceGetPowerUsage_t nvmlDeviceGetPowerUsage{nullptr};
    nvmlDeviceGetEnforcedPowerLimit_t nvmlDeviceGetEnforcedPowerLimit{nullptr};
    nvmlDeviceGetFanSpeed_t nvmlDeviceGetFanSpeed{nullptr};
    nvmlDeviceGetMemoryInfo_t nvmlDeviceGetMemoryInfo{nullptr};
    nvmlDeviceGetClockInfo_t nvmlDeviceGetClockInfo{nullptr};
    nvmlDeviceGetUtilizationRates_t nvmlDeviceGetUtilizationRates{nullptr};

    bool loadLibrary() {
#ifdef __linux__
        // Try multiple library names
        const char* libNames[] = {
            "libnvidia-ml.so.1",
            "libnvidia-ml.so",
            "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1",
            nullptr
        };

        for (int i = 0; libNames[i] != nullptr; i++) {
            libHandle = dlopen(libNames[i], RTLD_NOW);
            if (libHandle) break;
        }

        if (!libHandle) {
            return false;
        }

        // Load function pointers
        nvmlInit = (nvmlInit_t)dlsym(libHandle, "nvmlInit_v2");
        if (!nvmlInit) nvmlInit = (nvmlInit_t)dlsym(libHandle, "nvmlInit");
        nvmlShutdown = (nvmlShutdown_t)dlsym(libHandle, "nvmlShutdown");
        nvmlDeviceGetCount = (nvmlDeviceGetCount_t)dlsym(libHandle, "nvmlDeviceGetCount_v2");
        nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)dlsym(libHandle, "nvmlDeviceGetHandleByIndex_v2");
        nvmlDeviceGetName = (nvmlDeviceGetName_t)dlsym(libHandle, "nvmlDeviceGetName");
        nvmlDeviceGetTemperature = (nvmlDeviceGetTemperature_t)dlsym(libHandle, "nvmlDeviceGetTemperature");
        nvmlDeviceGetPowerUsage = (nvmlDeviceGetPowerUsage_t)dlsym(libHandle, "nvmlDeviceGetPowerUsage");
        nvmlDeviceGetEnforcedPowerLimit = (nvmlDeviceGetEnforcedPowerLimit_t)dlsym(libHandle, "nvmlDeviceGetEnforcedPowerLimit");
        nvmlDeviceGetFanSpeed = (nvmlDeviceGetFanSpeed_t)dlsym(libHandle, "nvmlDeviceGetFanSpeed");
        nvmlDeviceGetMemoryInfo = (nvmlDeviceGetMemoryInfo_t)dlsym(libHandle, "nvmlDeviceGetMemoryInfo");
        nvmlDeviceGetClockInfo = (nvmlDeviceGetClockInfo_t)dlsym(libHandle, "nvmlDeviceGetClockInfo");
        nvmlDeviceGetUtilizationRates = (nvmlDeviceGetUtilizationRates_t)dlsym(libHandle, "nvmlDeviceGetUtilizationRates");

#elif defined(_WIN32)
        // Windows NVML loading
        libHandle = LoadLibraryA("nvml.dll");
        if (!libHandle) {
            // Try NVIDIA default path
            libHandle = LoadLibraryA("C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
        }

        if (!libHandle) {
            return false;
        }

        nvmlInit = (nvmlInit_t)GetProcAddress((HMODULE)libHandle, "nvmlInit_v2");
        if (!nvmlInit) nvmlInit = (nvmlInit_t)GetProcAddress((HMODULE)libHandle, "nvmlInit");
        nvmlShutdown = (nvmlShutdown_t)GetProcAddress((HMODULE)libHandle, "nvmlShutdown");
        nvmlDeviceGetCount = (nvmlDeviceGetCount_t)GetProcAddress((HMODULE)libHandle, "nvmlDeviceGetCount_v2");
        nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress((HMODULE)libHandle, "nvmlDeviceGetHandleByIndex_v2");
        nvmlDeviceGetName = (nvmlDeviceGetName_t)GetProcAddress((HMODULE)libHandle, "nvmlDeviceGetName");
        nvmlDeviceGetTemperature = (nvmlDeviceGetTemperature_t)GetProcAddress((HMODULE)libHandle, "nvmlDeviceGetTemperature");
        nvmlDeviceGetPowerUsage = (nvmlDeviceGetPowerUsage_t)GetProcAddress((HMODULE)libHandle, "nvmlDeviceGetPowerUsage");
        nvmlDeviceGetEnforcedPowerLimit = (nvmlDeviceGetEnforcedPowerLimit_t)GetProcAddress((HMODULE)libHandle, "nvmlDeviceGetEnforcedPowerLimit");
        nvmlDeviceGetFanSpeed = (nvmlDeviceGetFanSpeed_t)GetProcAddress((HMODULE)libHandle, "nvmlDeviceGetFanSpeed");
        nvmlDeviceGetMemoryInfo = (nvmlDeviceGetMemoryInfo_t)GetProcAddress((HMODULE)libHandle, "nvmlDeviceGetMemoryInfo");
        nvmlDeviceGetClockInfo = (nvmlDeviceGetClockInfo_t)GetProcAddress((HMODULE)libHandle, "nvmlDeviceGetClockInfo");
        nvmlDeviceGetUtilizationRates = (nvmlDeviceGetUtilizationRates_t)GetProcAddress((HMODULE)libHandle, "nvmlDeviceGetUtilizationRates");
#elif defined(__APPLE__)
        // NVML not available on macOS
        return false;
#endif

        return nvmlInit && nvmlShutdown && nvmlDeviceGetCount && nvmlDeviceGetHandleByIndex;
    }

    void unloadLibrary() {
#ifdef __linux__
        if (libHandle) {
            dlclose(libHandle);
            libHandle = nullptr;
        }
#elif defined(_WIN32)
        if (libHandle) {
            FreeLibrary((HMODULE)libHandle);
            libHandle = nullptr;
        }
#endif
    }
};

NvmlMonitor::NvmlMonitor() : m_impl(std::make_unique<Impl>()) {}

NvmlMonitor::~NvmlMonitor() {
    shutdown();
}

bool NvmlMonitor::init() {
    if (m_impl->initialized) return true;

    if (!m_impl->loadLibrary()) {
        Log::debug("NVML library not found - NVIDIA monitoring disabled");
        return false;
    }

    if (m_impl->nvmlInit() != NVML_SUCCESS) {
        Log::debug("Failed to initialize NVML");
        m_impl->unloadLibrary();
        return false;
    }

    if (m_impl->nvmlDeviceGetCount(&m_impl->deviceCount) != NVML_SUCCESS) {
        m_impl->nvmlShutdown();
        m_impl->unloadLibrary();
        return false;
    }

    // Get device handles
    m_impl->devices.resize(m_impl->deviceCount);
    for (unsigned int i = 0; i < m_impl->deviceCount; i++) {
        if (m_impl->nvmlDeviceGetHandleByIndex(i, &m_impl->devices[i]) != NVML_SUCCESS) {
            m_impl->devices[i] = nullptr;
        }
    }

    m_impl->initialized = true;
    Log::info("NVML initialized: " + std::to_string(m_impl->deviceCount) + " NVIDIA GPU(s) found");
    return true;
}

void NvmlMonitor::shutdown() {
    if (!m_impl->initialized) return;

    if (m_impl->nvmlShutdown) {
        m_impl->nvmlShutdown();
    }

    m_impl->unloadLibrary();
    m_impl->initialized = false;
    m_impl->deviceCount = 0;
    m_impl->devices.clear();
}

bool NvmlMonitor::isAvailable() const {
    return m_impl->initialized;
}

int NvmlMonitor::getDeviceCount() const {
    return m_impl->initialized ? static_cast<int>(m_impl->deviceCount) : 0;
}

GpuStats NvmlMonitor::getStats(int deviceIndex) {
    GpuStats stats;
    stats.deviceIndex = deviceIndex;

    if (!m_impl->initialized || deviceIndex < 0 ||
        deviceIndex >= static_cast<int>(m_impl->deviceCount)) {
        return stats;
    }

    nvmlDevice_t device = m_impl->devices[deviceIndex];
    if (!device) return stats;

    // Name
    char name[64] = {0};
    if (m_impl->nvmlDeviceGetName && m_impl->nvmlDeviceGetName(device, name, sizeof(name)) == NVML_SUCCESS) {
        stats.name = name;
    }

    // Temperature
    unsigned int temp;
    if (m_impl->nvmlDeviceGetTemperature &&
        m_impl->nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS) {
        stats.temperature = static_cast<int>(temp);
    }

    // Power
    unsigned int power;
    if (m_impl->nvmlDeviceGetPowerUsage &&
        m_impl->nvmlDeviceGetPowerUsage(device, &power) == NVML_SUCCESS) {
        stats.powerUsage = static_cast<int>(power / 1000);  // Convert mW to W
    }

    if (m_impl->nvmlDeviceGetEnforcedPowerLimit &&
        m_impl->nvmlDeviceGetEnforcedPowerLimit(device, &power) == NVML_SUCCESS) {
        stats.powerLimit = static_cast<int>(power / 1000);  // Convert mW to W
    }

    // Fan
    unsigned int fan;
    if (m_impl->nvmlDeviceGetFanSpeed &&
        m_impl->nvmlDeviceGetFanSpeed(device, &fan) == NVML_SUCCESS) {
        stats.fanSpeed = static_cast<int>(fan);
    }

    // Memory
    nvmlMemory_t memory;
    if (m_impl->nvmlDeviceGetMemoryInfo &&
        m_impl->nvmlDeviceGetMemoryInfo(device, &memory) == NVML_SUCCESS) {
        stats.memoryTotal = memory.total;
        stats.memoryUsed = memory.used;
    }

    // Clocks
    unsigned int clock;
    if (m_impl->nvmlDeviceGetClockInfo) {
        if (m_impl->nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &clock) == NVML_SUCCESS) {
            stats.clockCore = static_cast<int>(clock);
        }
        if (m_impl->nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM, &clock) == NVML_SUCCESS) {
            stats.clockMemory = static_cast<int>(clock);
        }
    }

    // Utilization
    nvmlUtilization_t util;
    if (m_impl->nvmlDeviceGetUtilizationRates &&
        m_impl->nvmlDeviceGetUtilizationRates(device, &util) == NVML_SUCCESS) {
        stats.gpuUtilization = static_cast<int>(util.gpu);
        stats.memoryUtilization = static_cast<int>(util.memory);
    }

    stats.valid = true;
    return stats;
}

std::vector<GpuStats> NvmlMonitor::getAllStats() {
    std::vector<GpuStats> results;
    for (int i = 0; i < getDeviceCount(); i++) {
        results.push_back(getStats(i));
    }
    return results;
}

#endif  // WITH_CUDA

// ============================================================================
// AMD Monitor (via sysfs on Linux)
// ============================================================================

#ifdef WITH_OPENCL

struct AmdMonitor::Impl {
    bool initialized{false};
    std::vector<std::string> hwmonPaths;  // Paths to hwmon directories for each GPU

    // Read a value from sysfs file
    static int readSysfsInt(const std::string& path) {
        std::ifstream file(path);
        if (!file) return -1;
        int value;
        file >> value;
        return file.good() ? value : -1;
    }

    static std::string readSysfsString(const std::string& path) {
        std::ifstream file(path);
        if (!file) return "";
        std::string value;
        std::getline(file, value);
        return value;
    }

    std::vector<std::string> findAmdGpus() {
        std::vector<std::string> gpuPaths;

#ifdef __linux__
        // Scan /sys/class/drm for AMD GPUs
        const std::string drmPath = "/sys/class/drm";

        try {
            for (const auto& entry : std::filesystem::directory_iterator(drmPath)) {
                std::string name = entry.path().filename().string();
                // Look for cardN directories (not cardN-something)
                if (name.find("card") == 0 && name.find("-") == std::string::npos) {
                    std::string devicePath = entry.path().string() + "/device";
                    std::string vendorPath = devicePath + "/vendor";

                    std::string vendor = readSysfsString(vendorPath);
                    // AMD vendor ID is 0x1002
                    if (vendor.find("0x1002") != std::string::npos) {
                        // Find hwmon directory
                        std::string hwmonBase = devicePath + "/hwmon";
                        if (std::filesystem::exists(hwmonBase)) {
                            for (const auto& hwmon : std::filesystem::directory_iterator(hwmonBase)) {
                                gpuPaths.push_back(hwmon.path().string());
                                break;  // Use first hwmon
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            Log::debug("Error scanning for AMD GPUs: " + std::string(e.what()));
        }
#endif

        return gpuPaths;
    }
};

AmdMonitor::AmdMonitor() : m_impl(std::make_unique<Impl>()) {}

AmdMonitor::~AmdMonitor() {
    shutdown();
}

bool AmdMonitor::init() {
    if (m_impl->initialized) return true;

    m_impl->hwmonPaths = m_impl->findAmdGpus();

    if (m_impl->hwmonPaths.empty()) {
        Log::debug("No AMD GPUs found for monitoring");
        return false;
    }

    m_impl->initialized = true;
    Log::info("AMD GPU monitoring initialized: " + std::to_string(m_impl->hwmonPaths.size()) + " GPU(s) found");
    return true;
}

void AmdMonitor::shutdown() {
    m_impl->initialized = false;
    m_impl->hwmonPaths.clear();
}

bool AmdMonitor::isAvailable() const {
    return m_impl->initialized;
}

int AmdMonitor::getDeviceCount() const {
    return static_cast<int>(m_impl->hwmonPaths.size());
}

GpuStats AmdMonitor::getStats(int deviceIndex) {
    GpuStats stats;
    stats.deviceIndex = deviceIndex;

    if (!m_impl->initialized || deviceIndex < 0 ||
        deviceIndex >= static_cast<int>(m_impl->hwmonPaths.size())) {
        return stats;
    }

    const std::string& hwmon = m_impl->hwmonPaths[deviceIndex];

    // Name
    stats.name = Impl::readSysfsString(hwmon + "/name");
    if (stats.name.empty()) {
        stats.name = "AMD GPU " + std::to_string(deviceIndex);
    }

    // Temperature (stored in millidegrees)
    int temp = Impl::readSysfsInt(hwmon + "/temp1_input");
    if (temp > 0) {
        stats.temperature = temp / 1000;  // Convert to Celsius
    }

    // Edge temperature might be temp2
    temp = Impl::readSysfsInt(hwmon + "/temp2_input");
    if (temp > 0) {
        stats.temperatureHotspot = temp / 1000;
    }

    // Memory temperature might be temp3
    temp = Impl::readSysfsInt(hwmon + "/temp3_input");
    if (temp > 0) {
        stats.temperatureMemory = temp / 1000;
    }

    // Fan speed (PWM value 0-255, convert to percent)
    int pwm = Impl::readSysfsInt(hwmon + "/pwm1");
    if (pwm >= 0) {
        stats.fanSpeed = (pwm * 100) / 255;
    }

    // Power (stored in microwatts for some cards)
    int power = Impl::readSysfsInt(hwmon + "/power1_average");
    if (power > 0) {
        stats.powerUsage = power / 1000000;  // Convert uW to W
    }

    // GPU clock (stored in Hz or MHz depending on kernel)
    // Try pp_dpm_sclk first
    std::string dpmPath = hwmon + "/../pp_dpm_sclk";
    std::ifstream dpmFile(dpmPath);
    if (dpmFile) {
        std::string line;
        while (std::getline(dpmFile, line)) {
            if (line.find("*") != std::string::npos) {
                // Current frequency has asterisk
                size_t pos = line.find(":");
                if (pos != std::string::npos) {
                    stats.clockCore = std::stoi(line.substr(pos + 1));
                }
                break;
            }
        }
    }

    stats.valid = true;
    return stats;
}

std::vector<GpuStats> AmdMonitor::getAllStats() {
    std::vector<GpuStats> results;
    for (int i = 0; i < getDeviceCount(); i++) {
        results.push_back(getStats(i));
    }
    return results;
}

#endif  // WITH_OPENCL

// ============================================================================
// Unified GPU Monitor
// ============================================================================

struct GpuMonitor::Impl {
#ifdef WITH_CUDA
    std::unique_ptr<NvmlMonitor> nvml;
#endif
#ifdef WITH_OPENCL
    std::unique_ptr<AmdMonitor> amd;
#endif
    bool initialized{false};
};

GpuMonitor& GpuMonitor::instance() {
    static GpuMonitor instance;
    return instance;
}

GpuMonitor::GpuMonitor() : m_impl(std::make_unique<Impl>()) {}

GpuMonitor::~GpuMonitor() {
    shutdown();
}

bool GpuMonitor::init() {
    if (m_impl->initialized) return true;

    bool anyInitialized = false;

#ifdef WITH_CUDA
    m_impl->nvml = std::make_unique<NvmlMonitor>();
    if (m_impl->nvml->init()) {
        anyInitialized = true;
    }
#endif

#ifdef WITH_OPENCL
    m_impl->amd = std::make_unique<AmdMonitor>();
    if (m_impl->amd->init()) {
        anyInitialized = true;
    }
#endif

    m_impl->initialized = anyInitialized;
    return anyInitialized;
}

void GpuMonitor::shutdown() {
#ifdef WITH_CUDA
    if (m_impl->nvml) {
        m_impl->nvml->shutdown();
    }
#endif

#ifdef WITH_OPENCL
    if (m_impl->amd) {
        m_impl->amd->shutdown();
    }
#endif

    m_impl->initialized = false;
}

bool GpuMonitor::isAvailable() const {
    return m_impl->initialized;
}

GpuStats GpuMonitor::getNvidiaStats(int cudaIndex) {
#ifdef WITH_CUDA
    if (m_impl->nvml && m_impl->nvml->isAvailable()) {
        return m_impl->nvml->getStats(cudaIndex);
    }
#endif
    return GpuStats();
}

GpuStats GpuMonitor::getAmdStats(int clIndex) {
#ifdef WITH_OPENCL
    if (m_impl->amd && m_impl->amd->isAvailable()) {
        return m_impl->amd->getStats(clIndex);
    }
#endif
    return GpuStats();
}

std::vector<GpuStats> GpuMonitor::getAllStats() {
    std::vector<GpuStats> all;

#ifdef WITH_CUDA
    if (m_impl->nvml && m_impl->nvml->isAvailable()) {
        auto nvmlStats = m_impl->nvml->getAllStats();
        all.insert(all.end(), nvmlStats.begin(), nvmlStats.end());
    }
#endif

#ifdef WITH_OPENCL
    if (m_impl->amd && m_impl->amd->isAvailable()) {
        auto amdStats = m_impl->amd->getAllStats();
        all.insert(all.end(), amdStats.begin(), amdStats.end());
    }
#endif

    return all;
}

bool GpuMonitor::anyOverheating(int threshold) {
    auto allStats = getAllStats();
    return std::any_of(allStats.begin(), allStats.end(),
                       [threshold](const GpuStats& s) { return s.isOverheating(threshold); });
}

}  // namespace tos
