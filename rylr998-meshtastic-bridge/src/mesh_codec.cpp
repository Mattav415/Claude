#include "mesh_codec.h"

#include <string.h>

namespace mesh {
namespace {

const uint8_t kDefaultKey[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

enum WireType : uint32_t { VARINT = 0, FIXED64 = 1, LEN = 2, FIXED32 = 5 };

class Writer {
 public:
  void varint(uint64_t v) {
    while (v >= 0x80) {
      buf += char((v & 0x7f) | 0x80);
      v >>= 7;
    }
    buf += char(v);
  }
  void tag(uint32_t field, WireType t) { varint((uint64_t(field) << 3) | t); }
  void u32(uint32_t field, uint32_t v) {
    tag(field, VARINT);
    varint(v);
  }
  void fixed32(uint32_t field, uint32_t v) {
    tag(field, FIXED32);
    for (int i = 0; i < 4; i++) buf += char((v >> (8 * i)) & 0xff);
  }
  void bytes(uint32_t field, const std::string& v) {
    tag(field, LEN);
    varint(v.size());
    buf += v;
  }

  std::string buf;
};

class Reader {
 public:
  explicit Reader(const std::string& s)
      : p_(reinterpret_cast<const uint8_t*>(s.data())), end_(p_ + s.size()) {}

  // Reads the next field. Returns false at the end of input or on malformed
  // data; ok() tells the two apart.
  bool next(uint32_t& field, uint32_t& wire, uint64_t& num, std::string& bytes) {
    if (p_ >= end_) return false;
    uint64_t key;
    if (!varint(key)) return fail();
    field = uint32_t(key >> 3);
    wire = uint32_t(key & 7);
    if (field == 0) return fail();
    switch (wire) {
      case VARINT:
        if (!varint(num)) return fail();
        break;
      case FIXED64:
        if (!little(8, num)) return fail();
        break;
      case FIXED32:
        if (!little(4, num)) return fail();
        break;
      case LEN: {
        uint64_t n;
        if (!varint(n) || n > uint64_t(end_ - p_)) return fail();
        bytes.assign(reinterpret_cast<const char*>(p_), size_t(n));
        p_ += n;
        break;
      }
      default:
        return fail();
    }
    return true;
  }
  bool ok() const { return ok_; }

 private:
  bool varint(uint64_t& v) {
    v = 0;
    for (int shift = 0; shift < 64 && p_ < end_; shift += 7) {
      uint8_t b = *p_++;
      v |= uint64_t(b & 0x7f) << shift;
      if (!(b & 0x80)) return true;
    }
    return false;
  }
  bool little(int n, uint64_t& v) {
    if (end_ - p_ < n) return false;
    v = 0;
    for (int i = 0; i < n; i++) v |= uint64_t(p_[i]) << (8 * i);
    p_ += n;
    return true;
  }
  bool fail() {
    ok_ = false;
    return false;
  }

  const uint8_t* p_;
  const uint8_t* end_;
  bool ok_ = true;
};

bool base64Decode(const std::string& in, std::string& out) {
  out.clear();
  uint32_t acc = 0;
  int bits = 0;
  for (char c : in) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+' || c == '-') v = 62;
    else if (c == '/' || c == '_') v = 63;
    else if (c == '=' || c == ' ' || c == '\r' || c == '\n') continue;
    else return false;
    acc = (acc << 6) | uint32_t(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += char((acc >> bits) & 0xff);
    }
  }
  return true;
}

// Nonce layout from the firmware's CryptoEngine: packet id as a little-endian
// uint64, then the sender node number, then a zero block counter.
void crypt(const Channel& ch, uint32_t id, uint32_t from, std::string& buf) {
  if (ch.keyLen == 0 || buf.empty()) return;
  uint8_t nonce[16] = {0};
  for (int i = 0; i < 4; i++) {
    nonce[i] = uint8_t(id >> (8 * i));
    nonce[8 + i] = uint8_t(from >> (8 * i));
  }
  aesCtr(ch.key, ch.keyLen, nonce, reinterpret_cast<uint8_t*>(&buf[0]), buf.size());
}

std::string encodeData(const Packet& p) {
  Writer w;
  w.u32(1, p.portnum);
  w.bytes(2, p.payload);
  if (p.hasBitfield) w.u32(9, p.bitfield);
  return w.buf;
}

bool decodeData(const std::string& buf, Packet& out) {
  Reader r(buf);
  uint32_t field, wire;
  uint64_t num;
  std::string bytes;
  out.portnum = 0;
  out.payload.clear();
  out.hasBitfield = false;
  while (r.next(field, wire, num, bytes)) {
    if (field == 1 && wire == VARINT) out.portnum = uint32_t(num);
    else if (field == 2 && wire == LEN) out.payload = bytes;
    else if (field == 9 && wire == VARINT) {
      out.hasBitfield = true;
      out.bitfield = uint32_t(num);
    }
  }
  // CTR mode has no authentication; a wrong key shows up as garbage here.
  return r.ok() && out.portnum != 0;
}

}  // namespace

bool initChannel(Channel& ch, const std::string& name, const std::string& pskBase64) {
  std::string k;
  if (!base64Decode(pskBase64, k)) return false;
  ch.name = name;
  memset(ch.key, 0, sizeof(ch.key));
  if (k.size() == 1) {
    uint8_t index = uint8_t(k[0]);
    if (index == 0) {
      ch.keyLen = 0;
    } else {
      memcpy(ch.key, kDefaultKey, 16);
      ch.key[15] = uint8_t(ch.key[15] + index - 1);
      ch.keyLen = 16;
    }
  } else if (k.empty()) {
    ch.keyLen = 0;
  } else if (k.size() <= 32) {
    // Short keys are zero-padded, as the firmware does.
    memcpy(ch.key, k.data(), k.size());
    ch.keyLen = k.size() <= 16 ? 16 : 32;
  } else {
    return false;
  }

  uint8_t h = 0;
  for (char c : name) h ^= uint8_t(c);
  for (size_t i = 0; i < ch.keyLen; i++) h ^= ch.key[i];
  ch.hash = h;
  return true;
}

std::string encodeEnvelope(const Channel& ch, const Packet& p, const std::string& gatewayId) {
  std::string data = encodeData(p);
  crypt(ch, p.id, p.from, data);

  Writer mp;
  mp.fixed32(1, p.from);
  mp.fixed32(2, p.to);
  mp.u32(3, ch.hash);
  mp.bytes(5, data);
  mp.fixed32(6, p.id);
  if (p.hopLimit) mp.u32(9, p.hopLimit);
  if (p.wantAck) mp.u32(10, 1);
  if (p.hopStart) mp.u32(15, p.hopStart);

  Writer env;
  env.bytes(1, mp.buf);
  env.bytes(2, ch.name);
  env.bytes(3, gatewayId);
  return env.buf;
}

DecodeResult decodeEnvelope(const std::string& buf, const Channel& ch, Packet& out,
                            std::string& channelId, std::string& gatewayId) {
  uint32_t field, wire;
  uint64_t num;
  std::string bytes, packet;
  bool hasPacket = false;
  channelId.clear();
  gatewayId.clear();

  Reader env(buf);
  while (env.next(field, wire, num, bytes)) {
    if (wire != LEN) continue;
    if (field == 1) {
      packet = bytes;
      hasPacket = true;
    } else if (field == 2) {
      channelId = bytes;
    } else if (field == 3) {
      gatewayId = bytes;
    }
  }
  if (!env.ok() || !hasPacket) return DecodeResult::MALFORMED;

  out = Packet();
  std::string decoded, encrypted;
  bool hasDecoded = false, hasEncrypted = false, pki = false;
  uint32_t channel = 0;
  Reader mp(packet);
  while (mp.next(field, wire, num, bytes)) {
    switch (field) {
      case 1: out.from = uint32_t(num); break;
      case 2: out.to = uint32_t(num); break;
      case 3: channel = uint32_t(num); break;
      case 4: decoded = bytes; hasDecoded = true; break;
      case 5: encrypted = bytes; hasEncrypted = true; break;
      case 6: out.id = uint32_t(num); break;
      case 9: out.hopLimit = uint32_t(num); break;
      case 10: out.wantAck = num != 0; break;
      case 14: out.viaMqtt = num != 0; break;
      case 15: out.hopStart = uint32_t(num); break;
      case 17: pki = num != 0; break;
    }
  }
  if (!mp.ok()) return DecodeResult::MALFORMED;

  if (hasDecoded) return decodeData(decoded, out) ? DecodeResult::OK : DecodeResult::MALFORMED;
  if (!hasEncrypted) return DecodeResult::MALFORMED;
  // PKI direct messages are encrypted to a node's private key; we can't read them.
  if (pki) return DecodeResult::UNDECRYPTABLE;
  if (channel != ch.hash) return DecodeResult::OTHER_CHANNEL;
  crypt(ch, out.id, out.from, encrypted);
  return decodeData(encrypted, out) ? DecodeResult::OK : DecodeResult::UNDECRYPTABLE;
}

std::string encodeUser(const std::string& id, const std::string& longName,
                       const std::string& shortName, uint32_t hwModel) {
  Writer w;
  w.bytes(1, id);
  w.bytes(2, longName);
  w.bytes(3, shortName);
  w.u32(5, hwModel);
  return w.buf;
}

bool decodeUser(const std::string& buf, std::string& longName, std::string& shortName) {
  uint32_t field, wire;
  uint64_t num;
  std::string bytes;
  Reader r(buf);
  while (r.next(field, wire, num, bytes)) {
    if (wire != LEN) continue;
    if (field == 2) longName = bytes;
    else if (field == 3) shortName = bytes;
  }
  return r.ok();
}

}  // namespace mesh
