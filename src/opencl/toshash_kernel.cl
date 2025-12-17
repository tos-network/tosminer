/**
 * TOS Hash V3 - OpenCL Kernel
 *
 * GPU implementation of TOS Hash V3 algorithm
 * Each work item computes one complete hash independently
 */

// Platform-specific optimizations
#ifdef PLATFORM_AMD
    #pragma OPENCL EXTENSION cl_amd_media_ops : enable
    #pragma OPENCL EXTENSION cl_amd_media_ops2 : enable
#endif

// TOS Hash V3 parameters
#define MEMORY_SIZE 8192          // 64KB / 8 bytes = 8192 uint64
#define MIXING_ROUNDS 8
#define MEMORY_PASSES 4
#define MIX_CONST 0x517cc1b727220a95UL
#define INPUT_SIZE 112
#define HASH_SIZE 32

// Blake3 constants
#define BLAKE3_OUT_LEN 32
#define BLAKE3_KEY_LEN 32
#define BLAKE3_BLOCK_LEN 64
#define BLAKE3_CHUNK_LEN 1024

// Blake3 IV
__constant ulong BLAKE3_IV[8] = {
    0x6A09E667F3BCC908UL, 0xBB67AE8584CAA73BUL,
    0x3C6EF372FE94F82BUL, 0xA54FF53A5F1D36F1UL,
    0x510E527FADE682D1UL, 0x9B05688C2B3E6C1FUL,
    0x1F83D9ABFB41BD6BUL, 0x5BE0CD19137E2179UL
};

