#include <mbedtls/aes.h>
#include <string.h>

#include "mesh_codec.h"

namespace mesh {

void aesCtr(const uint8_t* key, size_t keyLen, const uint8_t nonce[16], uint8_t* buf, size_t len) {
  if (keyLen == 0 || len == 0) return;
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, keyLen * 8);
  uint8_t counter[16];
  uint8_t stream[16];
  size_t offset = 0;
  memcpy(counter, nonce, sizeof(counter));
  mbedtls_aes_crypt_ctr(&ctx, len, &offset, counter, stream, buf, buf);
  mbedtls_aes_free(&ctx);
}

}  // namespace mesh
