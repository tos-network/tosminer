/**
 * TOS Miner - Core Types
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>

namespace tos {

// Hash types
using Hash256 = std::array<uint8_t, 32>;
using Hash512 = std::array<uint8_t, 64>;

// Nonce type
using Nonce = uint64_t;

// Convert hash to hex string
inline std::string toHex(const Hash256& hash) {
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (uint8_t b : hash) {
        result.push_back(hex[b >> 4]);
        result.push_back(hex[b & 0x0f]);
    }
    return result;
}

// Convert hex string to hash
inline Hash256 fromHex(const std::string& hex) {
    Hash256 result{};
    if (hex.length() != 64) return result;

    for (size_t i = 0; i < 32; ++i) {
        char c1 = hex[i * 2];
        char c2 = hex[i * 2 + 1];
        uint8_t v1 = (c1 >= 'a') ? (c1 - 'a' + 10) : ((c1 >= 'A') ? (c1 - 'A' + 10) : (c1 - '0'));
        uint8_t v2 = (c2 >= 'a') ? (c2 - 'a' + 10) : ((c2 >= 'A') ? (c2 - 'A' + 10) : (c2 - '0'));
        result[i] = (v1 << 4) | v2;
    }
    return result;
}

// Compare hash against target (hash < target means valid)
inline bool meetsTarget(const Hash256& hash, const Hash256& target) {
    for (int i = 0; i < 32; ++i) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true;  // Equal
}

// Miner type enumeration
enum class MinerType {
    CPU,
    OpenCL,
    CUDA,
    Mixed
};

// Solution structure
struct Solution {
    Nonce nonce;
    Hash256 hash;
    Hash256 mixHash;  // For compatibility, may not be used
    unsigned deviceIndex;  // Which device found this solution

    Solution() : nonce(0), hash{}, mixHash{}, deviceIndex(0) {}
    Solution(Nonce n, const Hash256& h, unsigned devIdx = 0)
        : nonce(n), hash(h), mixHash{}, deviceIndex(devIdx) {}
};

// Mining statistics snapshot (copyable)
struct MiningStatsSnapshot {
    uint64_t hashCount{0};
    uint64_t acceptedShares{0};
    uint64_t rejectedShares{0};
    uint64_t staleShares{0};

    double hashRate(double seconds) const {
        if (seconds <= 0) return 0;
        return static_cast<double>(hashCount) / seconds;
    }
};

// Mining statistics (thread-safe, non-copyable)
struct MiningStats {
    std::atomic<uint64_t> hashCount{0};
    std::atomic<uint64_t> acceptedShares{0};
    std::atomic<uint64_t> rejectedShares{0};
    std::atomic<uint64_t> staleShares{0};

    void reset() {
        hashCount = 0;
        acceptedShares = 0;
        rejectedShares = 0;
        staleShares = 0;
    }

    double hashRate(double seconds) const {
        if (seconds <= 0) return 0;
        return static_cast<double>(hashCount.load()) / seconds;
    }

    // Get a copyable snapshot of current stats
    MiningStatsSnapshot snapshot() const {
        MiningStatsSnapshot s;
        s.hashCount = hashCount.load();
        s.acceptedShares = acceptedShares.load();
        s.rejectedShares = rejectedShares.load();
        s.staleShares = staleShares.load();
        return s;
    }
};

// Mutex guard type
using Guard = std::lock_guard<std::mutex>;

}  // namespace tos
