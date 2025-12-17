/**
 * TOS Hash V3 - C++ Implementation
 */

#include "TosHash.h"
#include <blake3.h>
#include <chrono>
#include <cstring>

namespace tos {

// Strides for stage 3
static constexpr size_t STRIDES[4] = {1, 64, 256, 1024};

TosHash::TosHash() {}

TosHash::~TosHash() {}

inline uint64_t TosHash::rotl64(uint64_t x, uint32_t r) {
    r &= 63;
    return (x << r) | (x >> ((64 - r) & 63));
}

inline uint64_t TosHash::rotr64(uint64_t x, uint32_t r) {
    r &= 63;
    return (x >> r) | (x << ((64 - r) & 63));
}

inline uint64_t TosHash::mix(uint64_t a, uint64_t b, size_t round) {
    uint32_t rot = static_cast<uint32_t>((round * 7) % 64);
    uint64_t x = a + b;
    uint64_t y = a ^ rotl64(b, rot);
    uint64_t z = x * TOSHASH_MIX_CONST;
    return z ^ rotr64(y, rot / 2);
}

void TosHash::stage1Init(const uint8_t* input, size_t inputLen, ScratchPad& scratch) {
    // Hash input to get 256-bit seed
    uint8_t hash[32];
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, input, inputLen);
    blake3_hasher_finalize(&hasher, hash, 32);

    // Initialize 4-word state from hash
    uint64_t state[4];
    for (int i = 0; i < 4; ++i) {
        state[i] = 0;
        for (int j = 0; j < 8; ++j) {
            state[i] |= static_cast<uint64_t>(hash[i * 8 + j]) << (j * 8);
        }
    }

    // Fill scratchpad sequentially
    for (size_t i = 0; i < TOSHASH_MEMORY_SIZE; ++i) {
        size_t idx = i % 4;
        state[idx] = mix(state[idx], state[(idx + 1) % 4], i);
        scratch[i] = state[idx];
    }
}

void TosHash::stage2Mix(ScratchPad& scratch) {
    for (size_t pass = 0; pass < TOSHASH_MEMORY_PASSES; ++pass) {
        if (pass % 2 == 0) {
            // Forward pass
            uint64_t carry = scratch[TOSHASH_MEMORY_SIZE - 1];
            for (size_t i = 0; i < TOSHASH_MEMORY_SIZE; ++i) {
                uint64_t prev = (i > 0) ? scratch[i - 1] : scratch[TOSHASH_MEMORY_SIZE - 1];
                scratch[i] = mix(scratch[i], prev ^ carry, pass);
                carry = scratch[i];
            }
        } else {
            // Backward pass
            uint64_t carry = scratch[0];
            for (size_t i = TOSHASH_MEMORY_SIZE; i > 0; --i) {
                size_t idx = i - 1;
                uint64_t next = (idx < TOSHASH_MEMORY_SIZE - 1) ? scratch[idx + 1] : scratch[0];
                scratch[idx] = mix(scratch[idx], next ^ carry, pass);
                carry = scratch[idx];
            }
        }
    }
}

void TosHash::stage3Strided(ScratchPad& scratch) {
    for (size_t round = 0; round < TOSHASH_MIXING_ROUNDS; ++round) {
        size_t stride = STRIDES[round % 4];

        for (size_t i = 0; i < TOSHASH_MEMORY_SIZE; ++i) {
            size_t j = (i + stride) % TOSHASH_MEMORY_SIZE;
            size_t k = (i + stride * 2) % TOSHASH_MEMORY_SIZE;

            uint64_t a = scratch[i];
            uint64_t b = scratch[j];
            uint64_t c = scratch[k];

            scratch[i] = mix(a, b ^ c, round);
        }
    }
}

void TosHash::stage4Finalize(const ScratchPad& scratch, uint8_t* output) {
    // XOR-fold to 256 bits (4 x 64-bit words)
    uint64_t folded[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < TOSHASH_MEMORY_SIZE; ++i) {
        folded[i % 4] ^= scratch[i];
    }

    // Convert to bytes
    uint8_t bytes[32];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 8; ++j) {
            bytes[i * 8 + j] = static_cast<uint8_t>((folded[i] >> (j * 8)) & 0xFF);
        }
    }

    // Final Blake3 hash
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, bytes, 32);
    blake3_hasher_finalize(&hasher, output, 32);
}

void TosHash::hash(const uint8_t* input, uint8_t* output, ScratchPad& scratch) {
    stage1Init(input, INPUT_SIZE, scratch);
    stage2Mix(scratch);
    stage3Strided(scratch);
    stage4Finalize(scratch, output);
}

Solution TosHash::search(const WorkPackage& work, Nonce nonce, ScratchPad& scratch) {
    // Prepare input with nonce
    std::array<uint8_t, INPUT_SIZE> input;
    std::memcpy(input.data(), work.header.data(), INPUT_SIZE);

    // Set nonce (last 8 bytes, little-endian)
    for (int i = 0; i < 8; ++i) {
        input[INPUT_SIZE - 8 + i] = static_cast<uint8_t>(nonce >> (i * 8));
    }

    // Compute hash
    Hash256 hashResult;
    hash(input.data(), hashResult.data(), scratch);

    // Check against target
    if (meetsTarget(hashResult, work.target)) {
        return Solution(nonce, hashResult);
    }

    return Solution();
}

bool TosHash::verify(const WorkPackage& work, const Solution& solution) {
    ScratchPad scratch;

    // Prepare input with nonce
    std::array<uint8_t, INPUT_SIZE> input;
    std::memcpy(input.data(), work.header.data(), INPUT_SIZE);

    for (int i = 0; i < 8; ++i) {
        input[INPUT_SIZE - 8 + i] = static_cast<uint8_t>(solution.nonce >> (i * 8));
    }

    // Compute hash
    Hash256 hashResult;
    hash(input.data(), hashResult.data(), scratch);

    // Verify hash matches and meets target
    return (hashResult == solution.hash) && meetsTarget(hashResult, work.target);
}

double TosHash::benchmark(uint64_t iterations) {
    ScratchPad scratch;
    std::array<uint8_t, INPUT_SIZE> input{};
    uint8_t output[32];

    auto start = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 0; i < iterations; ++i) {
        // Vary input slightly
        input[0] = static_cast<uint8_t>(i & 0xFF);
        input[1] = static_cast<uint8_t>((i >> 8) & 0xFF);
        hash(input.data(), output, scratch);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double seconds = duration.count() / 1000000.0;
    return static_cast<double>(iterations) / seconds;
}

}  // namespace tos
