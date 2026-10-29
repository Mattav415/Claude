// Minimal Meshtastic MQTT codec: just enough protobuf + AES-CTR to send and
// receive text messages and NodeInfo as a "virtual node" on an MQTT channel.
// Field numbers follow meshtastic/protobufs (mqtt.proto, mesh.proto).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace mesh {

constexpr uint32_t BROADCAST = 0xFFFFFFFF;
constexpr uint32_t PORT_TEXT_MESSAGE = 1;
constexpr uint32_t PORT_NODEINFO = 4;
constexpr uint32_t HW_PRIVATE = 255;
// Data.bitfield bit 0: sender allows the packet to be uplinked to MQTT.
constexpr uint32_t BITFIELD_OK_TO_MQTT = 1;

struct Channel {
  std::string name;
  uint8_t key[32];
  size_t keyLen = 0;  // 0 = unencrypted, 16 = AES-128, 32 = AES-256
  uint8_t hash = 0;   // MeshPacket.channel for encrypted packets
};

struct Packet {
  uint32_t from = 0;
  uint32_t to = BROADCAST;
  uint32_t id = 0;
  uint32_t hopLimit = 0;
  uint32_t hopStart = 0;
  bool wantAck = false;
  bool viaMqtt = false;
  uint32_t portnum = 0;
  std::string payload;
  bool hasBitfield = false;
  uint32_t bitfield = 0;
};

enum class DecodeResult { OK, MALFORMED, OTHER_CHANNEL, UNDECRYPTABLE };

// Expands a base64 PSK the way the firmware does ("AQ==" = default key,
// one byte N = default key with the last byte bumped by N-1, "AA==" = none).
bool initChannel(Channel& ch, const std::string& name, const std::string& pskBase64);

// Serialized ServiceEnvelope carrying an encrypted MeshPacket.
std::string encodeEnvelope(const Channel& ch, const Packet& p, const std::string& gatewayId);

DecodeResult decodeEnvelope(const std::string& buf, const Channel& ch, Packet& out,
                            std::string& channelId, std::string& gatewayId);

std::string encodeUser(const std::string& id, const std::string& longName,
                       const std::string& shortName, uint32_t hwModel);
bool decodeUser(const std::string& buf, std::string& longName, std::string& shortName);

// AES-CTR in place with a 16-byte initial counter block. Implemented per
// platform (mbedtls on the ESP32, OpenSSL in the host test).
void aesCtr(const uint8_t* key, size_t keyLen, const uint8_t nonce[16], uint8_t* buf, size_t len);

}  // namespace mesh
