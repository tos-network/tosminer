/**
 * TOS Miner - Moving Average Calculations
 *
 * Provides EMA (Exponential Moving Average) for smooth hashrate display.
 */

#pragma once

#include <chrono>
#include <deque>
#include <cmath>

namespace tos {

/**
 * Exponential Moving Average (EMA)
 *
 * Provides smooth average that weights recent values more heavily.
 * Formula: EMA_t = alpha * value_t + (1 - alpha) * EMA_{t-1}
 * where alpha = 2 / (period + 1)
 */
class ExponentialMovingAverage {
public:
    /**
     * Constructor
     * @param period Number of samples for smoothing (higher = smoother but slower response)
     */
    explicit ExponentialMovingAverage(unsigned period = 10)
        : m_alpha(2.0 / (period + 1.0))
        , m_value(0)
        , m_initialized(false)
    {}

    /**
     * Add a new sample
     * @param value New sample value
     */
    void add(double value) {
        if (!m_initialized) {
            m_value = value;
            m_initialized = true;
        } else {
            m_value = m_alpha * value + (1.0 - m_alpha) * m_value;
        }
    }

    /**
     * Get current EMA value
     */
    double get() const {
        return m_value;
    }

    /**
     * Check if EMA has been initialized
     */
    bool isInitialized() const {
        return m_initialized;
    }

    /**
     * Reset to uninitialized state
     */
    void reset() {
        m_value = 0;
        m_initialized = false;
    }

    /**
     * Set smoothing period
     */
    void setPeriod(unsigned period) {
        m_alpha = 2.0 / (period + 1.0);
    }

private:
    double m_alpha;
    double m_value;
    bool m_initialized;
};

/**
 * Simple Moving Average (SMA) with fixed window
 *
 * Uses a deque to maintain a sliding window of samples.
 */
class SimpleMovingAverage {
public:
    /**
     * Constructor
     * @param windowSize Number of samples to average
     */
    explicit SimpleMovingAverage(size_t windowSize = 10)
        : m_windowSize(windowSize)
        , m_sum(0)
    {}

    /**
     * Add a new sample
     * @param value New sample value
     */
    void add(double value) {
        m_samples.push_back(value);
        m_sum += value;

        while (m_samples.size() > m_windowSize) {
            m_sum -= m_samples.front();
            m_samples.pop_front();
        }
    }

    /**
     * Get current average
     */
    double get() const {
        if (m_samples.empty()) {
            return 0;
        }
        return m_sum / m_samples.size();
    }

    /**
     * Get number of samples
     */
    size_t count() const {
        return m_samples.size();
    }

    /**
     * Check if window is full
     */
    bool isFull() const {
        return m_samples.size() >= m_windowSize;
    }

    /**
     * Reset to empty state
     */
    void reset() {
        m_samples.clear();
        m_sum = 0;
    }

private:
    size_t m_windowSize;
    std::deque<double> m_samples;
    double m_sum;
};

/**
 * Time-weighted hashrate calculator
 *
 * Calculates hashrate using EMA with configurable averaging period.
 * Handles variable sample intervals correctly.
 */
class HashRateCalculator {
public:
    /**
     * Constructor
     * @param emaPeriod EMA smoothing period in seconds
     */
    explicit HashRateCalculator(double emaPeriod = 30.0)
        : m_emaPeriod(emaPeriod)
        , m_lastCount(0)
        , m_currentRate(0)
        , m_emaRate(0)
        , m_initialized(false)
    {
        m_lastUpdate = std::chrono::steady_clock::now();
    }

    /**
     * Update with new hash count
     * @param totalCount Total hashes computed since start
     */
    void update(uint64_t totalCount) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - m_lastUpdate).count();

        if (elapsed < 0.1) {
            // Too short interval, skip to avoid noise
            return;
        }

        // Calculate instantaneous rate
        uint64_t delta = totalCount - m_lastCount;
        m_currentRate = static_cast<double>(delta) / elapsed;

        // Update EMA with time-weighted alpha
        // For variable intervals: alpha = 1 - exp(-elapsed / period)
        if (!m_initialized) {
            m_emaRate = m_currentRate;
            m_initialized = true;
        } else {
            double alpha = 1.0 - std::exp(-elapsed / m_emaPeriod);
            m_emaRate = alpha * m_currentRate + (1.0 - alpha) * m_emaRate;
        }

        m_lastCount = totalCount;
        m_lastUpdate = now;
    }

    /**
     * Get current instantaneous rate (noisy)
     */
    double getInstantRate() const {
        return m_currentRate;
    }

    /**
     * Get smoothed EMA rate (stable)
     */
    double getEmaRate() const {
        return m_emaRate;
    }

    /**
     * Get effective rate (EMA if available, else instant)
     */
    double getEffectiveRate() const {
        return m_initialized ? m_emaRate : m_currentRate;
    }

    /**
     * Reset calculator
     */
    void reset() {
        m_lastCount = 0;
        m_currentRate = 0;
        m_emaRate = 0;
        m_initialized = false;
        m_lastUpdate = std::chrono::steady_clock::now();
    }

    /**
     * Reset with initial count (for continuing after pause)
     */
    void reset(uint64_t initialCount) {
        m_lastCount = initialCount;
        m_currentRate = 0;
        m_emaRate = 0;
        m_initialized = false;
        m_lastUpdate = std::chrono::steady_clock::now();
    }

    /**
     * Set EMA period
     */
    void setEmaPeriod(double seconds) {
        m_emaPeriod = seconds;
    }

private:
    double m_emaPeriod;
    uint64_t m_lastCount;
    double m_currentRate;
    double m_emaRate;
    bool m_initialized;
    std::chrono::steady_clock::time_point m_lastUpdate;
};

}  // namespace tos
