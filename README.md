# tosminer

TOS Miner is a high-performance GPU mining software for the TOS Hash V3 algorithm. It supports both NVIDIA CUDA and AMD OpenCL devices, with optimized kernels designed for the unique 64KB scratchpad memory requirement of TOS Hash V3.

## Features

### Mining
- NVIDIA CUDA mining with multi-stream pipelining
- AMD OpenCL mining with event-based synchronization
- CPU mining for testing and low-power scenarios
- Automatic device enumeration and selection
- GPU tuning profiles for different architectures

### Networking
- Stratum protocol support (stratum+tcp:// and stratum+ssl://)
- Multiple protocol variants: Stratum, EthProxy, EthereumStratum
- TLS/SSL encrypted pool connections with optional strict verification
- Pool failover with automatic reconnection
- Graceful shutdown with pending share submission

### Monitoring
- **GPU Temperature Monitoring** - Real-time temperature, fan speed, power usage via NVML (NVIDIA) and sysfs (AMD)
- **EMA Hashrate Smoothing** - Exponential Moving Average for stable hashrate display
- **HTTP JSON API** - RESTful API for remote monitoring and integration
- **Device Health Tracking** - Automatic detection of failing or overheating GPUs

### Robustness
- Device failure isolation (failed GPU doesn't stop others)
- Duplicate nonce prevention
- Work caching with fallback support
- Parallel GPU initialization for faster startup

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

## Installation

### Pre-built Binaries

Download the latest release from the [Releases](https://github.com/tos-network/tosminer/releases) page.

### Building from Source

See [Building from Source](#building-from-source) section below.

## Usage

```
tosminer --help
```

### Quick Start

```sh
# List available mining devices
tosminer --list-devices

# Run benchmark
tosminer --benchmark

# Start mining
tosminer -G -P stratum+tcp://pool.example.com:3333 -u wallet.worker
```

### Mining Examples

```sh
# Mine with OpenCL (AMD/Intel GPUs)
tosminer -G -P stratum+tcp://pool.example.com:3333 -u wallet.worker

# Mine with CUDA (NVIDIA GPUs)
tosminer -U -P stratum+tcp://pool.example.com:3333 -u wallet.worker

# Mine with TLS/SSL encryption
tosminer -G -P stratum+ssl://pool.example.com:3334 -u wallet.worker

# Mine with specific devices
tosminer -U --cuda-devices 0,1 -P stratum+tcp://pool.example.com:3333 -u wallet.worker

# Mine with API server enabled
tosminer -G -P stratum+tcp://pool.example.com:3333 -u wallet.worker --api-port 3333

# Use GPU tuning profile
tosminer -U -P stratum+tcp://pool.example.com:3333 -u wallet.worker --profile rtx4090
```

### Command Line Options

#### General Options
| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `-V, --version` | Show version |
| `-v, --verbose` | Verbose output |
| `-q, --quiet` | Quiet output (errors only) |

#### Device Options
| Option | Description |
|--------|-------------|
| `-L, --list-devices` | List available mining devices |
| `-G, --opencl` | Use OpenCL mining |
| `-U, --cuda` | Use CUDA mining |
| `-C, --cpu` | Use CPU mining |
| `--opencl-devices LIST` | OpenCL device indices (e.g., 0,1,2) |
| `--cuda-devices LIST` | CUDA device indices (e.g., 0,1,2) |

#### Pool Options
| Option | Description |
|--------|-------------|
| `-P, --pool URL` | Pool URL (stratum+tcp:// or stratum+ssl://) |
| `-u, --user USER` | Pool username (wallet.worker) |
| `-p, --password PASS` | Pool password (default: x) |
| `--stratum-protocol PROTO` | Protocol: stratum, ethproxy, ethereumstratum |
| `--tls-strict` | Enable strict TLS certificate verification |

#### Performance Options
| Option | Description |
|--------|-------------|
| `--profile NAME` | GPU tuning profile (use --list-profiles to see available) |
| `--list-profiles` | List available GPU tuning profiles |
| `-M, --benchmark` | Run benchmark mode |

#### Monitoring Options
| Option | Description |
|--------|-------------|
| `--api-port PORT` | Enable HTTP API on specified port |

## GPU Tuning Profiles

Pre-configured tuning profiles for different GPU architectures:

```sh
tosminer --list-profiles
```

| Profile | Target GPUs |
|---------|-------------|
| `default` | Generic settings |
| `rtx2080` | NVIDIA Turing (RTX 20 series) |
| `rtx3080` | NVIDIA Ampere (RTX 30 series) |
| `rtx4090` | NVIDIA Ada Lovelace (RTX 40 series) |
| `rx6800` | AMD RDNA2 (RX 6000 series) |
| `rx7900` | AMD RDNA3 (RX 7000 series) |
| `arc770` | Intel Arc (Alchemist) |

## HTTP Monitoring API

Enable the API server with `--api-port`:

```sh
tosminer -G -P stratum+tcp://pool:3333 -u wallet --api-port 8080
```

### Endpoints

#### GET /status
Returns overall miner status.

```json
{
  "version": "1.0.0",
  "uptime": 3600,
  "mining": true,
  "connected": true,
  "hashrate": "2.30 MH/s",
  "hashrate_raw": 2300000.0,
  "hashrate_instant": 2330000.0,
  "hashrate_ema": 2300000.0,
  "shares": {
    "accepted": 150,
    "rejected": 2,
    "stale": 1
  },
  "miners": 2,
  "active_miners": 2
}
```

#### GET /devices
Returns per-device statistics including GPU monitoring data.

```json
[
  {
    "index": 0,
    "name": "NVIDIA GeForce RTX 4090",
    "type": "CUDA",
    "hashrate": 1500000.0,
    "hashrate_ema": 1500000.0,
    "temperature": 72,
    "fan_speed": 65,
    "power_usage": 320,
    "clock_core": 2520,
    "gpu_utilization": 98,
    "failed": false
  }
]
```

#### GET /health
Returns health status with temperature monitoring.

```json
{
  "overall": "healthy",
  "devices": [
    {
      "index": 0,
      "name": "NVIDIA GeForce RTX 4090",
      "status": "healthy",
      "temperature": 72,
      "temperature_status": "normal"
    }
  ],
  "active_miners": 2,
  "total_miners": 2
}
```

Temperature thresholds:
- **normal**: < 80°C
- **warning**: 80-89°C
- **critical**: >= 90°C

#### GET /stats
Returns detailed mining statistics.

```json
{
  "hashrate": 2300000.0,
  "hashrate_instant": 2330000.0,
  "hashrate_ema": 2300000.0,
  "hashes": 8280000000,
  "duration": 3600.0,
  "accepted": 150,
  "rejected": 2,
  "stale": 1,
  "efficiency": 98.04
}
```

## Console Output

The miner displays real-time statistics:

```
12:34:56.789 [I] 2.30 MH/s | A:150 R:2 S:1 | T:72C/68C
```

- **2.30 MH/s** - EMA-smoothed hashrate (stable)
- **A:150** - Accepted shares
- **R:2** - Rejected shares
- **S:1** - Stale shares
- **T:72C/68C** - GPU temperatures

## F.A.Q

### 1. Why does each GPU thread need 64KB of memory?

TOS Hash V3 uses a 64KB scratchpad that is heavily accessed during the mixing phases. This scratchpad must be kept in fast memory (shared memory on CUDA, local memory on OpenCL) for optimal performance. This limits the number of concurrent threads per GPU but ensures memory-hard security.

### 2. What hashrate can I expect?

Performance varies by GPU. Approximate ranges:
- CPU: 500-1500 H/s
- Mid-range GPU (RTX 3060, RX 6600): 100-300 KH/s
- High-end GPU (RTX 4090, RX 7900): 500 KH/s - 1 MH/s

Run `tosminer --benchmark` to test your hardware.

### 3. Why is my hashrate fluctuating?

The miner uses Exponential Moving Average (EMA) with a 30-second window to smooth hashrate display. Initial readings may fluctuate until enough samples are collected.

### 4. How do I monitor GPU temperatures?

GPU temperatures are displayed in the console output and available via the API. The miner uses:
- **NVML** for NVIDIA GPUs
- **sysfs hwmon** for AMD GPUs on Linux

Enable the API with `--api-port 8080` and query `GET /devices` or `GET /health`.

### 5. What happens if a GPU fails?

The miner isolates failed GPUs and continues mining with healthy devices. Failed GPUs are marked in the `/health` API endpoint and excluded from work distribution.

### 6. How do I use TLS/SSL for secure pool connections?

Use `stratum+ssl://` instead of `stratum+tcp://`:
```
tosminer -G -P stratum+ssl://pool.example.com:3334 -u wallet.worker
```

For strict certificate verification:
```
tosminer -G -P stratum+ssl://pool.example.com:3334 -u wallet.worker --tls-strict
```

### 7. Does tosminer support dual mining?

No, tosminer is dedicated to TOS Hash V3 only.

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

### Running Tests

```sh
cd build
./bin/test_target          # pdiff calculation tests
./bin/test_gpu_monitor     # GPU monitoring tests
./bin/test_api_response    # API response structure tests
```

## Project Structure

```
tosminer/
├── src/
│   ├── core/              # Core mining framework
│   │   ├── Miner.cpp      # Base miner class with health tracking
│   │   ├── Farm.cpp       # Multi-device coordinator
│   │   ├── TuningProfiles.h # GPU tuning presets
│   │   └── Types.h        # Common types
│   ├── toshash/           # TOS Hash V3 implementation
│   │   └── TosHash.cpp    # CPU reference implementation
│   ├── opencl/            # OpenCL backend
│   │   ├── CLMiner.cpp
│   │   └── toshash_kernel.cl
│   ├── cuda/              # CUDA backend
│   │   ├── CUDAMiner.cpp
│   │   └── toshash_kernel.cu
│   ├── stratum/           # Pool protocols
│   │   ├── StratumClient.cpp  # Stratum v1
│   │   └── StratumV2.cpp      # Stratum v2 framework
│   ├── api/               # HTTP API server
│   │   └── ApiServer.cpp
│   ├── util/              # Utilities
│   │   ├── Log.cpp
│   │   ├── Guards.h       # SpinLock implementation
│   │   ├── MovingAverage.h # EMA calculation
│   │   └── GpuMonitor.cpp # NVML/AMD monitoring
│   └── main.cpp           # Entry point
├── tests/
│   ├── test_target.cpp       # pdiff tests
│   ├── test_gpu_monitor.cpp  # GPU monitor tests
│   └── test_api_response.cpp # API tests
├── third_party/
│   └── blake3/            # Blake3 hash library
└── CMakeLists.txt
```

## License

This project is licensed under the MIT License.

## Acknowledgments

- Architecture inspired by [ethminer](https://github.com/ethereum-mining/ethminer)
- Blake3 hash function by [BLAKE3 team](https://github.com/BLAKE3-team/BLAKE3)
