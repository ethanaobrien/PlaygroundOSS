/*
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "CKLBCrypto.h"

#include "assert_klb.h"
#include "hash_sha1.h"

#include <stdio.h>
#include <string.h>

bool
cryptoSHA1(unsigned char* output, const char* input,
	int inputLength, int outputLength)
{
	SHA1Context context;
	SHA1Reset(&context);
	SHA1Input(&context, reinterpret_cast<const unsigned char*>(input), inputLength);
	if (!SHA1Result(&context)) {
		return false;
	}

	unsigned char digest[20];
	digest[0]  = static_cast<unsigned char>(context.Message_Digest[0] >> 24);
	digest[1]  = static_cast<unsigned char>(context.Message_Digest[0] >> 16);
	digest[2]  = static_cast<unsigned char>(context.Message_Digest[0] >> 8);
	digest[3]  = static_cast<unsigned char>(context.Message_Digest[0]);
	digest[4]  = static_cast<unsigned char>(context.Message_Digest[1] >> 24);
	digest[5]  = static_cast<unsigned char>(context.Message_Digest[1] >> 16);
	digest[6]  = static_cast<unsigned char>(context.Message_Digest[1] >> 8);
	digest[7]  = static_cast<unsigned char>(context.Message_Digest[1]);
	digest[8]  = static_cast<unsigned char>(context.Message_Digest[2] >> 24);
	digest[9]  = static_cast<unsigned char>(context.Message_Digest[2] >> 16);
	digest[10] = static_cast<unsigned char>(context.Message_Digest[2] >> 8);
	digest[11] = static_cast<unsigned char>(context.Message_Digest[2]);
	digest[12] = static_cast<unsigned char>(context.Message_Digest[3] >> 24);
	digest[13] = static_cast<unsigned char>(context.Message_Digest[3] >> 16);
	digest[14] = static_cast<unsigned char>(context.Message_Digest[3] >> 8);
	digest[15] = static_cast<unsigned char>(context.Message_Digest[3]);
	digest[16] = static_cast<unsigned char>(context.Message_Digest[4] >> 24);
	digest[17] = static_cast<unsigned char>(context.Message_Digest[4] >> 16);
	digest[18] = static_cast<unsigned char>(context.Message_Digest[4] >> 8);
	digest[19] = static_cast<unsigned char>(context.Message_Digest[4]);

	if (outputLength != 20) {
		if (outputLength == 40) {
			sprintf(reinterpret_cast<char*>(output), "%02x", digest[0]);
			for (int index = 1; index < 20; ++index) {
				output += strlen(reinterpret_cast<char*>(output));
				sprintf(reinterpret_cast<char*>(output), "%02x", digest[index]);
			}
			return true;
		}
		klb_assertNull(false, "sha1 buf size must be 20 or 40");
		return false;
	}
	memcpy(output, digest, sizeof(digest));
	return true;
}

bool
cryptoHmacSHA1(unsigned char* output, const char* input,
	size_t inputLength, const char* key, size_t keyLength)
{
	unsigned char* innerMessage = new unsigned char[inputLength + 64];
	unsigned char innerPadding[64];
	unsigned char outerPadding[64];
	unsigned char normalizedKey[64] = { 0 };
	unsigned char innerDigest[65];
	unsigned char outerMessage[100];

	memset(innerPadding, 0x36, sizeof(innerPadding));
	memset(outerPadding, 0x5c, sizeof(outerPadding));
	if (keyLength > sizeof(normalizedKey)) {
		cryptoSHA1(normalizedKey, key, keyLength, 20);
	} else {
		memcpy(normalizedKey, key, keyLength);
	}

	for (int index = 0; index < 64; ++index) {
		innerPadding[index] ^= normalizedKey[index];
	}

	memcpy(innerMessage, innerPadding, 64);
	memcpy(innerMessage + 64, input, inputLength);

	const size_t innerMessageLength = inputLength + 64;
	cryptoSHA1(innerDigest, reinterpret_cast<const char*>(innerMessage), innerMessageLength, 20);

	for (int index = 0; index < 64; ++index) {
		outerPadding[index] ^= normalizedKey[index];
	}
	memcpy(outerMessage, outerPadding, 64);
	memcpy(outerMessage + 64, innerDigest, 20);
	const size_t outerMessageLength = 64 + 20;
	cryptoSHA1(output, reinterpret_cast<const char*>(outerMessage), outerMessageLength, 20);
	return true;
}
