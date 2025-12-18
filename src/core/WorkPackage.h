/**
 * TOS Miner - Work Package
 *
 * Represents a mining job received from pool or node
 */

#pragma once

#include "Types.h"
#include <cstring>
#include <cstdio>
#include <string>
#include <chrono>

namespace tos {

// TOS Hash V3 constants
constexpr size_t INPUT_SIZE = 112;      // Block header size
constexpr size_t HASH_SIZE = 32;        // Output hash size
constexpr size_t SCRATCHPAD_SIZE = 8192; // 64KB / 8 bytes

/**
 * Work package structure
 *
 * Contains all data needed for mining:
 * - Block header (without nonce)
 * - Target difficulty
 * - Job identification
 */
struct WorkPackage {
    // Job identifier from pool
    std::string jobId;

    // Block header data (104 bytes header + 8 bytes nonce space = 112 bytes)
    std::array<uint8_t, INPUT_SIZE> header;

    // Difficulty target (hash must be less than this)
    Hash256 target;

    // Block height (for logging)
    uint64_t height;

    // Starting nonce base (includes extranonce1 from pool)
    Nonce startNonce;

    // Extranonce1 from pool (hex string for submission)
    std::string extraNonce1;

    // Extranonce2 size in bytes (from pool)
    unsigned extraNonce2Size;

    // Total number of mining devices (for nonce partitioning)
    unsigned totalDevices;

    // Epoch/seed hash (for compatibility, not used in V3)
    Hash256 seedHash;

    // Header hash (Blake3 of header without nonce)
    Hash256 headerHash;

    // Is this work package valid?
    bool valid;

    // Timestamp when this work was received
    std::chrono::steady_clock::time_point receivedTime;

    WorkPackage()
        : jobId("")
        , header{}
        , target{}
        , height(0)
        , startNonce(0)
        , extraNonce1("")
        , extraNonce2Size(4)
        , totalDevices(1)
        , seedHash{}
        , headerHash{}
        , valid(false)
        , receivedTime(std::chrono::steady_clock::now())
    {}

    // Check if work package is valid
    explicit operator bool() const { return valid; }

    // Reset the work package
    void reset() {
        jobId.clear();
        header.fill(0);
        target.fill(0);
        height = 0;
        startNonce = 0;
        extraNonce1.clear();
        extraNonce2Size = 4;
        totalDevices = 1;
        seedHash.fill(0);
        headerHash.fill(0);
        valid = false;
        receivedTime = std::chrono::steady_clock::now();
    }

    // Get age of this work package in seconds
    unsigned getAgeSeconds() const {
        auto now = std::chrono::steady_clock::now();
        return static_cast<unsigned>(
            std::chrono::duration_cast<std::chrono::seconds>(now - receivedTime).count()
        );
    }

    // Check if work is stale (older than given threshold in seconds)
    bool isStale(unsigned thresholdSeconds = 60) const {
        return getAgeSeconds() > thresholdSeconds;
    }

    // Maximum number of devices to prevent nonce space becoming too small
    static constexpr unsigned MAX_DEVICES = 256;

    // Get starting nonce for a specific device
    // Divides the nonce space evenly among all devices
    // Each device gets: (2^64 / totalDevices) nonces starting at their offset
    Nonce getDeviceStartNonce(unsigned deviceIndex) const {
        if (totalDevices <= 1) {
            return startNonce;
        }

        // Clamp totalDevices to prevent nonce space becoming too small
        unsigned clampedDevices = (totalDevices > MAX_DEVICES) ? MAX_DEVICES : totalDevices;

        // Calculate the size of each device's nonce space
        // Using the extranonce2 space, each device gets a portion
        uint64_t spacePerDevice = UINT64_MAX / clampedDevices;

        // Clamp deviceIndex to valid range
        unsigned clampedIndex = (deviceIndex >= clampedDevices) ? (clampedDevices - 1) : deviceIndex;

        // Device's base offset
        uint64_t deviceOffset = spacePerDevice * clampedIndex;

        // Check for wrap: if startNonce + deviceOffset would overflow, clamp to max
        if (startNonce > UINT64_MAX - deviceOffset) {
            // Would wrap, return maximum safe value
            return UINT64_MAX - spacePerDevice + 1;
        }

        // Combine with startNonce (which may include extranonce1)
        return startNonce + deviceOffset;
    }

    // Get the extranonce2 value for this device
    // This is what gets sent to the pool along with the nonce
    uint64_t getExtranonce2(unsigned deviceIndex) const {
        if (totalDevices <= 1) {
            return 0;
        }

        // Clamp totalDevices and deviceIndex consistently
        unsigned clampedDevices = (totalDevices > MAX_DEVICES) ? MAX_DEVICES : totalDevices;
        unsigned clampedIndex = (deviceIndex >= clampedDevices) ? (clampedDevices - 1) : deviceIndex;

        // Each device has a unique extranonce2 prefix based on its index
        // The remaining bits are used for incrementing nonce within device
        uint64_t spacePerDevice = UINT64_MAX / clampedDevices;
        return spacePerDevice * clampedIndex;
    }

    // Get extranonce2 as hex string (for Stratum submission)
    std::string getExtranonce2Hex(unsigned deviceIndex, uint64_t nonceOffset) const {
        uint64_t en2 = getExtranonce2(deviceIndex) + nonceOffset;

        // Format as hex string with proper size (extraNonce2Size bytes)
        std::string hex;
        hex.reserve(extraNonce2Size * 2);
        for (unsigned i = 0; i < extraNonce2Size; i++) {
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x", static_cast<uint8_t>((en2 >> (i * 8)) & 0xFF));
            hex += buf;
        }
        return hex;
    }

    // Set header data
    void setHeader(const uint8_t* data, size_t len) {
        if (len > INPUT_SIZE) len = INPUT_SIZE;
        std::memcpy(header.data(), data, len);
        if (len < INPUT_SIZE) {
            std::memset(header.data() + len, 0, INPUT_SIZE - len);
        }
    }

    // Set nonce in header (last 8 bytes)
    void setNonce(Nonce nonce) {
        // Nonce goes in last 8 bytes of header (little-endian)
        for (int i = 0; i < 8; ++i) {
            header[INPUT_SIZE - 8 + i] = static_cast<uint8_t>(nonce >> (i * 8));
        }
    }

    // Get nonce from header
    Nonce getNonce() const {
        Nonce nonce = 0;
        for (int i = 0; i < 8; ++i) {
            nonce |= static_cast<Nonce>(header[INPUT_SIZE - 8 + i]) << (i * 8);
        }
        return nonce;
    }

    // Set target from compact difficulty
    void setTarget(uint64_t difficulty) {
        // Convert difficulty to 256-bit target
        // target = 2^256 / difficulty (simplified)
        target.fill(0);
        if (difficulty == 0) {
            target.fill(0xff);
            return;
        }

        // For simplicity, store difficulty in first 8 bytes
        // Real implementation would compute proper target
        uint64_t maxTarget = 0xFFFFFFFFFFFFFFFFULL / difficulty;
        for (int i = 0; i < 8; ++i) {
            target[i] = static_cast<uint8_t>(maxTarget >> ((7 - i) * 8));
        }
    }
};

}  // namespace tos
