/**
 * TOS Miner - Lock Guards and Synchronization Primitives
 *
 * Provides SpinLock for high-frequency operations where context
 * switches would be too expensive.
 */

#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>

namespace tos {

// Standard mutex aliases
using Mutex = std::mutex;
using Guard = std::lock_guard<std::mutex>;
using UniqueGuard = std::unique_lock<std::mutex>;

/**
 * SpinLock - Lightweight lock for high-frequency operations
 *
 * Uses atomic test-and-set instead of OS mutex. Better for:
 * - Very short critical sections (few instructions)
 * - High contention with short hold times
 * - Avoiding context switch overhead
 *
 * Not suitable for:
 * - Long critical sections
 * - Waiting for I/O
 * - Low-priority threads (can cause priority inversion)
 */
class SpinLock {
public:
    SpinLock() {
        m_lock.clear(std::memory_order_release);
    }

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() {
        // Spin until we acquire the lock
        while (m_lock.test_and_set(std::memory_order_acquire)) {
            // Hint to CPU that we're in a spin-wait loop
            // This can improve performance on hyperthreaded CPUs
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
            __asm__ volatile("yield");
#endif
        }
    }

    bool try_lock() {
        return !m_lock.test_and_set(std::memory_order_acquire);
    }

    void unlock() {
        m_lock.clear(std::memory_order_release);
    }

private:
    std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
};

// SpinLock guard (RAII)
using SpinGuard = std::lock_guard<SpinLock>;

/**
 * ReadWriteSpinLock - Multiple readers, single writer spin lock
 *
 * Optimized for read-heavy workloads where writes are infrequent.
 */
class ReadWriteSpinLock {
public:
    ReadWriteSpinLock() : m_state(0) {}

    ReadWriteSpinLock(const ReadWriteSpinLock&) = delete;
    ReadWriteSpinLock& operator=(const ReadWriteSpinLock&) = delete;

    void lock_read() {
        while (true) {
            // Wait while writer is active or waiting
            while (m_state.load(std::memory_order_relaxed) & WRITER_MASK) {
#if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#endif
            }

            // Try to increment reader count
            uint32_t expected = m_state.load(std::memory_order_relaxed) & ~WRITER_MASK;
            if (m_state.compare_exchange_weak(expected, expected + 1,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                return;
            }
        }
    }

    void unlock_read() {
        m_state.fetch_sub(1, std::memory_order_release);
    }

    void lock_write() {
        // Set writer waiting bit
        uint32_t expected = 0;
        while (!m_state.compare_exchange_weak(expected, WRITER_MASK,
                std::memory_order_acquire, std::memory_order_relaxed)) {
            expected = 0;  // Only succeed when no readers and no writer
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#endif
        }
    }

    void unlock_write() {
        m_state.store(0, std::memory_order_release);
    }

private:
    static constexpr uint32_t WRITER_MASK = 0x80000000;
    std::atomic<uint32_t> m_state;
};

/**
 * RAII read lock guard
 */
class ReadGuard {
public:
    explicit ReadGuard(ReadWriteSpinLock& lock) : m_lock(lock) {
        m_lock.lock_read();
    }
    ~ReadGuard() {
        m_lock.unlock_read();
    }

    ReadGuard(const ReadGuard&) = delete;
    ReadGuard& operator=(const ReadGuard&) = delete;

private:
    ReadWriteSpinLock& m_lock;
};

/**
 * RAII write lock guard
 */
class WriteGuard {
public:
    explicit WriteGuard(ReadWriteSpinLock& lock) : m_lock(lock) {
        m_lock.lock_write();
    }
    ~WriteGuard() {
        m_lock.unlock_write();
    }

    WriteGuard(const WriteGuard&) = delete;
    WriteGuard& operator=(const WriteGuard&) = delete;

private:
    ReadWriteSpinLock& m_lock;
};

}  // namespace tos
