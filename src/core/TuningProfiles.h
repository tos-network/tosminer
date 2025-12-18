/**
 * TOS Miner - GPU Tuning Profiles
 *
 * Preset configurations for different GPU architectures
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <iostream>

namespace tos {

/**
 * GPU tuning profile
 */
struct TuningProfile {
    std::string name;
    std::string description;

    // OpenCL parameters
    unsigned openclGlobalWorkSize{16384};
    unsigned openclLocalWorkSize{1};

    // CUDA parameters
    unsigned cudaGridSize{16384};
    unsigned cudaBlockSize{1};
    unsigned cudaStreams{2};

    TuningProfile() = default;
    TuningProfile(const std::string& n, const std::string& desc,
                  unsigned oclGlobal, unsigned oclLocal,
                  unsigned cuGrid, unsigned cuBlock, unsigned cuStreams = 2)
        : name(n), description(desc)
        , openclGlobalWorkSize(oclGlobal), openclLocalWorkSize(oclLocal)
        , cudaGridSize(cuGrid), cudaBlockSize(cuBlock), cudaStreams(cuStreams)
    {}
};

/**
 * Predefined tuning profiles for common GPU architectures
 */
class TuningProfiles {
public:
    // Get profile by name
    static const TuningProfile& getProfile(const std::string& name) {
        auto it = profiles().find(name);
        if (it != profiles().end()) {
            return it->second;
        }
        return profiles().at("default");
    }

    // Get all available profile names
    static std::vector<std::string> getProfileNames() {
        std::vector<std::string> names;
        for (const auto& p : profiles()) {
            names.push_back(p.first);
        }
        return names;
    }

    // Check if profile exists
    static bool hasProfile(const std::string& name) {
        return profiles().find(name) != profiles().end();
    }

    // Print all profiles
    static void printProfiles() {
        for (const auto& [name, profile] : profiles()) {
            std::cout << "  " << name << ": " << profile.description << "\n";
        }
    }

private:
    static const std::map<std::string, TuningProfile>& profiles() {
        static const std::map<std::string, TuningProfile> s_profiles = {
            // Default balanced profile
            {"default", TuningProfile(
                "default", "Balanced settings for most GPUs",
                16384, 1,   // OpenCL
                16384, 1, 2 // CUDA
            )},

            // NVIDIA profiles
            {"nvidia-pascal", TuningProfile(
                "nvidia-pascal", "NVIDIA Pascal (GTX 10xx)",
                32768, 1,
                32768, 128, 2
            )},
            {"nvidia-turing", TuningProfile(
                "nvidia-turing", "NVIDIA Turing (RTX 20xx, GTX 16xx)",
                65536, 1,
                65536, 256, 4
            )},
            {"nvidia-ampere", TuningProfile(
                "nvidia-ampere", "NVIDIA Ampere (RTX 30xx)",
                131072, 1,
                131072, 256, 4
            )},
            {"nvidia-ada", TuningProfile(
                "nvidia-ada", "NVIDIA Ada Lovelace (RTX 40xx)",
                262144, 1,
                262144, 512, 4
            )},

            // AMD profiles
            {"amd-polaris", TuningProfile(
                "amd-polaris", "AMD Polaris (RX 4xx, RX 5xx)",
                16384, 64,
                16384, 1, 2
            )},
            {"amd-vega", TuningProfile(
                "amd-vega", "AMD Vega (Vega 56/64, VII)",
                32768, 64,
                32768, 1, 2
            )},
            {"amd-navi", TuningProfile(
                "amd-navi", "AMD RDNA (RX 5xxx)",
                65536, 64,
                65536, 1, 2
            )},
            {"amd-rdna2", TuningProfile(
                "amd-rdna2", "AMD RDNA2 (RX 6xxx)",
                131072, 64,
                131072, 1, 2
            )},
            {"amd-rdna3", TuningProfile(
                "amd-rdna3", "AMD RDNA3 (RX 7xxx)",
                262144, 64,
                262144, 1, 2
            )},

            // Intel profiles
            {"intel-arc", TuningProfile(
                "intel-arc", "Intel Arc (A7xx)",
                32768, 32,
                32768, 1, 2
            )},

            // Low-end / power-saving
            {"low-power", TuningProfile(
                "low-power", "Low power consumption, reduced performance",
                8192, 1,
                8192, 64, 1
            )},

            // Maximum throughput (may increase power)
            {"max-throughput", TuningProfile(
                "max-throughput", "Maximum throughput, high power consumption",
                524288, 1,
                524288, 512, 4
            )}
        };
        return s_profiles;
    }
};

}  // namespace tos
