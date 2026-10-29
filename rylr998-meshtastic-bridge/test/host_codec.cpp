// Host-side driver for test/verify_codec.py. Builds against OpenSSL instead
// of mbedtls so the codec can be checked without an ESP32.
//   host_codec encode <out>   write a text-message ServiceEnvelope
//   host_codec decode <in>    decode an envelope and print its fields
#include <openssl/evp.h>
#include <stdio.h>

#include <fstream>
#include <iterator>
#include <string>

#include "../src/mesh_codec.h"

namespace mesh {
void aesCtr(const uint8_t* key, size_t keyLen, const uint8_t nonce[16], uint8_t* buf, size_t len) {
  if (keyLen == 0 || len == 0) return;
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  EVP_EncryptInit_ex(ctx, keyLen == 32 ? EVP_aes_256_ctr() : EVP_aes_128_ctr(), nullptr, key, nonce);
  int outLen = 0;
  EVP_EncryptUpdate(ctx, buf, &outLen, buf, int(len));
  EVP_CIPHER_CTX_free(ctx);
}
}  // namespace mesh

int main(int argc, char** argv) {
  if (argc != 5) {
    fprintf(stderr, "usage: %s encode|decode <file> <channel> <psk>\n", argv[0]);
    return 2;
  }
  std::string mode = argv[1];
  mesh::Channel ch;
  if (!mesh::initChannel(ch, argv[3], argv[4])) return 3;
  printf("hash=%u\n", ch.hash);

  if (mode == "encode") {
    mesh::Packet p;
    p.from = 0x1234abcd;
    p.id = 0xdeadbeef;
    p.hopLimit = p.hopStart = 3;
    p.portnum = mesh::PORT_TEXT_MESSAGE;
    p.payload = "R12: hello from the RYLR998 network, with commas, and utf-8 \xc3\xa9";
    p.hasBitfield = true;
    p.bitfield = mesh::BITFIELD_OK_TO_MQTT;
    std::ofstream(argv[2], std::ios::binary) << mesh::encodeEnvelope(ch, p, "!1234abcd");
    return 0;
  }

  std::ifstream in(argv[2], std::ios::binary);
  std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  mesh::Packet p;
  std::string channelId, gatewayId;
  mesh::DecodeResult r = mesh::decodeEnvelope(buf, ch, p, channelId, gatewayId);
  printf("result=%d from=%u to=%u id=%u hop_limit=%u port=%u channel_id=%s gateway=%s\n", int(r), p.from,
         p.to, p.id, p.hopLimit, p.portnum, channelId.c_str(), gatewayId.c_str());
  if (p.portnum == mesh::PORT_NODEINFO) {
    std::string longName, shortName;
    mesh::decodeUser(p.payload, longName, shortName);
    printf("long=%s short=%s\n", longName.c_str(), shortName.c_str());
  } else {
    printf("payload=%s\n", p.payload.c_str());
  }
  return 0;
}
