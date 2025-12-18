/**
 * TOS Miner - Target Calculation Unit Tests
 *
 * Verifies pdiff target calculation against known vectors.
 */

#include <iostream>
#include <iomanip>
#include <cstdint>
#include <array>
#include <cstring>
#include <cmath>

using Hash256 = std::array<uint8_t, 32>;

// Copy of difficultyToTarget for standalone testing (must match StratumClient.cpp)
void difficultyToTarget(double difficulty, Hash256& target) {
    // Pool difficulty (pdiff) formula:
    // base_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000
    // target = base_target / difficulty

    target.fill(0);

    if (difficulty <= 0) {
        target.fill(0xFF);
        return;
    }

    if (difficulty < 1.0) {
        target[4] = 0xFF;
        target[5] = 0xFF;
        return;
    }

#if defined(__SIZEOF_INT128__)
    // Use fixed-point arithmetic to handle fractional difficulties correctly
    // Scale difficulty by 2^32 to preserve fractional precision
    __uint128_t diffScaled = static_cast<__uint128_t>(difficulty * 4294967296.0);  // 2^32
    if (diffScaled == 0) diffScaled = 1;

    __uint128_t remainder = 0;

    for (int i = 0; i < 36; i++) {
        uint8_t dividendByte = (i == 4 || i == 5) ? 0xFF : 0;

        remainder = (remainder << 8) | dividendByte;

        __uint128_t q = remainder / diffScaled;

        // Output position is shifted by 4 due to 2^32 scaling
        int outputPos = i - 4;
        if (outputPos >= 0 && outputPos < 32) {
            target[outputPos] = (q > 255) ? 255 : static_cast<uint8_t>(q);
        }

        remainder = remainder % diffScaled;
    }
#else
    double quotient = 65535.0 / difficulty;

    for (int i = 4; i < 32; i++) {
        int bitShift = 8 * i - 40;
        double scaled;
        if (bitShift >= 0) {
            scaled = quotient * std::pow(2.0, bitShift);
        } else {
            scaled = quotient / std::pow(2.0, -bitShift);
        }

        double byteVal = std::fmod(std::floor(scaled), 256.0);
        if (byteVal < 0) byteVal = 0;
        if (byteVal > 255) byteVal = 255;
        target[i] = static_cast<uint8_t>(byteVal);
    }
#endif

    bool allZero = true;
    for (int i = 0; i < 32; i++) {
        if (target[i] != 0) {
            allZero = false;
            break;
        }
    }
    if (allZero) {
        target[31] = 1;
    }
}

void printTarget(const Hash256& target) {
    std::cout << "0x";
    for (int i = 0; i < 32; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)target[i];
    }
    std::cout << std::dec << std::endl;
}

bool compareTarget(const Hash256& actual, const Hash256& expected, const std::string& testName) {
    bool match = (actual == expected);
    if (match) {
        std::cout << "[PASS] " << testName << std::endl;
    } else {
        std::cout << "[FAIL] " << testName << std::endl;
        std::cout << "  Expected: ";
        printTarget(expected);
        std::cout << "  Actual:   ";
        printTarget(actual);
    }
    return match;
}

