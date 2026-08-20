#ifndef PLAYGROUND_RUNTIME_CRYPTO_H
#define PLAYGROUND_RUNTIME_CRYPTO_H

#include <cstddef>

namespace playground::runtime {

bool cryptoRandom(unsigned char *output, std::size_t size);
bool cryptoSha1(const void *input, std::size_t size, unsigned char output[20]);
bool cryptoSha512(const void *input, std::size_t size,
                  unsigned char output[64]);
bool cryptoPublicKeyVerify(const unsigned char *message,
                           std::size_t messageLength,
                           const unsigned char *signature,
                           std::size_t signatureLength);
int cryptoPublicKeyEncrypt(const unsigned char *input, std::size_t inputLength,
                           unsigned char *output, std::size_t outputLength);
int cryptoEncryptAes128Cbc(unsigned char *output, std::size_t outputLength,
                           const unsigned char *input, std::size_t inputLength,
                           const unsigned char key[16]);
int cryptoDecryptAes128Cbc(unsigned char *output, std::size_t outputLength,
                           const unsigned char *input, std::size_t inputLength,
                           const unsigned char key[16]);

} // namespace playground::runtime

#endif
