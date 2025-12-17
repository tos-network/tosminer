/**
 * TOS Hash V3 - GPU/ASIC Friendly Algorithm
 *
 * C++ implementation for CPU mining and verification
 */

#pragma once

#include "core/Types.h"
#include "core/WorkPackage.h"
#include <array>
#include <cstdint>

namespace tos {

// TOS Hash V3 parameters
constexpr size_t TOSHASH_MEMORY_SIZE = 8192;      // 64KB scratchpad (8192 * 8 bytes)
constexpr size_t TOSHASH_MIXING_ROUNDS = 8;
constexpr size_t TOSHASH_MEMORY_PASSES = 4;
constexpr uint64_t TOSHASH_MIX_CONST = 0x517cc1b727220a95ULL;

// Scratchpad type
using ScratchPad = std::array<uint64_t, TOSHASH_MEMORY_SIZE>;

/**
 * TOS Hash V3 class
 *
 * Provides methods for:
 * - Computing hashes
 * - Verifying solutions
 * - Benchmarking
 */
class TosHash {
public:
    TosHash();
    ~TosHash();

    /**
     * Compute hash for given input
     *
     * @param input Block header data (112 bytes)
     * @param output 32-byte hash result
     * @param scratch Reusable scratchpad (64KB)
     */
    void hash(const uint8_t* input, uint8_t* output, ScratchPad& scratch);

    /**
     * Compute hash and check against target
     *
     * @param work Work package containing header and target
     * @param nonce Nonce to test
     * @param scratch Reusable scratchpad
     * @return Solution if valid, empty Solution otherwise
     */
    Solution search(const WorkPackage& work, Nonce nonce, ScratchPad& scratch);

    /**
     * Verify a solution
     *
     * @param work Work package
     * @param solution Solution to verify
     * @return true if solution is valid
     */
    bool verify(const WorkPackage& work, const Solution& solution);

    /**
     * Benchmark hash rate
     *
     * @param iterations Number of hashes to compute
     * @return Hashes per second
     */
    double benchmark(uint64_t iterations);

private:
    // Internal implementation
    void stage1Init(const uint8_t* input, size_t inputLen, ScratchPad& scratch);
    void stage2Mix(ScratchPad& scratch);
    void stage3Strided(ScratchPad& scratch);
    void stage4Finalize(const ScratchPad& scratch, uint8_t* output);

    // Mixing function
    static inline uint64_t mix(uint64_t a, uint64_t b, size_t round);
    static inline uint64_t rotl64(uint64_t x, uint32_t r);
    static inline uint64_t rotr64(uint64_t x, uint32_t r);
};

}  // namespace tos
