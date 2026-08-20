#include "RuntimeCrypto.h"

#include <switch.h>

#include <mbedtls/aes.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha512.h>

#include <array>
#include <cstring>
#include <vector>

namespace playground::runtime {
namespace {

constexpr char PublicKey[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDE0RNd6047aeBirzVb61DolatY\n"
    "YWpaEUIPugOIkobHDc9qVR5iliMLyC0ErXO1siLBwN+U3zaDVOa5uhXbiS7uYq5c\n"
    "cpxComxTnZtcn/b+mKDpYWLaC0Gv7UoiT8rpNqN3Vko645usz9OFc4VciijsHGRP\n"
    "XmmmoP6qykfI/vba8wIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

int randomCallback(void *, unsigned char *output, std::size_t size) {
  return R_SUCCEEDED(csrngGetRandomBytes(output, size)) ? 0 : -1;
}

bool loadPublicKey(mbedtls_pk_context &key) {
  mbedtls_pk_init(&key);
  return mbedtls_pk_parse_public_key(
             &key, reinterpret_cast<const unsigned char *>(PublicKey),
             sizeof(PublicKey)) == 0;
}

} // namespace

bool cryptoRandom(unsigned char *output, std::size_t size) {
  return output && (size == 0 || R_SUCCEEDED(csrngGetRandomBytes(output, size)));
}

bool cryptoSha1(const void *input, std::size_t size, unsigned char output[20]) {
  return mbedtls_sha1_ret(static_cast<const unsigned char *>(input), size,
                          output) == 0;
}

bool cryptoSha512(const void *input, std::size_t size,
                  unsigned char output[64]) {
  return mbedtls_sha512_ret(static_cast<const unsigned char *>(input), size,
                            output, 0) == 0;
}

bool cryptoPublicKeyVerify(const unsigned char *message,
                           std::size_t messageLength,
                           const unsigned char *signature,
                           std::size_t signatureLength) {
  mbedtls_pk_context key;
  if (!loadPublicKey(key)) {
    mbedtls_pk_free(&key);
    return false;
  }
  unsigned char digest[20];
  const bool valid = cryptoSha1(message, messageLength, digest) &&
                     mbedtls_pk_verify(&key, MBEDTLS_MD_SHA1, digest,
                                       sizeof(digest), signature,
                                       signatureLength) == 0;
  mbedtls_pk_free(&key);
  return valid;
}

int cryptoPublicKeyEncrypt(const unsigned char *input, std::size_t inputLength,
                           unsigned char *output, std::size_t outputLength) {
  mbedtls_pk_context key;
  if (!loadPublicKey(key)) {
    mbedtls_pk_free(&key);
    return -1;
  }
  std::size_t written = 0;
  const int result = mbedtls_pk_encrypt(&key, input, inputLength, output,
                                        &written, outputLength,
                                        randomCallback, nullptr);
  mbedtls_pk_free(&key);
  return result == 0 ? static_cast<int>(written) : -1;
}

int cryptoEncryptAes128Cbc(unsigned char *output, std::size_t outputLength,
                           const unsigned char *input, std::size_t inputLength,
                           const unsigned char key[16]) {
  const std::size_t padding = 16 - inputLength % 16;
  const std::size_t encryptedLength = inputLength + padding;
  if (outputLength < 16 + encryptedLength)
    return -8;
  if (!cryptoRandom(output, 16))
    return -1;
  std::vector<unsigned char> padded(encryptedLength,
                                    static_cast<unsigned char>(padding));
  std::memcpy(padded.data(), input, inputLength);
  std::array<unsigned char, 16> iv;
  std::memcpy(iv.data(), output, iv.size());
  mbedtls_aes_context context;
  mbedtls_aes_init(&context);
  const bool valid = mbedtls_aes_setkey_enc(&context, key, 128) == 0 &&
                     mbedtls_aes_crypt_cbc(
                         &context, MBEDTLS_AES_ENCRYPT, encryptedLength,
                         iv.data(), padded.data(), output + 16) == 0;
  mbedtls_aes_free(&context);
  return valid ? static_cast<int>(16 + encryptedLength) : -7;
}

int cryptoDecryptAes128Cbc(unsigned char *output, std::size_t outputLength,
                           const unsigned char *input, std::size_t inputLength,
                           const unsigned char key[16]) {
  if (inputLength < 32 || (inputLength - 16) % 16)
    return -1;
  const std::size_t encryptedLength = inputLength - 16;
  if (outputLength < encryptedLength)
    return -8;
  std::array<unsigned char, 16> iv;
  std::memcpy(iv.data(), input, iv.size());
  mbedtls_aes_context context;
  mbedtls_aes_init(&context);
  const bool decrypted = mbedtls_aes_setkey_dec(&context, key, 128) == 0 &&
                         mbedtls_aes_crypt_cbc(
                             &context, MBEDTLS_AES_DECRYPT, encryptedLength,
                             iv.data(), input + 16, output) == 0;
  mbedtls_aes_free(&context);
  if (!decrypted)
    return -7;
  const unsigned char padding = output[encryptedLength - 1];
  if (!padding || padding > 16 || padding > encryptedLength)
    return -7;
  for (std::size_t index = encryptedLength - padding;
       index < encryptedLength; ++index) {
    if (output[index] != padding)
      return -7;
  }
  return static_cast<int>(encryptedLength - padding);
}

} // namespace playground::runtime
