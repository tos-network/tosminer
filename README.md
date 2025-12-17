# tosminer

TOS Miner is a high-performance GPU mining worker for the TOS Hash V3 algorithm. It supports both NVIDIA CUDA and AMD OpenCL devices, with optimized kernels designed for the unique 64KB scratchpad memory requirement of TOS Hash V3.

### Features

- NVIDIA CUDA mining with multi-stream pipelining
- AMD OpenCL mining with event-based synchronization
- CPU mining for testing and low-power scenarios
- Stratum protocol support (stratum+tcp:// and stratum+ssl://)
- TLS/SSL encrypted pool connections
- Automatic device enumeration and selection
- Pool failover support
- Real-time hashrate monitoring
- Benchmark mode for performance testing

## TOS Hash V3 Algorithm

TOS Hash V3 is a memory-hard proof-of-work algorithm with the following characteristics:

| Property | Value |
|----------|-------|
| Input Size | 112 bytes (header + nonce) |
| Output Size | 32 bytes (256-bit hash) |
| Scratchpad | 64KB per hash |
| Hash Function | Blake3 |
| Memory Passes | 4 sequential + 8 strided |

Unlike Ethash which requires a large DAG (1-4GB), TOS Hash V3 uses a small but frequently-accessed 64KB scratchpad, making it suitable for a wider range of GPUs.

## Usage

tosminer is a command line program. For a full list of available commands, run:

```
tosminer --help
```

### Examples

```sh
# List available mining devices
tosminer --list-devices

# Run benchmark
tosminer --benchmark

# Mine with OpenCL (AMD/Intel GPUs)
tosminer -G -P stratum+tcp://pool.example.com:3333 -u wallet.worker

# Mine with CUDA (NVIDIA GPUs)
tosminer -U -P stratum+tcp://pool.example.com:3333 -u wallet.worker

# Mine with TLS/SSL encryption
tosminer -G -P stratum+ssl://pool.example.com:3334 -u wallet.worker

# Mine with specific devices
tosminer -U --cuda-devices 0,1 -P stratum+tcp://pool.example.com:3333 -u wallet.worker

# CPU mining (for testing)
tosminer -C -P stratum+tcp://pool.example.com:3333 -u wallet.worker
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `-V, --version` | Show version |
| `-L, --list-devices` | List available mining devices |
| `-G, --opencl` | Use OpenCL mining |
| `-U, --cuda` | Use CUDA mining |
| `-C, --cpu` | Use CPU mining |
| `-P, --pool URL` | Pool URL (stratum+tcp:// or stratum+ssl://) |
| `-u, --user USER` | Pool username (wallet.worker) |
| `-p, --password PASS` | Pool password (default: x) |
| `-M, --benchmark` | Run benchmark mode |

## F.A.Q

1. **Why does each GPU thread need 64KB of memory?**

   TOS Hash V3 uses a 64KB scratchpad that is heavily accessed during the mixing phases. This scratchpad must be kept in fast memory (shared memory on CUDA, local memory on OpenCL) for optimal performance. This limits the number of concurrent threads per GPU but ensures memory-hard security.

2. **What hashrate can I expect?**

   Performance varies by GPU. Approximate ranges:
   - CPU: 500-1500 H/s
   - Mid-range GPU (RTX 3060, RX 6600): 100-300 KH/s
   - High-end GPU (RTX 4090, RX 7900): 500 KH/s - 1 MH/s

   Run `tosminer --benchmark` to test your hardware.

3. **Why is my CUDA hashrate lower than expected?**

   The 64KB shared memory requirement limits GPU occupancy. Ensure you have the latest NVIDIA drivers. The miner auto-tunes grid size based on your GPU's SM count and shared memory capacity.

4. **Does tosminer support dual mining?**

   No, tosminer is dedicated to TOS Hash V3 only.

5. **How do I use TLS/SSL for secure pool connections?**

   Simply use `stratum+ssl://` instead of `stratum+tcp://` in your pool URL:
   ```
   tosminer -G -P stratum+ssl://pool.example.com:3334 -u wallet.worker
   ```

## Building from Source

### Prerequisites

- CMake >= 3.10
- C++17 compatible compiler
- Boost >= 1.65 (system, program_options)
- OpenSSL (for TLS support)
- OpenCL SDK (for OpenCL mining)
- CUDA Toolkit >= 10.0 (for CUDA mining)

### Build Instructions

```sh
git clone https://github.com/tos-network/tosminer.git
cd tosminer
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

The binary will be located at `build/bin/tosminer`.

### CMake Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `-DWITH_OPENCL=ON` | ON | Enable OpenCL mining support |
| `-DWITH_CUDA=ON` | ON | Enable CUDA mining support |
| `-DWITH_CPU=ON` | ON | Enable CPU mining support |
| `-DWITH_TLS=ON` | ON | Enable TLS/SSL for stratum+ssl:// |

### Example Build Configurations

```sh
# Full build with all features
cmake .. -DWITH_OPENCL=ON -DWITH_CUDA=ON -DWITH_TLS=ON

# OpenCL only (AMD/Intel GPUs)
cmake .. -DWITH_OPENCL=ON -DWITH_CUDA=OFF

# CUDA only (NVIDIA GPUs)
cmake .. -DWITH_OPENCL=OFF -DWITH_CUDA=ON

# CPU only (minimal build)
cmake .. -DWITH_OPENCL=OFF -DWITH_CUDA=OFF
```

## Project Structure

```
tosminer/
├── src/
│   ├── core/           # Core mining framework
│   │   ├── Miner.cpp   # Base miner class
│   │   ├── Farm.cpp    # Multi-device coordinator
│   │   └── Types.h     # Common types
│   ├── toshash/        # TOS Hash V3 implementation
│   │   └── TosHash.cpp # CPU reference implementation
│   ├── opencl/         # OpenCL backend
│   │   ├── CLMiner.cpp
│   │   └── toshash_kernel.cl
│   ├── cuda/           # CUDA backend
│   │   ├── CUDAMiner.cpp
│   │   └── toshash_kernel.cu
│   ├── stratum/        # Pool protocol
│   │   └── StratumClient.cpp
│   └── main.cpp        # Entry point
├── third_party/
│   └── blake3/         # Blake3 hash library
└── CMakeLists.txt
```

## License

This project is licensed under the MIT License.

## Acknowledgments

- Architecture inspired by [ethminer](https://github.com/ethereum-mining/ethminer)
- Blake3 hash function by [BLAKE3 team](https://github.com/BLAKE3-team/BLAKE3)