int main() {
    Hash256 target;
    Hash256 expected;
    int passed = 0;
    int failed = 0;

    std::cout << "=== pdiff Target Calculation Tests ===" << std::endl << std::endl;

    // Test 1: difficulty = 1
    // Expected: 0x00000000FFFF0000...00
    difficultyToTarget(1.0, target);
    expected.fill(0);
    expected[4] = 0xFF;
    expected[5] = 0xFF;
    if (compareTarget(target, expected, "difficulty = 1")) passed++; else failed++;

    // Test 2: difficulty = 2
    // Expected: 0x000000007FFF8000...00
    // 0xFFFF / 2 = 0x7FFF remainder 1
    // 1 * 256 / 2 = 128 = 0x80
    difficultyToTarget(2.0, target);
    expected.fill(0);
    expected[4] = 0x7F;
    expected[5] = 0xFF;
    expected[6] = 0x80;
    if (compareTarget(target, expected, "difficulty = 2")) passed++; else failed++;

    // Test 3: difficulty = 256
    // Expected: 0x0000000000FFFF00...00
    // 0xFF / 256 = 0, remainder 0xFF
    // (0xFF * 256 + 0xFF) / 256 = 0xFFFF / 256 = 255 = 0xFF, remainder 0xFF
    // (0xFF * 256 + 0) / 256 = 0xFF00 / 256 = 255 = 0xFF, remainder 0
    difficultyToTarget(256.0, target);
    expected.fill(0);
    expected[5] = 0xFF;
    expected[6] = 0xFF;
    if (compareTarget(target, expected, "difficulty = 256")) passed++; else failed++;

    // Test 4: difficulty = 65535 (0xFFFF)
    // Expected: 0x0000000000010000...00
    // 0xFFFF / 0xFFFF = 1 at position byte 5
    difficultyToTarget(65535.0, target);
    expected.fill(0);
    expected[5] = 0x01;
    if (compareTarget(target, expected, "difficulty = 65535")) passed++; else failed++;

    // Test 5: difficulty = 65536
    // Expected: 0x0000000000010000...00 (no remainder)
    // (0xFF * 256 + 0xFF) = 0xFFFF / 65536 = 0, remainder 0xFFFF
    // (0xFFFF * 256 + 0) / 65536 = 0xFFFF00 / 65536 = 255 = 0xFF, remainder 0xFF00
    // (0xFF00 * 256 + 0) / 65536 = 0xFF0000 / 65536 = 255 = 0xFF, remainder 0
    difficultyToTarget(65536.0, target);
    expected.fill(0);
    expected[6] = 0xFF;
    expected[7] = 0xFF;
    if (compareTarget(target, expected, "difficulty = 65536")) passed++; else failed++;

    // Test 6: difficulty = 1000000 (1M)
    difficultyToTarget(1000000.0, target);
    std::cout << "difficulty = 1000000: ";
    printTarget(target);
    // Just verify it's non-zero and reasonable
    bool nonZero = false;
    for (int i = 0; i < 32; i++) {
        if (target[i] != 0) nonZero = true;
    }
    if (nonZero && target[0] == 0 && target[1] == 0 && target[2] == 0 && target[3] == 0) {
        std::cout << "[PASS] difficulty = 1000000 (sanity check)" << std::endl;
        passed++;
    } else {
        std::cout << "[FAIL] difficulty = 1000000 (sanity check)" << std::endl;
        failed++;
    }

    std::cout << std::endl << "=== Fractional Difficulty Tests ===" << std::endl << std::endl;

    // Test 7: difficulty = 1.5
    // Expected: 0x00000000AAAA0000...00 (since 0xFFFF / 1.5 = 0xAAAA exactly)
    // 0xFFFF = 65535, 65535 / 1.5 = 43690 = 0xAAAA
    difficultyToTarget(1.5, target);
    expected.fill(0);
    expected[4] = 0xAA;
    expected[5] = 0xAA;
    if (compareTarget(target, expected, "difficulty = 1.5")) passed++; else failed++;

    // Test 8: difficulty = 3.0
    // Expected: 0xFFFF / 3 = 21845 = 0x5555
    difficultyToTarget(3.0, target);
    expected.fill(0);
    expected[4] = 0x55;
    expected[5] = 0x55;
    if (compareTarget(target, expected, "difficulty = 3.0")) passed++; else failed++;

    // Test 9: difficulty = 7.25
    // 0xFFFF / 7.25 = 65535 / 7.25 = 9039.31... = 0x234F...
    difficultyToTarget(7.25, target);
    std::cout << "difficulty = 7.25: ";
    printTarget(target);
    // Verify bytes 4-5 are approximately 0x234F (allowing for rounding)
    uint16_t high16 = (static_cast<uint16_t>(target[4]) << 8) | target[5];
    uint16_t expected16 = static_cast<uint16_t>(65535.0 / 7.25);  // 9039
    if (high16 == expected16 || high16 == expected16 + 1 || high16 == expected16 - 1) {
        std::cout << "[PASS] difficulty = 7.25 (high16=" << high16 << ", expected~" << expected16 << ")" << std::endl;
        passed++;
    } else {
        std::cout << "[FAIL] difficulty = 7.25 (high16=" << high16 << ", expected~" << expected16 << ")" << std::endl;
        failed++;
    }

    // Test 10: difficulty = 123.75
    // 0xFFFF / 123.75 = 529.6... = 0x0211...
    difficultyToTarget(123.75, target);
    std::cout << "difficulty = 123.75: ";
    printTarget(target);
    high16 = (static_cast<uint16_t>(target[4]) << 8) | target[5];
    expected16 = static_cast<uint16_t>(65535.0 / 123.75);  // 529
    if (high16 == expected16 || high16 == expected16 + 1 || high16 == expected16 - 1) {
        std::cout << "[PASS] difficulty = 123.75 (high16=" << high16 << ", expected~" << expected16 << ")" << std::endl;
        passed++;
    } else {
        std::cout << "[FAIL] difficulty = 123.75 (high16=" << high16 << ", expected~" << expected16 << ")" << std::endl;
        failed++;
    }

    // Test 11: difficulty = 0.5 (sub-1 difficulty)
    // Should return base target (or close to it)
    difficultyToTarget(0.5, target);
    expected.fill(0);
    expected[4] = 0xFF;
    expected[5] = 0xFF;
    if (compareTarget(target, expected, "difficulty = 0.5 (capped at base)")) passed++; else failed++;

    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return (failed == 0) ? 0 : 1;
}
