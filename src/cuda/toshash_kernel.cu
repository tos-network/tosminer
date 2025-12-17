/**
 * TOS Hash V3 - CUDA Kernel
 *
 * GPU implementation of TOS Hash V3 algorithm for NVIDIA GPUs
 */

#include <cuda_runtime.h>
#include <cstdint>

// TOS Hash V3 parameters
#define MEMORY_SIZE 8192          // 64KB / 8 bytes = 8192 uint64
#define MIXING_ROUNDS 8
#define MEMORY_PASSES 4
#define MIX_CONST 0x517cc1b727220a95ULL
#define INPUT_SIZE 112
#define HASH_SIZE 32

// Maximum solutions per kernel launch
#define MAX_OUTPUTS 64

// Blake3 IV
__constant__ uint64_t BLAKE3_IV[8] = {
    0x6A09E667F3BCC908ULL, 0xBB67AE8584CAA73BULL,
    0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL,
    0x510E527FADE682D1ULL, 0x9B05688C2B3E6C1FULL,
    0x1F83D9ABFB41BD6BULL, 0x5BE0CD19137E2179ULL
};

// Blake3 message schedule
__constant__ uint8_t MSG_SCHEDULE[7][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
    {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
    {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
    {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
    {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
    {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13}
};

// Strides for stage 3
__constant__ uint64_t STRIDES[4] = {1, 64, 256, 1024};

// Constant memory for header and target
__constant__ uint8_t d_header[INPUT_SIZE];
__constant__ uint8_t d_target[HASH_SIZE];

// Rotate functions
__device__ __forceinline__ uint64_t rotl64(uint64_t x, uint32_t r) {
    r &= 63;
    return (x << r) | (x >> ((64 - r) & 63));
}

__device__ __forceinline__ uint64_t rotr64(uint64_t x, uint32_t r) {
    r &= 63;
    return (x >> r) | (x << ((64 - r) & 63));
}

// TOS Hash mixing function
__device__ __forceinline__ uint64_t toshash_mix(uint64_t a, uint64_t b, uint64_t round) {
    uint32_t rot = (uint32_t)((round * 7) % 64);
    uint64_t x = a + b;
    uint64_t y = a ^ rotl64(b, rot);
    uint64_t z = x * MIX_CONST;
    return z ^ rotr64(y, rot / 2);
}

// Blake3 G function
__device__ __forceinline__ void blake3_g(uint64_t* state, uint32_t a, uint32_t b,
                                          uint32_t c, uint32_t d, uint64_t mx, uint64_t my) {
    state[a] = state[a] + state[b] + mx;
    state[d] = rotr64(state[d] ^ state[a], 32);
    state[c] = state[c] + state[d];
    state[b] = rotr64(state[b] ^ state[c], 24);
    state[a] = state[a] + state[b] + my;
    state[d] = rotr64(state[d] ^ state[a], 16);
    state[c] = state[c] + state[d];
    state[b] = rotr64(state[b] ^ state[c], 63);
}

// Blake3 round
__device__ void blake3_round(uint64_t* state, uint64_t* m, uint32_t round) {
    // Column step
    blake3_g(state, 0, 4, 8, 12, m[MSG_SCHEDULE[round][0]], m[MSG_SCHEDULE[round][1]]);
    blake3_g(state, 1, 5, 9, 13, m[MSG_SCHEDULE[round][2]], m[MSG_SCHEDULE[round][3]]);
    blake3_g(state, 2, 6, 10, 14, m[MSG_SCHEDULE[round][4]], m[MSG_SCHEDULE[round][5]]);
    blake3_g(state, 3, 7, 11, 15, m[MSG_SCHEDULE[round][6]], m[MSG_SCHEDULE[round][7]]);

    // Diagonal step
    blake3_g(state, 0, 5, 10, 15, m[MSG_SCHEDULE[round][8]], m[MSG_SCHEDULE[round][9]]);
    blake3_g(state, 1, 6, 11, 12, m[MSG_SCHEDULE[round][10]], m[MSG_SCHEDULE[round][11]]);
    blake3_g(state, 2, 7, 8, 13, m[MSG_SCHEDULE[round][12]], m[MSG_SCHEDULE[round][13]]);
    blake3_g(state, 3, 4, 9, 14, m[MSG_SCHEDULE[round][14]], m[MSG_SCHEDULE[round][15]]);
}

// Blake3 hash (simplified for our use case)
__device__ void blake3_hash(uint8_t* input, uint32_t input_len, uint8_t* output) {
    uint64_t state[16];
    for (int i = 0; i < 8; i++) {
        state[i] = BLAKE3_IV[i];
    }
    state[8] = BLAKE3_IV[0];
    state[9] = BLAKE3_IV[1];
    state[10] = BLAKE3_IV[2];
    state[11] = BLAKE3_IV[3];
    state[12] = 0;
    state[13] = 0;
    state[14] = input_len;
    state[15] = 0x0B;  // CHUNK_START | CHUNK_END | ROOT

    uint64_t m[16];
    for (int i = 0; i < 16; i++) m[i] = 0;

    for (uint32_t i = 0; i < input_len && i < 128; i++) {
        uint32_t word = i / 8;
        uint32_t byte_pos = i % 8;
        m[word] |= ((uint64_t)input[i]) << (byte_pos * 8);
    }

    for (uint32_t round = 0; round < 7; round++) {
        blake3_round(state, m, round);
    }

    for (int i = 0; i < 4; i++) {
        uint64_t out_word = state[i] ^ state[i + 8];
        for (int j = 0; j < 8; j++) {
            output[i * 8 + j] = (uint8_t)((out_word >> (j * 8)) & 0xFF);
        }
    }
}

// Stage 1: Initialize scratchpad
__device__ void stage1_init(uint8_t* input, uint32_t input_len, uint64_t* scratch) {
    uint8_t hash[32];
    blake3_hash(input, input_len, hash);

    uint64_t state[4];
    for (int i = 0; i < 4; i++) {
        state[i] = 0;
        for (int j = 0; j < 8; j++) {
            state[i] |= ((uint64_t)hash[i * 8 + j]) << (j * 8);
        }
    }

    for (uint32_t i = 0; i < MEMORY_SIZE; i++) {
        uint32_t idx = i % 4;
        state[idx] = toshash_mix(state[idx], state[(idx + 1) % 4], i);
        scratch[i] = state[idx];
    }
}

// Stage 2: Sequential memory mixing
__device__ void stage2_mix(uint64_t* scratch) {
    for (uint32_t pass = 0; pass < MEMORY_PASSES; pass++) {
        if (pass % 2 == 0) {
            uint64_t carry = scratch[MEMORY_SIZE - 1];
            for (uint32_t i = 0; i < MEMORY_SIZE; i++) {
                uint64_t prev = (i > 0) ? scratch[i - 1] : scratch[MEMORY_SIZE - 1];
                scratch[i] = toshash_mix(scratch[i], prev ^ carry, pass);
                carry = scratch[i];
            }
        } else {
            uint64_t carry = scratch[0];
            for (uint32_t i = MEMORY_SIZE; i > 0; i--) {
                uint32_t idx = i - 1;
                uint64_t next = (idx < MEMORY_SIZE - 1) ? scratch[idx + 1] : scratch[0];
                scratch[idx] = toshash_mix(scratch[idx], next ^ carry, pass);
                carry = scratch[idx];
            }
        }
    }
}

// Stage 3: Strided memory mixing
__device__ void stage3_strided(uint64_t* scratch) {
    for (uint32_t round = 0; round < MIXING_ROUNDS; round++) {
        uint64_t stride = STRIDES[round % 4];

        for (uint32_t i = 0; i < MEMORY_SIZE; i++) {
            uint32_t j = (i + stride) % MEMORY_SIZE;
            uint32_t k = (i + stride * 2) % MEMORY_SIZE;

            uint64_t a = scratch[i];
            uint64_t b = scratch[j];
            uint64_t c = scratch[k];

            scratch[i] = toshash_mix(a, b ^ c, round);
        }
    }
}

// Stage 4: Finalize
__device__ void stage4_finalize(uint64_t* scratch, uint8_t* output) {
    uint64_t folded[4] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < MEMORY_SIZE; i++) {
        folded[i % 4] ^= scratch[i];
    }

    uint8_t bytes[32];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            bytes[i * 8 + j] = (uint8_t)((folded[i] >> (j * 8)) & 0xFF);
        }
    }

    blake3_hash(bytes, 32, output);
}

// Compare hash against target
__device__ bool meets_target(uint8_t* hash, const uint8_t* target) {
    for (int i = 0; i < 32; i++) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true;
}

/**
 * Main search kernel
 *
 * Uses shared memory for scratchpad (64KB per thread)
 * Block size should be 1 to maximize shared memory per thread
 */
extern "C" __global__ void toshash_search(
    uint32_t* g_output,      // [0] = count, [1..] = solution nonces
    uint64_t start_nonce
) {
    uint32_t gid = blockIdx.x * blockDim.x + threadIdx.x;
    uint64_t nonce = start_nonce + gid;

    // Shared memory scratchpad (64KB)
    __shared__ uint64_t scratch[MEMORY_SIZE];

    // Prepare input with nonce
    uint8_t input[INPUT_SIZE];
    for (int i = 0; i < INPUT_SIZE - 8; i++) {
        input[i] = d_header[i];
    }
    for (int i = 0; i < 8; i++) {
        input[INPUT_SIZE - 8 + i] = (uint8_t)(nonce >> (i * 8));
    }

    // Compute TOS Hash V3
    stage1_init(input, INPUT_SIZE, scratch);
    stage2_mix(scratch);
    stage3_strided(scratch);

    uint8_t hash[32];
    stage4_finalize(scratch, hash);

    // Check against target
    if (meets_target(hash, d_target)) {
        uint32_t slot = atomicAdd(&g_output[0], 1);
        if (slot < MAX_OUTPUTS) {
            g_output[1 + slot * 2] = (uint32_t)(nonce & 0xFFFFFFFF);
            g_output[1 + slot * 2 + 1] = (uint32_t)(nonce >> 32);
        }
    }
}

/**
 * Benchmark kernel - without target check
 */
extern "C" __global__ void toshash_benchmark(
    uint64_t* g_hashes,      // Output hashes (optional)
    uint64_t start_nonce,
    uint32_t store_hashes
) {
    uint32_t gid = blockIdx.x * blockDim.x + threadIdx.x;
    uint64_t nonce = start_nonce + gid;

    __shared__ uint64_t scratch[MEMORY_SIZE];

    uint8_t input[INPUT_SIZE];
    for (int i = 0; i < INPUT_SIZE - 8; i++) {
        input[i] = d_header[i];
    }
    for (int i = 0; i < 8; i++) {
        input[INPUT_SIZE - 8 + i] = (uint8_t)(nonce >> (i * 8));
    }

    stage1_init(input, INPUT_SIZE, scratch);
    stage2_mix(scratch);
    stage3_strided(scratch);

    uint8_t hash[32];
    stage4_finalize(scratch, hash);

    if (store_hashes && g_hashes) {
        for (int i = 0; i < 4; i++) {
            uint64_t h = 0;
            for (int j = 0; j < 8; j++) {
                h |= ((uint64_t)hash[i * 8 + j]) << (j * 8);
            }
            g_hashes[gid * 4 + i] = h;
        }
    }
}

// Host wrapper functions
extern "C" {

cudaError_t toshash_set_header(const uint8_t* header) {
    return cudaMemcpyToSymbol(d_header, header, INPUT_SIZE);
}

cudaError_t toshash_set_target(const uint8_t* target) {
    return cudaMemcpyToSymbol(d_target, target, HASH_SIZE);
}

}
