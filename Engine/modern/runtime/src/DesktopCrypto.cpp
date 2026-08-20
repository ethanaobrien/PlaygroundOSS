#include "RuntimeCrypto.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

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

EVP_PKEY *loadPublicKey() {
  BIO *input = BIO_new_mem_buf(PublicKey, sizeof(PublicKey) - 1);
  if (!input)
    return nullptr;
  EVP_PKEY *key = PEM_read_bio_PUBKEY(input, nullptr, nullptr, nullptr);
  BIO_free(input);
  return key;
}

} // namespace

bool cryptoRandom(unsigned char *output, std::size_t size) {
  return output && (size == 0 || RAND_bytes(output, static_cast<int>(size)) == 1);
}

bool cryptoSha1(const void *input, std::size_t size, unsigned char output[20]) {
  return SHA1(static_cast<const unsigned char *>(input), size, output) != nullptr;
}

bool cryptoSha512(const void *input, std::size_t size,
                  unsigned char output[64]) {
  return SHA512(static_cast<const unsigned char *>(input), size, output) != nullptr;
}

bool cryptoPublicKeyVerify(const unsigned char *message,
                           std::size_t messageLength,
                           const unsigned char *signature,
                           std::size_t signatureLength) {
  EVP_PKEY *key = loadPublicKey();
  if (!key)
    return false;
  static const unsigned char prefix[] = {
      0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e,
      0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14};
  unsigned char digest[20];
  cryptoSha1(message, messageLength, digest);
  EVP_PKEY_CTX *context = EVP_PKEY_CTX_new(key, nullptr);
  size_t recoveredLength = 0;
  bool valid = context && EVP_PKEY_verify_recover_init(context) > 0 &&
               EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_PADDING) > 0 &&
               EVP_PKEY_verify_recover(context, nullptr, &recoveredLength,
                                       signature, signatureLength) > 0;
  std::vector<unsigned char> recovered(recoveredLength);
  valid = valid && EVP_PKEY_verify_recover(context, recovered.data(),
                                           &recoveredLength, signature,
                                           signatureLength) > 0 &&
          recoveredLength == sizeof(prefix) + sizeof(digest) &&
          std::memcmp(recovered.data(), prefix, sizeof(prefix)) == 0 &&
          std::memcmp(recovered.data() + sizeof(prefix), digest,
                      sizeof(digest)) == 0;
  EVP_PKEY_CTX_free(context);
  EVP_PKEY_free(key);
  return valid;
}

int cryptoPublicKeyEncrypt(const unsigned char *input, std::size_t inputLength,
                           unsigned char *output, std::size_t outputLength) {
  EVP_PKEY *key = loadPublicKey();
  if (!key)
    return -1;
  EVP_PKEY_CTX *context = EVP_PKEY_CTX_new(key, nullptr);
  size_t required = 0;
  bool valid = context && EVP_PKEY_encrypt_init(context) > 0 &&
               EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_PADDING) > 0 &&
               EVP_PKEY_encrypt(context, nullptr, &required, input,
                                inputLength) > 0 &&
               required <= outputLength &&
               EVP_PKEY_encrypt(context, output, &required, input,
                                inputLength) > 0;
  EVP_PKEY_CTX_free(context);
  EVP_PKEY_free(key);
  return valid ? static_cast<int>(required) : -1;
}

int cryptoEncryptAes128Cbc(unsigned char *output, std::size_t outputLength,
                           const unsigned char *input, std::size_t inputLength,
                           const unsigned char key[16]) {
  if (outputLength < inputLength + 32 || !cryptoRandom(output, 16))
    return outputLength < inputLength + 32 ? -8 : -1;
  EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
  int first = 0, last = 0;
  bool valid = context &&
               EVP_EncryptInit_ex(context, EVP_aes_128_cbc(), nullptr, key,
                                  output) == 1 &&
               EVP_EncryptUpdate(context, output + 16, &first, input,
                                 static_cast<int>(inputLength)) == 1 &&
               EVP_EncryptFinal_ex(context, output + 16 + first, &last) == 1;
  EVP_CIPHER_CTX_free(context);
  return valid ? 16 + first + last : -7;
}

int cryptoDecryptAes128Cbc(unsigned char *output, std::size_t outputLength,
                           const unsigned char *input, std::size_t inputLength,
                           const unsigned char key[16]) {
  if (inputLength < 16 || outputLength < inputLength - 16)
    return inputLength < 16 ? -1 : -8;
  EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
  int first = 0, last = 0;
  bool valid = context &&
               EVP_DecryptInit_ex(context, EVP_aes_128_cbc(), nullptr, key,
                                  input) == 1 &&
               EVP_DecryptUpdate(context, output, &first, input + 16,
                                 static_cast<int>(inputLength - 16)) == 1 &&
               EVP_DecryptFinal_ex(context, output + first, &last) == 1;
  EVP_CIPHER_CTX_free(context);
  return valid ? first + last : -7;
}

} // namespace playground::runtime
