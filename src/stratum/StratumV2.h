/**
 * TOS Miner - Stratum V2 Protocol Support
 *
 * Stratum V2 is a binary protocol with improved efficiency and security.
 * Key features:
 * - Binary framing (no JSON overhead)
 * - Noise protocol encryption
 * - Channel-based communication
 * - Job declaration protocol
 *
 * This is a framework that can be extended for full V2 support.
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <array>

namespace tos {

/**
 * Stratum V2 Message Types
 */
namespace StratumV2MessageType {
    // Common messages (0x00-0x0F)
    constexpr uint8_t SetupConnection        = 0x00;
    constexpr uint8_t SetupConnectionSuccess = 0x01;
    constexpr uint8_t SetupConnectionError   = 0x02;
    constexpr uint8_t ChannelEndpointChanged = 0x03;

    // Mining Protocol messages (0x10-0x1F)
    constexpr uint8_t OpenStandardMiningChannel        = 0x10;
    constexpr uint8_t OpenStandardMiningChannelSuccess = 0x11;
    constexpr uint8_t OpenExtendedMiningChannel        = 0x12;
    constexpr uint8_t OpenExtendedMiningChannelSuccess = 0x13;
    constexpr uint8_t OpenMiningChannelError           = 0x14;
    constexpr uint8_t UpdateChannel                    = 0x16;
    constexpr uint8_t UpdateChannelError               = 0x17;
    constexpr uint8_t CloseChannel                     = 0x18;

    // Mining messages (0x1E-0x2F)
    constexpr uint8_t SetExtranoncePrefix = 0x1E;
    constexpr uint8_t SubmitSharesStandard = 0x1F;
    constexpr uint8_t SubmitSharesExtended = 0x20;
    constexpr uint8_t SubmitSharesSuccess  = 0x21;
    constexpr uint8_t SubmitSharesError    = 0x22;
    constexpr uint8_t NewMiningJob         = 0x23;
    constexpr uint8_t NewExtendedMiningJob = 0x24;
    constexpr uint8_t SetNewPrevHash       = 0x25;
    constexpr uint8_t SetTarget            = 0x26;
    constexpr uint8_t SetGroupChannel      = 0x27;
    constexpr uint8_t Reconnect            = 0x28;
}

/**
 * Stratum V2 Frame Header
 *
 * All V2 messages start with:
 * - 2 bytes: extension type (for now, always 0)
 * - 1 byte: message type
 * - 3 bytes: message length
 */
struct StratumV2Header {
    uint16_t extensionType{0};
    uint8_t messageType{0};
    uint32_t messageLength{0};  // Only 24 bits used

    static constexpr size_t HEADER_SIZE = 6;

    // Serialize header to bytes
    std::array<uint8_t, HEADER_SIZE> serialize() const {
        std::array<uint8_t, HEADER_SIZE> bytes;
        bytes[0] = static_cast<uint8_t>(extensionType & 0xFF);
        bytes[1] = static_cast<uint8_t>((extensionType >> 8) & 0xFF);
        bytes[2] = messageType;
        bytes[3] = static_cast<uint8_t>(messageLength & 0xFF);
        bytes[4] = static_cast<uint8_t>((messageLength >> 8) & 0xFF);
        bytes[5] = static_cast<uint8_t>((messageLength >> 16) & 0xFF);
        return bytes;
    }

    // Parse header from bytes
    static StratumV2Header parse(const uint8_t* data) {
        StratumV2Header hdr;
        hdr.extensionType = static_cast<uint16_t>(data[0]) |
                           (static_cast<uint16_t>(data[1]) << 8);
        hdr.messageType = data[2];
        hdr.messageLength = static_cast<uint32_t>(data[3]) |
                           (static_cast<uint32_t>(data[4]) << 8) |
                           (static_cast<uint32_t>(data[5]) << 16);
        return hdr;
    }
};

/**
 * Stratum V2 SetupConnection message
 */
struct SetupConnectionMsg {
    uint8_t protocol{0};            // 0 = mining protocol
    uint16_t minVersion{2};
    uint16_t maxVersion{2};
    uint32_t flags{0};
    std::string endpoint;           // Pool endpoint URL
    std::string vendor;             // Miner vendor name
    std::string hardwareVersion;    // Hardware version
    std::string firmwareVersion;    // Firmware/software version
    std::string deviceId;           // Unique device ID

    std::vector<uint8_t> serialize() const;
    static SetupConnectionMsg parse(const uint8_t* data, size_t len);
};

/**
 * Stratum V2 NewMiningJob message
 */
struct NewMiningJobMsg {
    uint32_t channelId{0};
    uint32_t jobId{0};
    bool futureJob{false};
    uint32_t version{0};
    std::array<uint8_t, 32> prevHash;
    uint32_t minNtime{0};
    uint32_t nbits{0};
    std::vector<uint8_t> coinbase;

    static NewMiningJobMsg parse(const uint8_t* data, size_t len);
};

/**
 * Stratum V2 SubmitShares message
 */
struct SubmitSharesMsg {
    uint32_t channelId{0};
    uint32_t sequenceNumber{0};
    uint32_t jobId{0};
    uint64_t nonce{0};
    uint32_t ntime{0};
    uint32_t version{0};

    std::vector<uint8_t> serialize() const;
};

/**
 * Stratum V2 Protocol Handler
 *
 * This class provides the framework for Stratum V2 communication.
 * Full implementation requires:
 * - Noise protocol handshake
 * - Binary message parsing/serialization
 * - Channel management
 */
class StratumV2Handler {
public:
    /**
     * Check if Stratum V2 is supported in this build
     */
    static bool isSupported() {
        // V2 requires noise protocol which needs additional libraries
        return false;  // Not fully implemented yet
    }

    /**
     * Create SetupConnection message
     */
    static std::vector<uint8_t> createSetupConnection(
        const std::string& endpoint,
        const std::string& vendor,
        const std::string& version
    );

    /**
     * Create OpenStandardMiningChannel message
     */
    static std::vector<uint8_t> createOpenMiningChannel(
        const std::string& user,
        uint64_t nominalHashrate
    );

    /**
     * Create SubmitSharesStandard message
     */
    static std::vector<uint8_t> createSubmitShare(
        uint32_t channelId,
        uint32_t jobId,
        uint64_t nonce,
        uint32_t ntime
    );

    /**
     * Parse incoming V2 message
     *
     * @param data Message data
     * @param len Message length
     * @return Message type, or -1 on error
     */
    static int parseMessage(const uint8_t* data, size_t len);
};

}  // namespace tos