// Blake3 message schedule
__constant uchar MSG_SCHEDULE[7][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
    {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
    {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
    {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
    {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
    {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13}
};

// Strides for stage 3
__constant ulong STRIDES[4] = {1, 64, 256, 1024};

// Rotate functions - use platform-specific intrinsics when available
#ifdef PLATFORM_AMD
// AMD has amd_bitalign which can be used for 32-bit rotations
// For 64-bit, we still use the standard approach but with optimized 32-bit parts
inline ulong rotl64(ulong x, uint r) {
    r &= 63;
    if (r == 0) return x;
    if (r == 32) {
        // Swap high and low 32-bit words
        uint lo = (uint)x;
        uint hi = (uint)(x >> 32);
        return ((ulong)lo << 32) | hi;
    }
    return (x << r) | (x >> (64 - r));
}

inline ulong rotr64(ulong x, uint r) {
    r &= 63;
    if (r == 0) return x;
    if (r == 32) {
        uint lo = (uint)x;
        uint hi = (uint)(x >> 32);
        return ((ulong)lo << 32) | hi;
    }
    return (x >> r) | (x << (64 - r));
}
#else
// Generic rotate functions
inline ulong rotl64(ulong x, uint r) {
    r &= 63;
    return (x << r) | (x >> ((64 - r) & 63));
}

inline ulong rotr64(ulong x, uint r) {
    r &= 63;
    return (x >> r) | (x << ((64 - r) & 63));
}
#endif

// TOS Hash mixing function
inline ulong toshash_mix(ulong a, ulong b, ulong round) {
    uint rot = (uint)((round * 7) % 64);
    ulong x = a + b;
    ulong y = a ^ rotl64(b, rot);
    ulong z = x * MIX_CONST;
    return z ^ rotr64(y, rot / 2);
}

// Blake3 G function (quarter round)
inline void blake3_g(ulong* state, uint a, uint b, uint c, uint d, ulong mx, ulong my) {
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
inline void blake3_round(ulong* state, __private ulong* m, uint round) {
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

// Simplified Blake3 hash for our use case (single block input up to 128 bytes)
void blake3_hash(__private uchar* input, uint input_len, __private uchar* output) {
    // Initialize state with IV
    ulong state[16];
    for (int i = 0; i < 8; i++) {
        state[i] = BLAKE3_IV[i];
    }

    // Block 1 setup
    state[8] = BLAKE3_IV[0];
    state[9] = BLAKE3_IV[1];
    state[10] = BLAKE3_IV[2];
    state[11] = BLAKE3_IV[3];
    state[12] = 0;  // counter low
    state[13] = 0;  // counter high
    state[14] = input_len;  // block length
    state[15] = 0x0B;  // flags: CHUNK_START | CHUNK_END | ROOT

    // Prepare message block (pad with zeros)
    ulong m[16];
    for (int i = 0; i < 16; i++) {
        m[i] = 0;
    }

    // Copy input to message block (little-endian)
    for (uint i = 0; i < input_len && i < 128; i++) {
        uint word = i / 8;
        uint byte_pos = i % 8;
        m[word] |= ((ulong)input[i]) << (byte_pos * 8);
    }

    // 7 rounds
    for (uint round = 0; round < 7; round++) {
        blake3_round(state, m, round);
    }

    // Output (XOR of first and last 8 words)
    for (int i = 0; i < 4; i++) {
        ulong out_word = state[i] ^ state[i + 8];
        for (int j = 0; j < 8; j++) {
            output[i * 8 + j] = (uchar)((out_word >> (j * 8)) & 0xFF);
        }
    }
}

// Stage 1: Initialize scratchpad
void stage1_init(__private uchar* input, uint input_len, __local ulong* scratch) {
    // Hash input to get 256-bit seed
    uchar hash[32];
    blake3_hash(input, input_len, hash);

    // Initialize 4-word state from hash
    ulong state[4];
    for (int i = 0; i < 4; i++) {
        state[i] = 0;
        for (int j = 0; j < 8; j++) {
            state[i] |= ((ulong)hash[i * 8 + j]) << (j * 8);
        }
    }

    // Fill scratchpad sequentially
    for (uint i = 0; i < MEMORY_SIZE; i++) {
        uint idx = i % 4;
        state[idx] = toshash_mix(state[idx], state[(idx + 1) % 4], i);
        scratch[i] = state[idx];
    }
}

// Stage 2: Sequential memory mixing
void stage2_mix(__local ulong* scratch) {
    for (uint pass = 0; pass < MEMORY_PASSES; pass++) {
        if (pass % 2 == 0) {
            // Forward pass
            ulong carry = scratch[MEMORY_SIZE - 1];
            for (uint i = 0; i < MEMORY_SIZE; i++) {
                ulong prev = (i > 0) ? scratch[i - 1] : scratch[MEMORY_SIZE - 1];
                scratch[i] = toshash_mix(scratch[i], prev ^ carry, pass);
                carry = scratch[i];
            }
        } else {
            // Backward pass
            ulong carry = scratch[0];
            for (uint i = MEMORY_SIZE; i > 0; i--) {
                uint idx = i - 1;
                ulong next = (idx < MEMORY_SIZE - 1) ? scratch[idx + 1] : scratch[0];
                scratch[idx] = toshash_mix(scratch[idx], next ^ carry, pass);
                carry = scratch[idx];
            }
        }
    }
}

// Stage 3: Strided memory mixing
void stage3_strided(__local ulong* scratch) {
    for (uint round = 0; round < MIXING_ROUNDS; round++) {
        ulong stride = STRIDES[round % 4];

        for (uint i = 0; i < MEMORY_SIZE; i++) {
            uint j = (i + stride) % MEMORY_SIZE;
            uint k = (i + stride * 2) % MEMORY_SIZE;

            ulong a = scratch[i];
            ulong b = scratch[j];
            ulong c = scratch[k];

            scratch[i] = toshash_mix(a, b ^ c, round);
        }
    }
}

// Stage 4: Finalize to 256-bit hash
void stage4_finalize(__local ulong* scratch, __private uchar* output) {
    // XOR-fold to 256 bits (4 x 64-bit words)
    ulong folded[4] = {0, 0, 0, 0};
    for (uint i = 0; i < MEMORY_SIZE; i++) {
        folded[i % 4] ^= scratch[i];
    }

    // Convert to bytes
    uchar bytes[32];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            bytes[i * 8 + j] = (uchar)((folded[i] >> (j * 8)) & 0xFF);
        }
    }

    // Final Blake3 hash
    blake3_hash(bytes, 32, output);
}

// Compare hash against target (returns true if hash < target)
bool meets_target(__private uchar* hash, __constant uchar* target) {
    for (int i = 0; i < 32; i++) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true;  // Equal
}

/**
 * Main search kernel
 *
 * Each work item:
 * 1. Uses local memory for scratchpad (64KB)
 * 2. Computes full TOS Hash V3
 * 3. Checks against target
 * 4. Reports solutions atomically
 *
 * Workgroup size should be 1 to give each work item full local memory
 */
__kernel void toshash_search(
    __global uint* g_output,           // [0] = count, [1..MAX] = solution nonces
    __constant uchar* g_header,        // Block header (104 bytes, nonce goes in last 8)
    __constant uchar* g_target,        // Target hash (32 bytes)
    ulong start_nonce,                 // Starting nonce for this batch
    uint max_outputs                   // Maximum solutions to store
) {
    uint gid = get_global_id(0);
    ulong nonce = start_nonce + gid;

    // Local memory scratchpad (64KB)
    __local ulong scratch[MEMORY_SIZE];

    // Prepare input with nonce
    uchar input[INPUT_SIZE];
    for (int i = 0; i < INPUT_SIZE - 8; i++) {
        input[i] = g_header[i];
    }
    // Set nonce (last 8 bytes, little-endian)
    for (int i = 0; i < 8; i++) {
        input[INPUT_SIZE - 8 + i] = (uchar)(nonce >> (i * 8));
    }

    // Compute TOS Hash V3
    stage1_init(input, INPUT_SIZE, scratch);
    stage2_mix(scratch);
    stage3_strided(scratch);

    uchar hash[32];
    stage4_finalize(scratch, hash);

    // Check against target
    if (meets_target(hash, g_target)) {
        // Found a solution! Store it atomically
        uint slot = atomic_inc(&g_output[0]);
        if (slot < max_outputs) {
            // Store nonce (as two 32-bit values)
            g_output[1 + slot * 2] = (uint)(nonce & 0xFFFFFFFF);
            g_output[1 + slot * 2 + 1] = (uint)(nonce >> 32);
        }
    }
}

/**
 * Benchmark kernel - same as search but without target check
 * Used to measure raw hash rate
 */
__kernel void toshash_benchmark(
    __global ulong* g_hashes,          // Output hashes for verification (optional)
    __constant uchar* g_header,        // Block header
    ulong start_nonce,                 // Starting nonce
    uint store_hashes                  // Whether to store computed hashes
) {
    uint gid = get_global_id(0);
    ulong nonce = start_nonce + gid;

    __local ulong scratch[MEMORY_SIZE];

    uchar input[INPUT_SIZE];
    for (int i = 0; i < INPUT_SIZE - 8; i++) {
        input[i] = g_header[i];
    }
    for (int i = 0; i < 8; i++) {
        input[INPUT_SIZE - 8 + i] = (uchar)(nonce >> (i * 8));
    }

    stage1_init(input, INPUT_SIZE, scratch);
    stage2_mix(scratch);
    stage3_strided(scratch);

    uchar hash[32];
    stage4_finalize(scratch, hash);

    // Optionally store hash for verification
    if (store_hashes) {
        for (int i = 0; i < 4; i++) {
            ulong h = 0;
            for (int j = 0; j < 8; j++) {
                h |= ((ulong)hash[i * 8 + j]) << (j * 8);
            }
            g_hashes[gid * 4 + i] = h;
        }
    }
}
