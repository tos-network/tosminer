/**
 * TOS Miner - Stratum V2 Protocol Implementation
 *
 * Basic framework for Stratum V2 support.
 * Full implementation would require:
 * - Noise protocol library for encryption
 * - Complete message parsing for all message types
 * - Channel state management
 */

#include "StratumV2.h"
#include "Version.h"
#include <cstring>

namespace tos {

// Helper: write string with length prefix
static void writeString(std::vector<uint8_t>& buf, const std::string& str) {
    // Stratum V2 uses STR0_255: 1 byte length + string bytes
    uint8_t len = static_cast<uint8_t>(std::min(str.size(), size_t(255)));
    buf.push_back(len);
    buf.insert(buf.end(), str.begin(), str.begin() + len);
}

// Helper: write u16 little endian
static void writeU16(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

// Helper: write u32 little endian
static void writeU32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

// Helper: write u64 little endian
static void writeU64(std::vector<uint8_t>& buf, uint64_t val) {
    for (int i = 0; i < 8; i++) {
        buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

std::vector<uint8_t> SetupConnectionMsg::serialize() const {
    std::vector<uint8_t> payload;

    // Protocol
    payload.push_back(protocol);

    // Min/max version
    writeU16(payload, minVersion);
    writeU16(payload, maxVersion);

    // Flags
    writeU32(payload, flags);

    // Strings
    writeString(payload, endpoint);
    writeString(payload, vendor);
    writeString(payload, hardwareVersion);
    writeString(payload, firmwareVersion);
    writeString(payload, deviceId);

    // Build full message with header
    std::vector<uint8_t> message;
    StratumV2Header hdr;
    hdr.messageType = StratumV2MessageType::SetupConnection;
    hdr.messageLength = static_cast<uint32_t>(payload.size());

    auto headerBytes = hdr.serialize();
    message.insert(message.end(), headerBytes.begin(), headerBytes.end());
    message.insert(message.end(), payload.begin(), payload.end());

    return message;
}

std::vector<uint8_t> SubmitSharesMsg::serialize() const {
    std::vector<uint8_t> payload;

    writeU32(payload, channelId);
    writeU32(payload, sequenceNumber);
    writeU32(payload, jobId);
    writeU64(payload, nonce);
    writeU32(payload, ntime);
    writeU32(payload, version);

    // Build full message with header
    std::vector<uint8_t> message;
    StratumV2Header hdr;
    hdr.messageType = StratumV2MessageType::SubmitSharesStandard;
    hdr.messageLength = static_cast<uint32_t>(payload.size());

    auto headerBytes = hdr.serialize();
    message.insert(message.end(), headerBytes.begin(), headerBytes.end());
    message.insert(message.end(), payload.begin(), payload.end());

    return message;
}

std::vector<uint8_t> StratumV2Handler::createSetupConnection(
    const std::string& endpoint,
    const std::string& vendor,
    const std::string& version
) {
    SetupConnectionMsg msg;
    msg.protocol = 0;  // Mining protocol
    msg.minVersion = 2;
    msg.maxVersion = 2;
    msg.flags = 0;
    msg.endpoint = endpoint;
    msg.vendor = vendor;
    msg.hardwareVersion = "";
    msg.firmwareVersion = version;
    msg.deviceId = "";

    return msg.serialize();
}

std::vector<uint8_t> StratumV2Handler::createOpenMiningChannel(
    const std::string& user,
    uint64_t nominalHashrate
) {
    std::vector<uint8_t> payload;

    // Request ID (client-chosen)
    writeU32(payload, 1);

    // User identity
    writeString(payload, user);

    // Nominal hash rate (H/s)
    // Actually this is float in V2, but for simplicity using u32
    writeU32(payload, static_cast<uint32_t>(nominalHashrate));

    // Max target (32 bytes of 0xFF = accept any target)
    for (int i = 0; i < 32; i++) {
        payload.push_back(0xFF);
    }

    // Build full message
    std::vector<uint8_t> message;
    StratumV2Header hdr;
    hdr.messageType = StratumV2MessageType::OpenStandardMiningChannel;
    hdr.messageLength = static_cast<uint32_t>(payload.size());

    auto headerBytes = hdr.serialize();
    message.insert(message.end(), headerBytes.begin(), headerBytes.end());
    message.insert(message.end(), payload.begin(), payload.end());

    return message;
}

std::vector<uint8_t> StratumV2Handler::createSubmitShare(
    uint32_t channelId,
    uint32_t jobId,
    uint64_t nonce,
    uint32_t ntime
) {
    SubmitSharesMsg msg;
    msg.channelId = channelId;
    msg.sequenceNumber = 0;  // Will be set by caller
    msg.jobId = jobId;
    msg.nonce = nonce;
    msg.ntime = ntime;
    msg.version = 0;

    return msg.serialize();
}

int StratumV2Handler::parseMessage(const uint8_t* data, size_t len) {
    if (len < StratumV2Header::HEADER_SIZE) {
        return -1;
    }

    auto header = StratumV2Header::parse(data);

    // Validate length
    if (len < StratumV2Header::HEADER_SIZE + header.messageLength) {
        return -1;
    }

    return header.messageType;
}

}  // namespace tos
