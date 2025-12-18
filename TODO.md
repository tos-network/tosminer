# TOS Miner - TODO List

基于与 cpp-ethereum (ethminer) 的对比分析，以下是需要修复的问题清单。

## Critical (严重 - 可能导致数据丢失或安全问题)

- [x] **工作超时检测** - 矿池停止发送工作时，矿工会无限期继续挖旧工作
  - 文件: `src/stratum/StratumClient.cpp`
  - 解决方案: 添加工作接收超时定时器，60秒无新工作则重连

- [x] **陈旧工作检测** - 无法检测是否在挖旧工作
  - 文件: `src/core/WorkPackage.h`, `src/stratum/StratumClient.cpp`
  - 解决方案: 添加工作时间戳，比较新旧工作

- [x] **GPU 解决方案边界检查** - solutionCount > MAX_OUTPUTS 会导致缓冲区溢出
  - 文件: `src/cuda/CUDAMiner.cpp:324-336`, `src/opencl/CLMiner.cpp`
  - 解决方案: 添加 `min(solutionCount, MAX_OUTPUTS)` 边界检查

- [x] **CUDA 内核错误检查** - 内核启动失败时继续挖矿
  - 文件: `src/cuda/CUDAMiner.cpp:308-318`
  - 解决方案: 检查 `cudaGetLastError()` 并处理错误

- [ ] **TLS 默认严格验证** - 当前默认接受任何证书，存在中间人攻击风险
  - 文件: `src/stratum/StratumClient.h:413`
  - 解决方案: 考虑是否改为默认严格验证，或在文档中强调安全配置

## High (高优先级 - 影响可靠性)

- [x] **重复解决方案检测** - GPU 故障可能返回相同 nonce 多次
  - 文件: `src/core/Miner.cpp`
  - 解决方案: 记录最近提交的 nonce，过滤重复

- [ ] **解决方案 nonce 范围验证** - 未验证 nonce 是否在预期范围内
  - 文件: `src/stratum/StratumClient.cpp:155-206`
  - 解决方案: 验证 nonce 在设备分配的范围内

- [ ] **设备健康监控** - 无法检测 GPU 热问题、内存损坏
  - 文件: `src/core/Miner.h`
  - 解决方案: 添加温度监控、错误模式检测

- [ ] **单设备故障隔离** - 单个 GPU 故障可能影响整个矿场
  - 文件: `src/core/Farm.cpp`
  - 解决方案: 隔离故障设备，继续使用健康设备

- [ ] **JSON-RPC 监控接口** - 无法远程监控矿工状态
  - 解决方案: 添加 HTTP API 端点用于监控

## Medium (中优先级 - 影响操作性)

- [ ] **Stratum V2 支持** - 无法连接到现代矿池
  - 文件: `src/stratum/StratumClient.cpp`
  - 解决方案: 实现 Stratum V2 协议

- [ ] **优雅关闭处理** - 关闭时可能丢失待提交的 share
  - 文件: `src/main.cpp`, `src/stratum/StratumClient.cpp`
  - 解决方案: 等待所有待处理请求完成后再关闭

- [ ] **工作缓存** - 工作接收中断时会停滞
  - 文件: `src/stratum/StratumClient.cpp`
  - 解决方案: 保留上一个有效工作作为备用

- [ ] **多 Stratum 协议变体** - 只支持简化的 TOS 格式
  - 文件: `src/stratum/StratumClient.cpp`
  - 解决方案: 支持 ETHPROXY、ETHEREUMSTRATUM 等格式

## Low (低优先级 - 优化)

- [ ] **并行 GPU 初始化** - 大型矿场启动较慢
  - 文件: `src/core/Farm.cpp`
  - 解决方案: 并行初始化多个 GPU

- [ ] **内核参数调优** - 针对不同 GPU 架构优化
  - 文件: `src/cuda/CUDAMiner.cpp`, `src/opencl/CLMiner.cpp`
  - 解决方案: 添加更多可配置参数

- [ ] **版本协商** - 固定发送 "tosminer/1.0.0"，无能力协商
  - 文件: `src/stratum/StratumClient.cpp:678`
  - 解决方案: 实现协议版本协商

---

## 已完成

- [x] 全 256 位目标计算 (pdiff: base = 0x00000000FFFF << 208, 支持小数难度, 包含单元测试)
- [x] 线程安全的 Stratum 发送
- [x] OpenCL 事件同步管道
- [x] Extranonce2 支持的 nonce 分配
- [x] 请求超时清理
- [x] CUDA 占用率自动调整
- [x] TLS/SSL 支持 (stratum+ssl://)
- [x] 保持矿池发送的目标值
- [x] TLS 证书验证选项
- [x] 工作接收超时检测 (60秒无新工作则重连)
- [x] 陈旧工作检测 (时间戳跟踪)
- [x] GPU 解决方案边界检查 (防止缓冲区溢出)
- [x] CUDA 内核错误检查 (cudaGetLastError)
- [x] 重复解决方案过滤 (nonce 去重)
