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
// ---------------------------------------------------------------
#include <string.h>
#include "encryptFile.h"
#include "CKLBLuaEnv.h"

namespace NMAsset {
	typedef void (CDecryptBaseClass::*DecryptRoutine)(void*, u32);
	typedef void (CDecryptBaseClass::*PositionRoutine)(u32);
	typedef void (*InitializerRoutine)(CDecryptBaseClass*, const u8*, const char*);

	struct EncryptionDispatchRegistry {
		DecryptRoutine decryptors[61];
		PositionRoutine positioners[47];
		InitializerRoutine primaryInitializers[7];
		InitializerRoutine* primaryInitializerEnd;
		InitializerRoutine secondaryInitializers[8];
		InitializerRoutine* secondaryInitializerEnd;
		u8 md5ShiftAmounts[64];
	};

	typedef char DecryptRoutineSize[
		(sizeof(DecryptRoutine) == 0x10) ? 1 : -1];
	typedef char PositionRoutineSize[
		(sizeof(PositionRoutine) == 0x10) ? 1 : -1];
	typedef char EncryptionDispatchRegistrySize[
		(sizeof(EncryptionDispatchRegistry) == 0x788) ? 1 : -1];

	// ---------------------------------------------------------------
	//  Obfuscated constant storage
	//
	//  None of the constants the asset decryptor relies on is present in
	//  readable form in the shipped binary.  Each one is kept instead as a
	//  shuffled list of bit positions, and recovering the constants is a
	//  single pass over that list into a zeroed destination:
	//
	//      destination[code / 32] |= 1 << (code % 32)
	//
	//  so a strings or constant scan of the binary never sees the MD5 round
	//  table, the generator parameters or the embedded application key.
	//  Every list is expanded at most once, on first use, and the expanded
	//  table is published through a pointer into it.
	//
	//  The code lists below are data, not hand-written constants: they were
	//  mechanically extracted from the shipped binary by
	//  tooling/scripts/extract_encryption_tables.py, which records the
	//  address, element count and SHA-256 of each one so that every list can
	//  be regenerated and re-verified against the binary at any time.
	// ---------------------------------------------------------------

	// Expands to MD5's 64-entry round-constant table
	// (T[i] = floor(|sin(i + 1)| * 2^32), 0xD76AA478 first).
	// Mechanically extracted from the shipped binary (libGame.so,
	// x86_64) at address 0x4DFE10: 1050 u16 entries, 2100 bytes, SHA-256
	// 6470ab8f8919cc08e5b9fc7ff22eb4aa8e1d947b3eda4c136ead12bdba468f00.
	static const u16 s_md5RoundConstantCodes[1050] = {
		0x07D1, 0x0331, 0x0031, 0x0491, 0x05B1, 0x0671, 0x0271, 0x0631, 0x0211, 0x04D1, 0x03B1, 0x0551,
		0x0011, 0x03F1, 0x0691, 0x03D1, 0x06D1, 0x0391, 0x07F1, 0x0291, 0x04F1, 0x0251, 0x0791, 0x0411,
		0x0151, 0x02F1, 0x0371, 0x05D1, 0x00B1, 0x04B1, 0x07B1, 0x00F1, 0x0511, 0x01D0, 0x07D0, 0x0330,
		0x0030, 0x0750, 0x05B0, 0x0670, 0x04D0, 0x03B0, 0x0550, 0x0450, 0x0690, 0x03D0, 0x02D0, 0x06D0,
		0x0390, 0x0290, 0x0310, 0x04F0, 0x0430, 0x0790, 0x0150, 0x02F0, 0x0470, 0x00B0, 0x0070, 0x0610,
		0x0530, 0x0350, 0x0510, 0x07CF, 0x002F, 0x048F, 0x012F, 0x010F, 0x05AF, 0x066F, 0x026F, 0x062F,
		0x03AF, 0x000F, 0x02CF, 0x06CF, 0x038F, 0x058F, 0x07EF, 0x030F, 0x04EF, 0x042F, 0x022F, 0x02EF,
		0x00AF, 0x006F, 0x04AF, 0x06AF, 0x07AF, 0x00EF, 0x016F, 0x072F, 0x01CE, 0x07CE, 0x048E, 0x074E,
		0x012E, 0x01AE, 0x026E, 0x062E, 0x070E, 0x04CE, 0x044E, 0x03EE, 0x068E, 0x02CE, 0x06CE, 0x004E,
		0x038E, 0x058E, 0x07EE, 0x030E, 0x042E, 0x024E, 0x078E, 0x014E, 0x02EE, 0x05CE, 0x00AE, 0x00CE,
		0x006E, 0x06EE, 0x04AE, 0x06AE, 0x07AE, 0x016E, 0x050E, 0x072E, 0x05EE, 0x002D, 0x048D, 0x012D,
		0x01AD, 0x066D, 0x062D, 0x020D, 0x070D, 0x03AD, 0x054D, 0x000D, 0x044D, 0x02CD, 0x06CD, 0x004D,
		0x038D, 0x04ED, 0x042D, 0x022D, 0x078D, 0x040D, 0x02ED, 0x064D, 0x05CD, 0x046D, 0x060D, 0x052D,
		0x07AD, 0x050D, 0x072D, 0x076C, 0x07CC, 0x002C, 0x056C, 0x012C, 0x010C, 0x05AC, 0x01AC, 0x018C,
		0x062C, 0x070C, 0x054C, 0x068C, 0x06CC, 0x004C, 0x058C, 0x07EC, 0x028C, 0x04EC, 0x042C, 0x024C,
		0x022C, 0x078C, 0x040C, 0x014C, 0x02EC, 0x036C, 0x05CC, 0x046C, 0x02AC, 0x06EC, 0x07AC, 0x00EC,
		0x016C, 0x050C, 0x05EC, 0x01EB, 0x056B, 0x048B, 0x010B, 0x008B, 0x05AB, 0x062B, 0x070B, 0x04CB,
		0x03EB, 0x068B, 0x038B, 0x030B, 0x04EB, 0x024B, 0x078B, 0x040B, 0x014B, 0x02EB, 0x05CB, 0x046B,
		0x006B, 0x06EB, 0x04AB, 0x06AB, 0x034B, 0x050B, 0x032A, 0x002A, 0x056A, 0x012A, 0x008A, 0x026A,
		0x062A, 0x020A, 0x070A, 0x000A, 0x03EA, 0x02CA, 0x06CA, 0x030A, 0x04EA, 0x042A, 0x078A, 0x036A,
		0x05CA, 0x00AA, 0x00CA, 0x006A, 0x02AA, 0x052A, 0x06EA, 0x04AA, 0x06AA, 0x00EA, 0x016A, 0x034A,
		0x050A, 0x072A, 0x05EA, 0x01C9, 0x07C9, 0x0329, 0x0029, 0x0489, 0x0749, 0x0129, 0x0089, 0x0269,
		0x0629, 0x0709, 0x04C9, 0x03A9, 0x03C9, 0x02C9, 0x07E9, 0x0429, 0x0249, 0x0229, 0x0789, 0x0149,
		0x02E9, 0x0649, 0x00A9, 0x00C9, 0x0069, 0x0609, 0x0529, 0x04A9, 0x07A9, 0x0169, 0x0509, 0x0729,
		0x05E9, 0x0768, 0x01C8, 0x0328, 0x0028, 0x0568, 0x0748, 0x0128, 0x0088, 0x05A8, 0x01A8, 0x0188,
		0x0268, 0x0628, 0x0208, 0x04C8, 0x03A8, 0x0448, 0x0688, 0x0388, 0x07E8, 0x0308, 0x0228, 0x0408,
		0x0148, 0x02E8, 0x0648, 0x0528, 0x06E8, 0x04A8, 0x00E8, 0x0168, 0x0348, 0x0767, 0x01C7, 0x07C7,
		0x0327, 0x0127, 0x0107, 0x0087, 0x05A7, 0x01A7, 0x0267, 0x0627, 0x03A7, 0x0547, 0x03E7, 0x0687,
		0x03C7, 0x02C7, 0x0047, 0x07E7, 0x0307, 0x0427, 0x0787, 0x0147, 0x02E7, 0x0367, 0x0647, 0x05C7,
		0x0067, 0x0527, 0x06E7, 0x04A7, 0x06A7, 0x0167, 0x0347, 0x0507, 0x0727, 0x0326, 0x0026, 0x0486,
		0x0106, 0x05A6, 0x0206, 0x0706, 0x04C6, 0x03A6, 0x0006, 0x0686, 0x03C6, 0x06C6, 0x0046, 0x0286,
		0x0306, 0x04E6, 0x0246, 0x0226, 0x0406, 0x02E6, 0x0366, 0x05C6, 0x0066, 0x0606, 0x02A6, 0x0526,
		0x06E6, 0x0506, 0x0726, 0x05E6, 0x0765, 0x07C5, 0x01E5, 0x0125, 0x0085, 0x05A5, 0x0665, 0x0185,
		0x0265, 0x0205, 0x04C5, 0x03A5, 0x0005, 0x0445, 0x06C5, 0x0585, 0x0305, 0x04E5, 0x0145, 0x0365,
		0x0645, 0x05C5, 0x00A5, 0x0065, 0x0525, 0x04A5, 0x07A5, 0x0165, 0x0725, 0x05E5, 0x07C4, 0x0324,
		0x0024, 0x0744, 0x0104, 0x01A4, 0x0664, 0x0624, 0x03A4, 0x0004, 0x03C4, 0x06C4, 0x0044, 0x0584,
		0x07E4, 0x0284, 0x04E4, 0x0244, 0x0144, 0x05C4, 0x00C4, 0x02A4, 0x0524, 0x06E4, 0x06A4, 0x07A4,
		0x0164, 0x01C3, 0x07C3, 0x0123, 0x0103, 0x0083, 0x0663, 0x0263, 0x0703, 0x03A3, 0x0003, 0x03E3,
		0x03C3, 0x06C3, 0x0043, 0x0583, 0x0283, 0x02E3, 0x0363, 0x05C3, 0x0463, 0x00A3, 0x0063, 0x0523,
		0x04A3, 0x0163, 0x01C2, 0x0322, 0x0022, 0x0562, 0x0482, 0x0742, 0x0122, 0x0082, 0x05A2, 0x0622,
		0x0702, 0x0542, 0x06C2, 0x0382, 0x0282, 0x0302, 0x0362, 0x0642, 0x0462, 0x0062, 0x0602, 0x07A2,
		0x0162, 0x0342, 0x0502, 0x05E2, 0x01C1, 0x07C1, 0x0321, 0x0021, 0x0121, 0x0081, 0x01A1, 0x0181,
		0x0261, 0x0621, 0x0201, 0x0701, 0x0441, 0x03E1, 0x0681, 0x0041, 0x0301, 0x0781, 0x0401, 0x0641,
		0x00A1, 0x00C1, 0x0061, 0x02A1, 0x0521, 0x06A1, 0x0161, 0x0341, 0x0501, 0x0760, 0x07C0, 0x01E0,
		0x0560, 0x0120, 0x0080, 0x05A0, 0x01A0, 0x0660, 0x0620, 0x0700, 0x0540, 0x0680, 0x03C0, 0x02C0,
		0x06C0, 0x0040, 0x0380, 0x0580, 0x07E0, 0x0280, 0x0420, 0x0240, 0x0140, 0x0360, 0x0640, 0x00C0,
		0x02A0, 0x06E0, 0x04A0, 0x07A0, 0x00E0, 0x0340, 0x05E0, 0x01DF, 0x033F, 0x003F, 0x049F, 0x075F,
		0x013F, 0x009F, 0x05BF, 0x01BF, 0x067F, 0x027F, 0x021F, 0x04DF, 0x03BF, 0x055F, 0x001F, 0x03FF,
		0x02DF, 0x06DF, 0x039F, 0x059F, 0x07FF, 0x029F, 0x04FF, 0x043F, 0x023F, 0x079F, 0x041F, 0x015F,
		0x02FF, 0x065F, 0x047F, 0x00DF, 0x007F, 0x061F, 0x053F, 0x06FF, 0x06BF, 0x07BF, 0x00FF, 0x017F,
		0x035F, 0x073F, 0x05FF, 0x077E, 0x033E, 0x003E, 0x01FE, 0x011E, 0x009E, 0x05BE, 0x01BE, 0x067E,
		0x019E, 0x027E, 0x063E, 0x021E, 0x071E, 0x04DE, 0x03BE, 0x055E, 0x001E, 0x045E, 0x069E, 0x03DE,
		0x02DE, 0x06DE, 0x059E, 0x07FE, 0x029E, 0x023E, 0x079E, 0x041E, 0x015E, 0x02FE, 0x037E, 0x047E,
		0x00BE, 0x007E, 0x061E, 0x053E, 0x04BE, 0x00FE, 0x035E, 0x073E, 0x05FE, 0x01DD, 0x07DD, 0x003D,
		0x049D, 0x075D, 0x011D, 0x009D, 0x05BD, 0x01BD, 0x067D, 0x019D, 0x027D, 0x021D, 0x071D, 0x04DD,
		0x03BD, 0x045D, 0x069D, 0x03DD, 0x06DD, 0x005D, 0x039D, 0x07FD, 0x031D, 0x04FD, 0x025D, 0x079D,
		0x041D, 0x015D, 0x02FD, 0x065D, 0x047D, 0x00DD, 0x061D, 0x053D, 0x07BD, 0x00FD, 0x035D, 0x051D,
		0x073D, 0x009C, 0x01BC, 0x067C, 0x021C, 0x04DC, 0x03BC, 0x055C, 0x001C, 0x02DC, 0x06DC, 0x059C,
		0x029C, 0x04FC, 0x079C, 0x041C, 0x015C, 0x05DC, 0x047C, 0x061C, 0x07BC, 0x00FC, 0x035C, 0x073C,
		0x077B, 0x07DB, 0x003B, 0x01FB, 0x013B, 0x011B, 0x01BB, 0x067B, 0x019B, 0x027B, 0x071B, 0x03BB,
		0x045B, 0x03FB, 0x02DB, 0x06DB, 0x039B, 0x059B, 0x07FB, 0x04FB, 0x041B, 0x015B, 0x065B, 0x05DB,
		0x047B, 0x00DB, 0x053B, 0x04BB, 0x06BB, 0x07BB, 0x00FB, 0x017B, 0x051B, 0x073B, 0x077A, 0x01DA,
		0x057A, 0x049A, 0x009A, 0x05BA, 0x01BA, 0x067A, 0x021A, 0x071A, 0x04DA, 0x03BA, 0x055A, 0x001A,
		0x045A, 0x03FA, 0x069A, 0x03DA, 0x06DA, 0x005A, 0x029A, 0x04FA, 0x043A, 0x025A, 0x079A, 0x041A,
		0x015A, 0x02FA, 0x037A, 0x05DA, 0x047A, 0x00BA, 0x061A, 0x06FA, 0x06BA, 0x07BA, 0x00FA, 0x035A,
		0x073A, 0x05FA, 0x0779, 0x01D9, 0x07D9, 0x0339, 0x0759, 0x0139, 0x05B9, 0x0199, 0x0639, 0x0219,
		0x0719, 0x04D9, 0x0019, 0x03D9, 0x06D9, 0x07F9, 0x0299, 0x04F9, 0x0439, 0x0259, 0x0799, 0x0419,
		0x0159, 0x02F9, 0x0659, 0x05D9, 0x00B9, 0x02B9, 0x0539, 0x04B9, 0x06B9, 0x0739, 0x0338, 0x01F8,
		0x0758, 0x0138, 0x0118, 0x0098, 0x01B8, 0x0198, 0x0278, 0x0638, 0x0718, 0x0018, 0x0458, 0x03F8,
		0x0698, 0x03D8, 0x06D8, 0x0398, 0x0598, 0x07F8, 0x0318, 0x0438, 0x0798, 0x0418, 0x0158, 0x02F8,
		0x0378, 0x0658, 0x05D8, 0x0478, 0x00B8, 0x0078, 0x06F8, 0x04B8, 0x06B8, 0x07B8, 0x00F8, 0x0178,
		0x07D7, 0x0037, 0x01F7, 0x0577, 0x0497, 0x0117, 0x05B7, 0x01B7, 0x0677, 0x0197, 0x0277, 0x0717,
		0x04D7, 0x03B7, 0x0557, 0x0457, 0x02D7, 0x06D7, 0x0397, 0x0597, 0x07F7, 0x0317, 0x04F7, 0x0417,
		0x0157, 0x02F7, 0x0657, 0x05D7, 0x0477, 0x00B7, 0x0077, 0x0537, 0x06F7, 0x04B7, 0x0357, 0x0517,
		0x05F7, 0x01D6, 0x07D6, 0x0036, 0x0136, 0x0096, 0x05B6, 0x03B6, 0x0556, 0x0016, 0x0696, 0x03D6,
		0x06D6, 0x0396, 0x0596, 0x0316, 0x0436, 0x0256, 0x0236, 0x0796, 0x0416, 0x0156, 0x02F6, 0x0376,
		0x0476, 0x02B6, 0x04B6, 0x00F6, 0x0176, 0x0356, 0x01D5, 0x0335, 0x01F5, 0x0495, 0x0095, 0x0275,
		0x0635, 0x0715, 0x04D5, 0x03B5, 0x0555, 0x0015, 0x03F5, 0x03D5, 0x02D5, 0x06D5, 0x0055, 0x0395,
		0x0295, 0x0315, 0x04F5, 0x0435, 0x0415, 0x0155, 0x05D5, 0x0475, 0x00D5, 0x0075, 0x0615, 0x0535,
		0x07B5, 0x0735, 0x05F5, 0x01D4, 0x07D4, 0x0334, 0x01F4, 0x0494, 0x0094, 0x05B4, 0x01B4, 0x0674,
		0x0194, 0x0274, 0x0214, 0x04D4, 0x0454, 0x0694, 0x0594, 0x04F4, 0x0434, 0x0254, 0x0794, 0x0414,
		0x0154, 0x02F4, 0x0374, 0x0654, 0x00D4, 0x0074, 0x04B4, 0x07B4, 0x0174, 0x0354, 0x0514, 0x0773,
		0x01D3, 0x0573, 0x0493, 0x0093, 0x05B3, 0x01B3, 0x0633, 0x0213, 0x0713, 0x04D3, 0x03B3, 0x0553,
		0x0013, 0x0453, 0x03F3, 0x0693, 0x03D3, 0x06D3, 0x0293, 0x04F3, 0x0253, 0x0413, 0x0153, 0x0373,
		0x0073, 0x0613, 0x04B3, 0x06B3, 0x07B3, 0x0173, 0x0513, 0x0733, 0x05F3, 0x07D2, 0x0332, 0x0032,
		0x01F2, 0x0492, 0x0132, 0x0092, 0x0272, 0x0212, 0x03B2, 0x0552, 0x0452, 0x03D2, 0x06D2, 0x0592,
		0x07F2, 0x0292, 0x04F2, 0x0252, 0x0152, 0x0652, 0x0472, 0x00B2, 0x0072, 0x02B2, 0x06F2, 0x04B2,
		0x06B2, 0x00F2, 0x0172, 0x0352, 0x0732, 0x05F2,
	};

	// Expands to the four MD5 chaining-state words (0x67452301, 0xEFCDAB89,
	// 0x98BADCFE, 0x10325476) followed by the word count.
	// Mechanically extracted from the shipped binary (libGame.so,
	// x86_64) at address 0x4E0650: 66 u16 entries, 132 bytes, SHA-256
	// 4af1838b5bc9bcc3c8b00d8c31445833dba7aeed465d4bdfb76bd05d1f509bb6.
	static const u16 s_md5InitialStateCodes[66] = {
		0x0062, 0x0082, 0x0042, 0x0061, 0x0041, 0x0080, 0x0020, 0x0000, 0x003F, 0x005F, 0x003E, 0x001E,
		0x003D, 0x001D, 0x007C, 0x005C, 0x003B, 0x005B, 0x003A, 0x001A, 0x0039, 0x0019, 0x0038, 0x0018,
		0x0037, 0x0057, 0x0036, 0x0016, 0x0075, 0x0055, 0x0074, 0x0054, 0x0033, 0x0053, 0x0032, 0x0012,
		0x0071, 0x0051, 0x0030, 0x0010, 0x002F, 0x004F, 0x006E, 0x004E, 0x002D, 0x000D, 0x006C, 0x004C,
		0x002B, 0x004B, 0x006A, 0x004A, 0x0029, 0x0009, 0x0028, 0x0008, 0x0027, 0x0047, 0x0066, 0x0046,
		0x0065, 0x0045, 0x0064, 0x0044, 0x0023, 0x0043,
	};

	// Expands to MD5's 64 per-step left-rotation amounts
	// (7, 12, 17, 22 ... 6, 10, 15, 21).
	// Mechanically extracted from the shipped binary (libGame.so,
	// x86_64) at address 0x4E06E0: 156 u16 entries, 312 bytes, SHA-256
	// 7a298ea811e0e84b6ebea4544c87c788d62fdbfc77b097a47bbb3961e56f2dd4.
	static const u16 s_md5ShiftAmountCodes[156] = {
		0x0664, 0x0444, 0x05E4, 0x06E4, 0x01C4, 0x01E4, 0x0464, 0x0564, 0x0364, 0x0064, 0x0164, 0x05C4,
		0x0044, 0x07E4, 0x04C4, 0x0764, 0x0544, 0x00E4, 0x0264, 0x0144, 0x03E4, 0x00C4, 0x02E4, 0x04E4,
		0x0223, 0x0243, 0x01A3, 0x0023, 0x0743, 0x0423, 0x0343, 0x0123, 0x02C3, 0x06A3, 0x0623, 0x04A3,
		0x05A3, 0x07A3, 0x03C3, 0x02A3, 0x0643, 0x0723, 0x06C3, 0x07C3, 0x0323, 0x03A3, 0x0523, 0x00A3,
		0x0662, 0x0242, 0x01A2, 0x0022, 0x05E2, 0x06E2, 0x01E2, 0x0462, 0x0742, 0x0302, 0x0342, 0x0122,
		0x02C2, 0x0562, 0x0402, 0x0202, 0x0362, 0x0062, 0x0162, 0x0182, 0x03C2, 0x0702, 0x0602, 0x0382,
		0x0642, 0x0682, 0x06C2, 0x0582, 0x07C2, 0x07E2, 0x0002, 0x0502, 0x0762, 0x0482, 0x00E2, 0x0262,
		0x03E2, 0x0082, 0x0782, 0x0282, 0x02E2, 0x04E2, 0x0102, 0x00A2, 0x0241, 0x05E1, 0x01E1, 0x0461,
		0x0741, 0x0421, 0x0341, 0x02C1, 0x0561, 0x0061, 0x0161, 0x06A1, 0x0621, 0x04A1, 0x05A1, 0x0181,
		0x07A1, 0x03C1, 0x0701, 0x0601, 0x0641, 0x0721, 0x0681, 0x06C1, 0x07C1, 0x0001, 0x00E1, 0x0081,
		0x0781, 0x0521, 0x04E1, 0x0101, 0x0220, 0x0660, 0x05E0, 0x06E0, 0x01C0, 0x0460, 0x0740, 0x0420,
		0x0300, 0x0560, 0x0200, 0x04A0, 0x05A0, 0x0180, 0x0040, 0x02A0, 0x0380, 0x0640, 0x06C0, 0x07C0,
		0x07E0, 0x0000, 0x0760, 0x0320, 0x0140, 0x0080, 0x0280, 0x00C0, 0x03A0, 0x0520, 0x04E0, 0x0100,
	};

	// Expands to the Lehmer generator pair: modulus 0x7FFFFFFF (2^31 - 1)
	// then multiplier 16807, i.e. the MINSTD parameters.
	// Mechanically extracted from the shipped binary (libGame.so,
	// x86_64) at address 0x4E0820: 38 u16 entries, 76 bytes, SHA-256
	// bc5dba3711c5708ff186cb4ce8482cfe89b1ab1f7d3c1cde1160e52e11900d29.
	static const u16 s_minstdParameterCodes[38] = {
		0x000C, 0x000B, 0x000A, 0x0009, 0x0008, 0x0028, 0x0007, 0x0027, 0x0006, 0x0005, 0x0025, 0x0004,
		0x0003, 0x0002, 0x0022, 0x0001, 0x0021, 0x0000, 0x0020, 0x001E, 0x001D, 0x001C, 0x001B, 0x001A,
		0x0019, 0x0018, 0x0017, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E,
		0x002E, 0x000D,
	};

	// Expands to four {multiplier, increment, output shift} parameter sets for
	// the stream ciphers in encryptFileAlgorithms.cpp, then a spare
	// word: {1103515245, 12345, 15}, {22695477, 1, 23},
	// {214013, 2531011, 24}, {0x00010101, 0x00415927, 8}.
	// Mechanically extracted from the shipped binary (libGame.so,
	// x86_64) at address 0x4E0870: 84 u16 entries, 168 bytes, SHA-256
	// 531ee902a3e35ac34024dba4684526bde73e9d238ce55c73d6837f9e572b0e75.
	static const u16 s_streamCipherParameterCodes[84] = {
		0x0073, 0x00F2, 0x0012, 0x00F1, 0x00D1, 0x0071, 0x0011, 0x00D0, 0x0130, 0x0150, 0x00EF, 0x00CE,
		0x006E, 0x000E, 0x014E, 0x002D, 0x00EC, 0x002C, 0x014C, 0x00EB, 0x006B, 0x000B, 0x014B, 0x00EA,
		0x006A, 0x000A, 0x00E9, 0x00C9, 0x0069, 0x0009, 0x00C8, 0x0128, 0x0148, 0x00E7, 0x00C7, 0x00E6,
		0x00C6, 0x0006, 0x0025, 0x00C5, 0x0065, 0x0005, 0x0145, 0x0024, 0x00C4, 0x0064, 0x0104, 0x00A4,
		0x0023, 0x00C3, 0x0183, 0x0103, 0x0043, 0x0003, 0x0163, 0x00C2, 0x0062, 0x00A2, 0x0042, 0x0002,
		0x0142, 0x00E1, 0x00A1, 0x0041, 0x0141, 0x00E0, 0x0020, 0x00C0, 0x0060, 0x0120, 0x00A0, 0x0040,
		0x0000, 0x0140, 0x0080, 0x001E, 0x0078, 0x0018, 0x0017, 0x0076, 0x0016, 0x0156, 0x00F5, 0x0074,
	};

	// Expands to the embedded application key, one character per word: the
	// key length (32) first, then that many characters, then a trailing word.
	// Mechanically extracted from the shipped binary (libGame.so,
	// x86_64) at address 0x4E0920: 128 u16 entries, 256 bytes, SHA-256
	// 8765fc8786ed6d1e316d280796c08e205b5167257fe2c816f6b608393a186951.
	static const u16 s_applicationKeyCodes[128] = {
		0x0366, 0x00C6, 0x0346, 0x02C6, 0x0046, 0x0106, 0x01E6, 0x00A6, 0x0306, 0x0226, 0x02A6, 0x0026,
		0x0386, 0x02E6, 0x0146, 0x0406, 0x00E6, 0x03A6, 0x0166, 0x01C6, 0x0266, 0x0186, 0x03E6, 0x0246,
		0x0066, 0x0206, 0x0286, 0x0365, 0x00C5, 0x02C5, 0x0045, 0x0105, 0x01E5, 0x0325, 0x0305, 0x0225,
		0x02A5, 0x03C5, 0x0025, 0x0385, 0x02E5, 0x0145, 0x01A5, 0x0005, 0x0405, 0x00E5, 0x0165, 0x01C5,
		0x0085, 0x0265, 0x0125, 0x03E5, 0x0245, 0x0065, 0x0205, 0x0285, 0x0344, 0x0324, 0x03C4, 0x01A4,
		0x00E4, 0x03A4, 0x0084, 0x0124, 0x0184, 0x03E4, 0x0244, 0x0064, 0x0284, 0x0363, 0x00C3, 0x0043,
		0x0103, 0x01E3, 0x0303, 0x02A3, 0x03C3, 0x0383, 0x0403, 0x03A3, 0x0163, 0x01C3, 0x0263, 0x0183,
		0x03E3, 0x0203, 0x0362, 0x0322, 0x0222, 0x02A2, 0x0022, 0x0382, 0x02E2, 0x01A2, 0x0402, 0x01C2,
		0x0082, 0x0122, 0x0422, 0x0062, 0x0202, 0x0282, 0x0361, 0x0341, 0x02C1, 0x0321, 0x02A1, 0x0381,
		0x0401, 0x01C1, 0x0360, 0x0340, 0x0040, 0x00A0, 0x0300, 0x0220, 0x02A0, 0x03C0, 0x0020, 0x0380,
		0x02E0, 0x0140, 0x0400, 0x0160, 0x01C0, 0x0420, 0x0200, 0x0280,
	};

	// Element counts of the expanded tables, not of the code lists.
	static const s32 s_md5RoundConstantCount = 64;
	static const s32 s_md5InitialStateCount = 5;
	static const s32 s_md5ShiftAmountCount = 64;
	static const s32 s_minstdParameterCount = 2;
	static const s32 s_streamCipherParameterCount = 13;
	static const s32 s_applicationKeyWordCount = 34;

	// Expanded tables.  Each group is a "ready" flag, the pointer the rest of
	// the engine reads, and the storage the expansion writes into.  The MD5
	// round table is the only one held on the heap; releaseNMAsset frees it.
	bool g_md5RoundConstantsReady = false;
	const u32* g_md5RoundConstants = NULL;
	u32* g_md5RoundConstantStorage = NULL;

	bool g_md5InitialStateReady = false;
	const u32* g_md5InitialState = NULL;
	u32 g_md5InitialStateStorage[s_md5InitialStateCount];

	bool g_md5ShiftAmountsReady = false;
	const u8* g_md5ShiftAmounts = NULL;
	u8 g_md5ShiftAmountStorage[s_md5ShiftAmountCount];

	bool g_minstdParametersReady = false;
	const u32* g_minstdParameters = NULL;
	u32 g_minstdParameterStorage[s_minstdParameterCount];

	bool g_streamCipherParametersReady = false;
	const u32* g_streamCipherParameters = NULL;
	u32 g_streamCipherParameterStorage[s_streamCipherParameterCount];

	bool g_applicationKeyReady = false;
	const u32* g_applicationKeyWords = NULL;
	u32 g_applicationKeyStorage[s_applicationKeyWordCount];

	char* g_nmAssetKey = NULL;
	s32 g_nmAssetKeyLength = 0;
	const char* g_assetFilterCallback = NULL;
	EncryptionDispatchRegistry* g_encryptionRegistry = NULL;

	/*!
	    @brief  Expands one obfuscated code list into its constant table.

	    Clears the destination, then sets one bit per code.  The destination
	    element size varies per table (the MD5 rotation amounts are bytes,
	    every other table is 32-bit words), so it is passed in rather than
	    baked into a type.

	    @param[out] destination     table to build; cleared first
	    @param[in]  elementCount    number of elements in the destination
	    @param[in]  elementSize     size in bytes of one destination element
	    @param[in]  codes           shuffled bit positions to set
	    @param[in]  codeCount       number of bit positions
	 */
	void expandObfuscatedConstants(
		void* destination,
		s32 elementCount,
		s32 elementSize,
		const u16* codes,
		s32 codeCount)
	{
		memset(destination, 0, elementCount * elementSize);
		switch (elementSize) {
		case 2:
			{
				u16* words = static_cast<u16*>(destination);
				for (s32 index = 0; index < codeCount; ++index) {
					const u32 code = codes[index];
					const u32 word = code / 32;
					words[word] |= static_cast<u16>(1u << (code % 32));
				}
			}
			break;
		case 4:
			{
				u32* words = static_cast<u32*>(destination);
				for (s32 index = 0; index < codeCount; ++index) {
					const u32 code = codes[index];
					const u32 word = code / 32;
					words[word] |= 1u << (code % 32);
				}
			}
			break;
		case 1:
			{
				u8* bytes = static_cast<u8*>(destination);
				for (s32 index = 0; index < codeCount; ++index) {
					const u32 code = codes[index];
					const u32 word = code / 32;
					bytes[word] |= static_cast<u8>(1u << (code % 32));
				}
			}
			break;
		}
	}

	/*!
	    @brief  Builds the MD5 round-constant table on first use.
	    @param[in]  firstEntry  index within the table the published pointer
	                            refers to.
	 */
	void initializeMd5RoundConstants(s32 firstEntry) {
		if (!g_md5RoundConstantsReady) {
			g_md5RoundConstantsReady = true;
			g_md5RoundConstantStorage = KLBNEWA(u32, s_md5RoundConstantCount);
			g_md5RoundConstants = g_md5RoundConstantStorage + firstEntry;
			expandObfuscatedConstants(
				g_md5RoundConstantStorage,
				s_md5RoundConstantCount,
				sizeof(u32),
				s_md5RoundConstantCodes,
				sizeof(s_md5RoundConstantCodes) / sizeof(u16));
		}
	}

	/*!
	    @brief  Builds the MD5 chaining-state table on first use.
	    @param[in]  firstEntry  index the published pointer refers to.
	 */
	void initializeMd5InitialState(s32 firstEntry) {
		if (!g_md5InitialStateReady) {
			g_md5InitialStateReady = true;
			g_md5InitialState = g_md5InitialStateStorage + firstEntry;
			expandObfuscatedConstants(
				g_md5InitialStateStorage,
				s_md5InitialStateCount,
				sizeof(u32),
				s_md5InitialStateCodes,
				sizeof(s_md5InitialStateCodes) / sizeof(u16));
		}
	}

	/*!
	    @brief  Builds the MD5 rotation-amount table on first use.
	    @param[in]  firstEntry  index the published pointer refers to.
	 */
	void initializeMd5ShiftAmounts(s32 firstEntry) {
		if (!g_md5ShiftAmountsReady) {
			g_md5ShiftAmountsReady = true;
			g_md5ShiftAmounts = g_md5ShiftAmountStorage + firstEntry;
			expandObfuscatedConstants(
				g_md5ShiftAmountStorage,
				s_md5ShiftAmountCount,
				sizeof(u8),
				s_md5ShiftAmountCodes,
				sizeof(s_md5ShiftAmountCodes) / sizeof(u16));
		}
	}

	/*!
	    @brief  Builds the Lehmer generator parameter pair on first use.
	    @param[in]  firstEntry  index the published pointer refers to.
	 */
	void initializeMinstdParameters(s32 firstEntry) {
		if (!g_minstdParametersReady) {
			g_minstdParametersReady = true;
			g_minstdParameters = g_minstdParameterStorage + firstEntry;
			expandObfuscatedConstants(
				g_minstdParameterStorage,
				s_minstdParameterCount,
				sizeof(u32),
				s_minstdParameterCodes,
				sizeof(s_minstdParameterCodes) / sizeof(u16));
		}
	}

	/*!
	    @brief  Builds the stream-cipher parameter sets on first use.
	    @param[in]  firstEntry  index the published pointer refers to; the
	                            header initializers read the set at that
	                            offset.
	 */
	void initializeStreamCipherParameters(s32 firstEntry) {
		if (!g_streamCipherParametersReady) {
			g_streamCipherParametersReady = true;
			g_streamCipherParameters = g_streamCipherParameterStorage + firstEntry;
			expandObfuscatedConstants(
				g_streamCipherParameterStorage,
				s_streamCipherParameterCount,
				sizeof(u32),
				s_streamCipherParameterCodes,
				sizeof(s_streamCipherParameterCodes) / sizeof(u16));
		}
	}

	/*!
	    @brief  Builds the embedded application key on first use.
	    @param[in]  firstEntry  index the published pointer refers to.
	 */
	void initializeApplicationKey(s32 firstEntry) {
		if (!g_applicationKeyReady) {
			g_applicationKeyReady = true;
			g_applicationKeyWords = g_applicationKeyStorage + firstEntry;
			expandObfuscatedConstants(
				g_applicationKeyStorage,
				s_applicationKeyWordCount,
				sizeof(u32),
				s_applicationKeyCodes,
				sizeof(s_applicationKeyCodes) / sizeof(u16));
		}
	}

	s32 negateValue(s32 value) {
		return -value;
	}

	s32 wrapTransformIndex(s32 value) {
		return (value * 9) % 47;
	}

	s32 advanceTransformGroup(s32 value) {
		return (value + ((value % 9) ? 47 : 0)) / 9;
	}

	/*!
	    @brief  Picks one of the four stream-cipher parameter sets from an
	            asset path.

	    Mode 0 accumulates the path bytes, mode 1 accumulates their
	    complements; any other mode selects the first set.

	    @param[in]  path    asset path, NUL terminated
	    @param[in]  mode    accumulation mode
	    @return     parameter set index, 0..3
	 */
	u32 pathModeSelector(const char* path, u32 mode) {
		u32 checksum = 0, selected = 0;
		s32 length = 0;
		switch (mode) {
		case 0:
			while (path[length]) {
				checksum += static_cast<u8>(path[length]);
				++length;
			}
			selected = length + checksum;
			break;
		case 1:
			while (path[length]) {
				checksum += ~static_cast<u32>(static_cast<u8>(path[length]));
				++length;
			}
			selected = length + checksum;
			break;
		}
		return selected & 3;
	}

	s32 nextRandom(u64* state) {
		*state = *state * 0x5851f42d4c957f2dULL
			+ 0x14057b7ef767814fULL;
		return static_cast<s32>((*state >> 16) & 0x7fffffff);
	}

	void seedRandom(s32 seed, u64* state) {
		*state = static_cast<u64>(static_cast<s64>(seed))
			* 0x5851f42d4c957f2dULL + 0x14057b7ef767814fULL;
	}

	// Width of the obfuscated bit field carried by an extended header.
	static const s32 s_permutedBitCount = 12;

	// Applied to the recovered bit field once it has been unshuffled.
	static const u32 s_permutedBitMask = 0x555;

	/*!
	    @brief  Builds a pseudo-random permutation of 0..count-1.

	    The table starts out as the identity and is then shuffled from the
	    back with the generator seeded by @a seed, so the same seed always
	    yields the same permutation.

	    @param[out] table   receives the permutation
	    @param[in]  count   number of entries
	    @param[in]  seed    generator seed
	    @param[in]  state   generator state, left at the last value used
	 */
	void buildPermutation(u32* table, s32 count, s32 seed, u64* state) {
		seedRandom(seed, state);
		for (s32 index = 0; index < count; ++index) {
			table[index] = index;
		}
		for (s32 index = count - 1; index >= 0; --index) {
			const s32 pick = nextRandom(state) % (index + 1);
			const u32 swapped = table[pick];
			table[pick] = table[index];
			table[index] = swapped;
		}
	}

	/*!
	    @brief  Moves the low twelve bits of a value to seed-dependent
	            positions.

	    @param[in]  seed    selects the permutation
	    @param[in]  value   bit field to permute
	    @return     the permuted bit field
	 */
	u32 permuteBits(s32 seed, u32 value) {
		u32 order[s_permutedBitCount];
		u64 state;
		buildPermutation(order, s_permutedBitCount, seed, &state);

		u32 permuted = 0;
		for (s32 bit = 0; bit < s_permutedBitCount; ++bit) {
			const u32 position = order[bit];
			permuted |= ((value >> bit) & 1) << position;
		}
		return permuted;
	}

	/*!
	    @brief  Recovers a counter stored obfuscated in an extended header.

	    The upper twelve bits of the stored value select the permutation
	    applied to its lower twelve bits.  Zero means "not present".

	    @param[in]  value   the twenty-four bit stored field
	    @return     the recovered counter
	 */
	u32 decodeObfuscatedCounter(u32 value) {
		if (!value) {
			return 0;
		}
		return permuteBits(value >> s_permutedBitCount,
			value & 0xfff) ^ s_permutedBitMask;
	}

	// Every asset key is derived from the MD5 digest of this fixed prefix
	// followed by the asset's file name, that is its path with any directory
	// part removed.
	static const char s_digestPrefix[] = "Hello";
	static const s32 s_digestPrefixLength = 5;

	// MD5 consumes 512 bit blocks and reserves the last 64 bits of the final
	// block for the message length, so the padding runs up to 448 bits.
	static const s32 s_md5BlockBits = 512;
	static const s32 s_md5PaddedBits = 448;
	static const s32 s_md5BlockBytes = 64;
	static const s32 s_md5RoundCount = 64;

	// Working room for the prefix and the file name; asset names are far
	// shorter than this.
	static const s32 s_digestMessageMax = 1000;

	// Length of one key stream cycle: an asset without a header of its own
	// starts somewhere inside the first one.
	static const u32 s_keyStreamCycle = 256;

	// Modulus of the Lehmer generator, 2^31 - 1.
	static const s64 s_lehmerModulus = 0x7fffffff;

	static u32 rotateLeft(u32 value, u8 amount) {
		return (value << amount) | (value >> (32 - amount));
	}

	/*!
	    @brief  MD5 digest of the fixed prefix followed by an asset file name.

	    The four digest words become, in order, the header key, the work key,
	    the primary generator key and the secondary generator key of the asset
	    being opened, so this is the single root of every per-asset secret.

	    The round constants and the initial chaining state come from the
	    obfuscated tables at the top of this file; the per-step rotation
	    amounts are the copy the dispatch registry carries.

	    @param[in]  path        asset path, NUL terminated
	    @param[out] hash0..3    the four digest words
	    @param[out] nameLength  length of the file name part of @a path
	 */
	void hashAssetName(
		const char* path,
		u32* hash0,
		u32* hash1,
		u32* hash2,
		u32* hash3,
		u32* nameLength)
	{
		const s32 pathLength = static_cast<s32>(strlen(path));
		// Walk back from the end of the path to the character after the last
		// separator, or to the start of the path if it has none.  The scan
		// leaves nameStart one short because the test consumed a step.
		const char* cursor = path + pathLength - 1;
		s32 nameStart = pathLength;
		while (nameStart-- > 0 && *cursor != '\\' && *cursor-- != '/') {
		}
		++nameStart;
		const char* const name = path + nameStart;
		const u32 length = pathLength - nameStart;

		char text[s_digestMessageMax];
		memcpy(text, s_digestPrefix, s_digestPrefixLength);
		memcpy(text + s_digestPrefixLength, name, length);
		text[s_digestPrefixLength + length] = '\0';
		*nameLength = length;

		u32 state[4] = {
			g_md5InitialState[0],
			g_md5InitialState[1],
			g_md5InitialState[2],
			g_md5InitialState[3]
		};

		const u32 messageLength = s_digestPrefixLength + length;
		const s32 messageBits = messageLength * 8;
		s32 paddedBits = (s_digestPrefixLength + pathLength - nameStart) * 8;
		do {
			++paddedBits;
		} while (paddedBits % s_md5BlockBits != s_md5PaddedBits);
		const s32 paddedBytes = paddedBits / 8;

		u8* message = KLBNEWA(u8, paddedBytes + s_md5BlockBytes);
		const u8* const shiftAmounts =
			g_encryptionRegistry->md5ShiftAmounts;
		memset(message + messageLength, 0,
			paddedBytes + s_md5BlockBytes - messageLength);
		memcpy(message, text, messageLength);
		message[messageLength] = 0x80;
		*reinterpret_cast<u32*>(message + paddedBytes) = messageBits;

		for (s32 offset = 0; offset < paddedBytes; offset += s_md5BlockBytes) {
			const u32* const words =
				reinterpret_cast<const u32*>(message + offset);
			u32 a = state[0];
			u32 b = state[1];
			u32 c = state[2];
			u32 d = state[3];
			for (u32 step = 0; step < s_md5RoundCount; ++step) {
				u32 mixed;
				u32 word;
				if (step <= 15) {
					mixed = (b & c) | (~b & d);
					word = step;
				} else if (step <= 31) {
					mixed = (b & d) | (~d & c);
					word = (5 * step + 1) & 15;
				} else if (step <= 47) {
					mixed = c ^ d ^ b;
					word = (3 * step + 5) & 15;
				} else {
					mixed = c ^ (b | ~d);
					word = (7 * step) & 15;
				}
				const u32 rotated = rotateLeft(
					mixed + a + g_md5RoundConstants[step] + words[word],
					shiftAmounts[step]);
				a = d;
				d = c;
				c = b;
				b += rotated;
			}
			state[0] += a;
			state[1] += b;
			state[2] += c;
			state[3] += d;
		}
		// The shipped implementation pairs this array allocation with scalar
		// delete.  Preserve that ownership behavior for binary compatibility.
		KLBDELETE(message);

		*hash0 = state[0];
		*hash1 = state[1];
		*hash2 = state[2];
		*hash3 = state[3];
	}

	/*!
	    @brief  Advances the counting key by one step.

	    The four key bytes are one big endian number and the step is added to
	    its last byte, so a step that would overflow that byte has to be
	    carried upwards by hand.  The remaining room in the last byte is kept
	    in @c m_blockRemaining, which is what makes the test a comparison
	    rather than an overflow check.
	 */
	static void advanceCounterKey(SUserStruct* ctx) {
		const u32 last = ctx->m_headerKeyBytes[3];
		if (last >= ctx->m_blockRemaining) {
			if (ctx->m_headerKeyBytes[2] == 0xff) {
				if (ctx->m_headerKeyBytes[1] == 0xff) {
					++ctx->m_headerKeyBytes[0];
				}
				++ctx->m_headerKeyBytes[1];
			}
			++ctx->m_headerKeyBytes[2];
		}
		ctx->m_headerKeyBytes[3] =
			static_cast<u8>(last + ctx->m_streamOffset);
	}

	/*!
	    @brief  Steps the Lehmer generator once and republishes its key.

	    The multiplication is split across the two halves of the state so that
	    it stays inside 32 bits, and the reduction modulo 2^31 - 1 folds the
	    overflowing bits back in at the bottom rather than dividing.
	 */
	static void advanceLehmerKey(SUserStruct* ctx, u32 multiplier,
		u32 modulus)
	{
		const u32 state = ctx->m_payloadLength;
		const u32 low = static_cast<u16>(state) * multiplier;
		const u32 high = (state >> 16) * multiplier;
		const u32 folded = (high << 16) & 0x7fff0000;
		u32 next = (high >> 15) + low + folded;
		if (next >= modulus) {
			next -= modulus;
		}
		ctx->m_payloadLength = next;
		ctx->m_headerKeyBytes[0] = static_cast<u8>(next >> 23);
		ctx->m_headerKeyBytes[1] = static_cast<u8>(next >> 15);
	}

	DecryptRoutine getDecryptRoutine(s32 index) {
		return g_encryptionRegistry->decryptors[index];
	}

	PositionRoutine getPositionRoutine(s32 index) {
		return g_encryptionRegistry->positioners[index];
	}

	void setDecryptRoutine(s32 group, DecryptRoutine routine) {
		g_encryptionRegistry->decryptors[wrapTransformIndex(group)] = routine;
	}

	void setPositionRoutine(s32 group, PositionRoutine routine) {
		g_encryptionRegistry->positioners[wrapTransformIndex(group)] = routine;
	}
}

int getApplicationCryptoKeyLength() {
	return NMAsset::g_nmAssetKeyLength;
}

const char* getApplicationCryptoKeyData() {
	return NMAsset::g_nmAssetKey;
}

void releaseNMAsset() {
	using namespace NMAsset;
	KLBDELETE(g_nmAssetKey);
	g_nmAssetKey = NULL;
	KLBDELETE(g_encryptionRegistry);
	g_encryptionRegistry = NULL;
	KLBDELETEA(g_md5RoundConstantStorage);
	g_md5RoundConstantsReady = false;
	g_md5RoundConstantStorage = NULL;
}

namespace NMAsset {
	// Seed table for the two header-seeded stream setups below: the low six
	// bits of the ninth header byte pick one entry.  Unlike the code lists at
	// the top of this file these values are used directly, not expanded.
	// Mechanically extracted from the shipped binary (libGame.so, x86_64) at
	// address 0x4099B0: 64 u32 entries, 256 bytes, SHA-256
	// 515e2e15af150fa6a06edb6f0ed417d8eac5196a303adeba7c0d51294431bd6e.
	static const u32 s_headerSeedTable[64] = {
	0x48230029, 0x678418BE, 0x3D6C4AE1, 0x72AE2CD6, 0x5F906952, 0x6DF11649,
	0x41BB5AF1, 0x01EB26E9, 0x2EA60BB3, 0x153C12DB, 0x390C7E87, 0x00990F3E,
	0x305E0124, 0x491C440D, 0x4DB74D06, 0x54DE1547, 0x2D1239B3, 0x4DC8074D,
	0x66BB6443, 0x26A6428B, 0x5D03701F, 0x767D7A5A, 0x12384509, 0x1E1F3B25,
	0x1AD46E5D, 0x6BFC63CB, 0x7FF57F96, 0x323B4E45, 0x260D2213, 0x030A6B89,
	0x0BDB301C, 0x073256AE, 0x759A0120, 0x22EE2350, 0x58784B40, 0x5CFD6B36,
	0x1A493E12, 0x3BF65F32, 0x797D3A9E, 0x0DDC5F49, 0x314F4CAD, 0x4DF25E14,
	0x2E404944, 0x1CD01366, 0x66C4366B, 0x7EB74230, 0x2C3B6032, 0x542215A1,
	0x08223EF6, 0x409D5991, 0x798B12E1, 0x73DA121F, 0x26CA58B0, 0x09023699,
	0x57727BB9, 0x7049139D, 0x4A80692C, 0x16C5187E, 0x3CD56899, 0x408013E9,
	0x33EA5DB2, 0x48CC23C9, 0x60BF5753, 0x3CD65C67,
	};

	// The stream-cipher parameter sets are {multiplier, increment, shift}
	// triples; both setups below use the third set.
	static const s32 s_headerSeedParameterSet = 2;
	static const s32 s_streamCipherParameterStride = 3;
}

/*!
    @brief  Stream setup seeded straight from the header seed table.
 */
void NMAsset::initializeHeaderSeeded(
	CDecryptBaseClass* decryptor,
	const u8* extendedHeader,
	const char* path)
{
	const s32 set = s_headerSeedParameterSet * s_streamCipherParameterStride;
	decryptor->m_primaryMultiplier = g_streamCipherParameters[set];
	decryptor->m_primaryIncrement = g_streamCipherParameters[set + 1];

	const u32 seed = s_headerSeedTable[extendedHeader[7] & 0x3f];
	decryptor->m_userCtx.m_primaryInitialState = seed;
	decryptor->m_userCtx.m_primaryState = seed;
	decryptor->m_userCtx.m_primaryVariant = extendedHeader[8] & 3;
	decryptor->m_schemeCounter = 0;
}

/*!
    @brief  Stream setup seeded from the complement of the header seed.
 */
void NMAsset::initializeHeaderSeededInverted(
	CDecryptBaseClass* decryptor,
	const u8* extendedHeader,
	const char* path)
{
	const s32 set = s_headerSeedParameterSet * s_streamCipherParameterStride;
	decryptor->m_primaryMultiplier = g_streamCipherParameters[set];
	decryptor->m_primaryIncrement = g_streamCipherParameters[set + 1];

	const u32 seed = ~s_headerSeedTable[extendedHeader[7] & 0x3f];
	decryptor->m_userCtx.m_primaryInitialState = seed;
	decryptor->m_userCtx.m_primaryState = seed;
	decryptor->m_userCtx.m_primaryVariant = 0;
	decryptor->m_schemeCounter = 0;
}

/*!
    @brief  Recovers the scheme counter from the extended header and hands the
            actual stream setup to one of the secondary initializers.

    The counter is stored obfuscated across header bytes 4 to 6, and the low
    nibble of byte 3 selects the secondary initializer, counted back from the
    end of that list.
 */
void NMAsset::initializeSchemeDispatch(
	CDecryptBaseClass* decryptor,
	const u8* extendedHeader,
	const char* path)
{
	const u32 stored = (static_cast<u32>(extendedHeader[4]) << 16)
		| (static_cast<u32>(extendedHeader[5]) << 8)
		| extendedHeader[6];
	decryptor->m_schemeCounter = decodeObfuscatedCounter(stored);

	const s32 scheme = extendedHeader[3] & 0xf;
	g_encryptionRegistry->secondaryInitializerEnd[-scheme](
		decryptor, extendedHeader, path);
}

/*!
    @brief  Stream setup taking its parameter set from the header and its
            seed from the primary key bytes.  Schemes past the end of the
            loaded key fall back to fixed constants.
 */
void NMAsset::initializeHeaderKeyed(
	CDecryptBaseClass* decryptor,
	const u8* extendedHeader,
	const char* path)
{
	if (decryptor->m_schemeCounter > g_nmAssetKeyLength) {
		decryptor->m_primaryMultiplier = 0xabde1476;
		decryptor->m_primaryIncrement = 0x5421eb89;
		decryptor->m_primaryShift = 0x14;
		decryptor->m_userCtx.m_primaryInitialState = 0xabde1476;
		decryptor->m_userCtx.m_primaryState = 0xabde1476;
		return;
	}

	const s32 set = (extendedHeader[2] & 3) * s_streamCipherParameterStride;
	decryptor->m_primaryMultiplier = g_streamCipherParameters[set];
	decryptor->m_primaryIncrement = g_streamCipherParameters[set + 1];
	decryptor->m_primaryShift = static_cast<u8>(g_streamCipherParameters[set + 2]);

	const u32 seed =
		(static_cast<u32>(decryptor->m_userCtx.m_primaryKeyBytes[0]) << 24)
		| (static_cast<u32>(decryptor->m_userCtx.m_primaryKeyBytes[1]) << 16)
		| (static_cast<u32>(decryptor->m_userCtx.m_primaryKeyBytes[2]) << 8)
		| decryptor->m_userCtx.m_primaryKeyBytes[3];
	decryptor->m_userCtx.m_primaryInitialState = seed;
	decryptor->m_userCtx.m_primaryState = seed;
}

/*!
    @brief  Stream setup whose parameter set comes from a checksum of the
            asset path and whose seed is the complement of the primary key
            bytes.
 */
void NMAsset::initializePathKeyed(
	CDecryptBaseClass* decryptor,
	const u8* extendedHeader,
	const char* path)
{
	if (decryptor->m_schemeCounter > g_nmAssetKeyLength) {
		decryptor->m_primaryMultiplier = 0xabde1476;
		decryptor->m_primaryIncrement = 0x5421eb89;
		decryptor->m_primaryShift = 0x14;
		decryptor->m_userCtx.m_primaryInitialState = 0xabde1476;
		decryptor->m_userCtx.m_primaryState = 0xabde1476;
		return;
	}

	u32 checksum = 0;
	s32 length = 0;
	while (path[length]) {
		checksum += static_cast<u8>(path[length]);
		++length;
	}

	const s32 set = ((length + checksum) & 3) * s_streamCipherParameterStride;
	decryptor->m_primaryMultiplier = g_streamCipherParameters[set];
	decryptor->m_primaryIncrement = g_streamCipherParameters[set + 1];
	decryptor->m_primaryShift = static_cast<u8>(g_streamCipherParameters[set + 2]);

	const u32 seed = ~(
		(static_cast<u32>(decryptor->m_userCtx.m_primaryKeyBytes[0]) << 24)
		| (static_cast<u32>(decryptor->m_userCtx.m_primaryKeyBytes[1]) << 16)
		| (static_cast<u32>(decryptor->m_userCtx.m_primaryKeyBytes[2]) << 8)
		| decryptor->m_userCtx.m_primaryKeyBytes[3]);
	decryptor->m_userCtx.m_primaryInitialState = seed;
	decryptor->m_userCtx.m_primaryState = seed;
}

/*!
    @brief  Stream setup for the two-generator schemes.

    Both generators take their parameter set from a checksum of the asset
    path, the primary one from the plain byte sum and the secondary one from
    the complemented sum, and each is seeded from its own key bytes.
 */
void NMAsset::initializePathKeyedDual(
	CDecryptBaseClass* decryptor,
	const u8* extendedHeader,
	const char* path)
{
	if (decryptor->m_schemeCounter > g_nmAssetKeyLength) {
		decryptor->m_primaryMultiplier = 0xabde1476;
		decryptor->m_primaryIncrement = 0x5421eb89;
		decryptor->m_primaryShift = 0x14;
		decryptor->m_userCtx.m_primaryState =
			decryptor->m_userCtx.m_secondaryState = 0xabde1476;
		decryptor->m_userCtx.m_primaryInitialState =
			decryptor->m_userCtx.m_secondaryInitialState = 0xabde1476;
		return;
	}

	const s32 primarySet =
		pathModeSelector(path, 0) * s_streamCipherParameterStride;
	decryptor->m_primaryMultiplier = g_streamCipherParameters[primarySet];
	decryptor->m_primaryIncrement = g_streamCipherParameters[primarySet + 1];
	decryptor->m_primaryShift =
		static_cast<u8>(g_streamCipherParameters[primarySet + 2]);

	const s32 secondarySet =
		pathModeSelector(path, 1) * s_streamCipherParameterStride;
	decryptor->m_secondaryMultiplier = g_streamCipherParameters[secondarySet];
	decryptor->m_secondaryIncrement = g_streamCipherParameters[secondarySet + 1];
	decryptor->m_secondaryShift =
		static_cast<u8>(g_streamCipherParameters[secondarySet + 2]);

	const u32 primarySeed =
		(static_cast<u32>(decryptor->m_userCtx.m_primaryKeyBytes[0]) << 24)
		| (static_cast<u32>(decryptor->m_userCtx.m_primaryKeyBytes[1]) << 16)
		| (static_cast<u32>(decryptor->m_userCtx.m_primaryKeyBytes[2]) << 8)
		| decryptor->m_userCtx.m_primaryKeyBytes[3];
	decryptor->m_userCtx.m_primaryInitialState = primarySeed;
	decryptor->m_userCtx.m_primaryState = primarySeed;

	const u32 secondarySeed =
		(static_cast<u32>(decryptor->m_userCtx.m_secondaryKeyBytes[0]) << 24)
		| (static_cast<u32>(decryptor->m_userCtx.m_secondaryKeyBytes[1]) << 16)
		| (static_cast<u32>(decryptor->m_userCtx.m_secondaryKeyBytes[2]) << 8)
		| decryptor->m_userCtx.m_secondaryKeyBytes[3];
	decryptor->m_userCtx.m_secondaryInitialState = secondarySeed;
	decryptor->m_userCtx.m_secondaryState = secondarySeed;
}

/*!
    @brief  コンストラクタ
 */
CDecryptBaseClass::CDecryptBaseClass(u32 allowedFormats) {
	m_format                    = 0;
	m_transformIndex            = 0;
	m_payloadOffset             = 0;
	m_headerSize                = 0;
	m_userCtx.m_streamOffset   = 0;
	m_userCtx.m_blockRemaining = 0;
	m_userCtx.m_streamPhase    = 0;
	m_decrypt                   = false;
	m_allowedFormats            = allowedFormats;
}

/*!
    @brief  Decrypts with the Lehmer generator stream.

    The generator publishes two key bytes per step, so the data is consumed
    two bytes at a time, and as one halfword when it is aligned.  An odd
    length leaves the second key byte unused, which @c m_streamPhase records
    so that the next block picks it up.
 */
void CDecryptBaseClass::decryptLehmer(void* ptr, u32 length) {
	if (length == 0) {
		return;
	}

	const u32 modulus = NMAsset::g_minstdParameters[0];
	const u32 multiplier = NMAsset::g_minstdParameters[1];

	u8* bytes = static_cast<u8*>(ptr);
	if (m_userCtx.m_streamPhase & 1) {
		*bytes ^= m_userCtx.m_headerKeyBytes[1];
		++bytes;
		m_userCtx.m_streamPhase = 0;
		--length;
		NMAsset::advanceLehmerKey(&m_userCtx, multiplier, modulus);
	}

	const u32 stepCount = length >> 1;
	const u32 stepBytes = stepCount * 2;
	if ((reinterpret_cast<size_t>(bytes) & 1) != 0) {
		for (u32 step = 0; step < stepCount; ++step) {
			bytes[0] ^= m_userCtx.m_headerKeyBytes[0];
			bytes[1] ^= m_userCtx.m_headerKeyBytes[1];
			NMAsset::advanceLehmerKey(&m_userCtx, multiplier, modulus);
			bytes += 2;
		}
	} else {
		u16* words = reinterpret_cast<u16*>(bytes);
		for (u32 step = 0; step < stepCount; ++step) {
			*words ^= *reinterpret_cast<u16*>(m_userCtx.m_headerKeyBytes);
			++words;
			NMAsset::advanceLehmerKey(&m_userCtx, multiplier, modulus);
		}
		bytes = reinterpret_cast<u8*>(words);
	}

	if (length != stepBytes) {
		*bytes ^= m_userCtx.m_headerKeyBytes[0];
		m_userCtx.m_streamPhase = 1;
	}
}

/*!
    @brief  Decrypts with the counting key stream.

    The key is simply XORed over the data one byte at a time and stepped once
    every four bytes, so whole four byte groups can be handled without
    consulting the phase at all, and as one word when the data is aligned.
 */
void CDecryptBaseClass::decryptCounterKey(void* ptr, u32 length) {
	if (length == 0) {
		return;
	}

	u8* bytes = static_cast<u8*>(ptr);
	u8* const end = bytes + length;

	if ((m_userCtx.m_streamPhase & 3) == 0) {
		if ((reinterpret_cast<size_t>(bytes) & 3) != 0) {
			while (length >= 4) {
				bytes[0] ^= m_userCtx.m_headerKeyBytes[0];
				bytes[1] ^= m_userCtx.m_headerKeyBytes[1];
				bytes[2] ^= m_userCtx.m_headerKeyBytes[2];
				bytes[3] ^= m_userCtx.m_headerKeyBytes[3];
				NMAsset::advanceCounterKey(&m_userCtx);
				bytes += 4;
				length -= 4;
			}
		} else {
			u32* words = reinterpret_cast<u32*>(bytes);
			while (length >= 4) {
				*words ^=
					*reinterpret_cast<u32*>(m_userCtx.m_headerKeyBytes);
				++words;
				NMAsset::advanceCounterKey(&m_userCtx);
				length -= 4;
			}
			bytes = reinterpret_cast<u8*>(words);
		}
	}

	while (bytes < end) {
		*bytes ^= m_userCtx.m_headerKeyBytes[m_userCtx.m_streamPhase & 3];
		++bytes;
		++m_userCtx.m_streamPhase;
		if ((m_userCtx.m_streamPhase & 3) == 0) {
			NMAsset::advanceCounterKey(&m_userCtx);
		}
	}
}

/*!
    @brief  Repositions the counting key stream.

    That stream advances its four byte key as one big endian number, adding
    the per-asset step once every four bytes, so an arbitrary position is
    reached by adding the step that many times to the saved key.
 */
void CDecryptBaseClass::gotoOffsetCounterKey(u32 offset) {
	const u8 saved0 = m_userCtx.m_savedWorkKeyBytes[0];
	const u8 saved1 = m_userCtx.m_savedWorkKeyBytes[1];
	const u8 saved2 = m_userCtx.m_savedWorkKeyBytes[2];
	const u8 saved3 = m_userCtx.m_savedWorkKeyBytes[3];

	m_userCtx.m_streamPhase = static_cast<u8>(offset);

	u32 carried = (offset >> 2) * m_userCtx.m_streamOffset;
	carried += saved3;
	m_userCtx.m_headerKeyBytes[3] = static_cast<u8>(carried);
	carried >>= 8;
	carried += saved2;
	m_userCtx.m_headerKeyBytes[2] = static_cast<u8>(carried);
	carried >>= 8;
	carried += saved1;
	m_userCtx.m_headerKeyBytes[1] = static_cast<u8>(carried);
	carried >>= 8;
	carried += saved0;
	m_userCtx.m_headerKeyBytes[0] = static_cast<u8>(carried);
}

/*!
    @brief  Repositions the Lehmer generator stream.

    The generator emits one key word every two bytes, so reaching an offset
    means advancing it by half that many steps.  Raising the multiplier to
    that power by repeated squaring keeps the cost logarithmic.
 */
void CDecryptBaseClass::gotoOffsetLehmer(u32 offset) {
	m_userCtx.m_headerKeyBytes[0] = m_userCtx.m_savedWorkKeyBytes[0];
	m_userCtx.m_headerKeyBytes[1] = m_userCtx.m_savedWorkKeyBytes[1];
	m_userCtx.m_headerKeyBytes[2] = m_userCtx.m_savedWorkKeyBytes[2];
	m_userCtx.m_headerKeyBytes[3] = m_userCtx.m_savedWorkKeyBytes[3];

	const u32 tailBits = m_userCtx.m_savedWorkKeyBytes[3];
	const u32 headBits = static_cast<u32>(m_userCtx.m_savedWorkKeyBytes[0] & 0x7f) << 24;
	const u32 upperBits = static_cast<u32>(m_userCtx.m_savedWorkKeyBytes[1]) << 16;
	const u32 lowerBits = static_cast<u32>(m_userCtx.m_savedWorkKeyBytes[2]) << 8;
	u32 state = headBits | upperBits | lowerBits | tailBits;
	m_userCtx.m_payloadLength = state;

	m_userCtx.m_streamPhase = static_cast<u8>(offset);

	u32 steps = offset >> 1;
	if (steps != 0) {
		s64 multiplier = NMAsset::g_minstdParameters[1];
		const s64 modulus =
			static_cast<s32>(NMAsset::g_minstdParameters[0]);
		do {
			if (steps & 1) {
				state = static_cast<u32>(
					(static_cast<s64>(state) * multiplier)
					% NMAsset::s_lehmerModulus);
				m_userCtx.m_payloadLength = state;
			}
			multiplier = (multiplier * multiplier) % modulus;
			steps >>= 1;
		} while (steps != 0);
	}

	m_userCtx.m_headerKeyBytes[0] = static_cast<u8>(state >> 23);
	m_userCtx.m_headerKeyBytes[1] = static_cast<u8>(state >> 15);
}

void CDecryptBaseClass::decryptBlck(void* ptr, u32 length) {
	if (!(m_useNew & 2)) {
		NMAsset::DecryptRoutine routine =
			NMAsset::g_encryptionRegistry->decryptors[m_transformIndex];
		(this->*routine)(ptr, length);
	}
}

/*!
    @brief  複合化
    @param[in]  void* ptr       暗号化されたデータ
    @param[in]  u32 length      データの長さ
    @return     void
 */
void CDecryptBaseClass::decrypt(void* ptr, u32 length) {
	// "Transparent" encryption : do nothing.
}

/*!
    @brief  複合化準備
    @param[in]  const u8* ptr           ファイルパス
    @param[in]  const u8* hdr           先頭のヘッダバイト
    @param[out] u32* headerSize         ヘッダの長さ
    @return     void
 */
void CDecryptBaseClass::decryptSetup(
	const u8* ptr,
	const u8* hdr,
	u32* headerSize)
{
	m_userCtx.m_streamPhase = 0;

	const s32 format = NMAsset::parseAssetHeader(
		hdr,
		reinterpret_cast<const char*>(ptr),
		m_userCtx.m_headerKeyBytes,
		m_userCtx.m_workKeyBytes,
		m_userCtx.m_primaryKeyBytes,
		m_userCtx.m_secondaryKeyBytes,
		&m_userCtx.m_streamOffset,
		&m_headerSize);
	*headerSize = m_headerSize;

	if (format == 1) {
		// The asset carries no header of its own; the key stream simply
		// starts part way into its first cycle.
		m_userCtx.m_savedWorkKeyBytes[0] = m_userCtx.m_headerKeyBytes[0];
		m_userCtx.m_savedWorkKeyBytes[1] = m_userCtx.m_headerKeyBytes[1];
		m_userCtx.m_savedWorkKeyBytes[2] = m_userCtx.m_headerKeyBytes[2];
		m_userCtx.m_savedWorkKeyBytes[3] = m_userCtx.m_headerKeyBytes[3];
		m_userCtx.m_blockRemaining =
			NMAsset::s_keyStreamCycle - m_userCtx.m_streamOffset;
		m_format = 1;
	} else if (format == 2) {
		// The header key doubles as the payload length, so it is consumed
		// here and what is left of it is shifted back into place.
		m_userCtx.m_savedWorkKeyBytes[0] = m_userCtx.m_headerKeyBytes[0];
		m_userCtx.m_savedWorkKeyBytes[1] = m_userCtx.m_headerKeyBytes[1];
		m_userCtx.m_savedWorkKeyBytes[2] = m_userCtx.m_headerKeyBytes[2];
		m_userCtx.m_savedWorkKeyBytes[3] = m_userCtx.m_headerKeyBytes[3];

		const u32 payloadLength =
			(static_cast<u32>(m_userCtx.m_headerKeyBytes[0] & 0x7f) << 24)
			| (static_cast<u32>(m_userCtx.m_headerKeyBytes[1]) << 16)
			| (static_cast<u32>(m_userCtx.m_headerKeyBytes[2]) << 8)
			| m_userCtx.m_headerKeyBytes[3];
		m_userCtx.m_payloadLength = payloadLength;
		m_userCtx.m_headerKeyBytes[0] = static_cast<u8>(payloadLength >> 23);
		m_userCtx.m_headerKeyBytes[1] = static_cast<u8>(payloadLength >> 15);
		m_format = 2;
	} else if (format == 3) {
		// The extended header sits between the four identifying bytes and
		// the payload.
		m_payloadOffset = static_cast<u16>(m_headerSize - 4);
		m_format = 3;
	}

	m_transformIndex =
		static_cast<u16>(NMAsset::wrapTransformIndex(format));

	const bool transparent = (format == 0);
	const u32 allowed = (1u << format) & m_allowedFormats;
	m_useNew = static_cast<u8>(transparent | (2 * (allowed == 0)));

	klb_assert(allowed != 0 || (hdr[0] | hdr[1] | hdr[2] | hdr[3]) == 0,
		"Error ESN 0.");

	m_decrypt = true;
}

namespace NMAsset {
	// Position just past the first path separator in @a text, or NULL when
	// @a text holds no separator at all.
	static const char* findSeparator(const char* text) {
		for (; *text != '\0'; ++text) {
			if (*text == '/') {
				return text + 1;
			}
		}
		return NULL;
	}
}

void CDecryptBaseClass::finishSetup(
	const u8* extendedHeader,
	const char* path)
{
	if (m_format != 3) {
		return;
	}

	const u32 variant = extendedHeader[3];
	m_useNew |= (variant == 1);
	if (variant >= 2) {
		const u32 transform = variant * 9 + 18;
		const size_t positionerCount =
			sizeof(NMAsset::g_encryptionRegistry->positioners)
			/ sizeof(NMAsset::g_encryptionRegistry->positioners[0]);
		m_transformIndex = static_cast<u16>(transform % positionerCount);
	}

	switch (variant) {
	case 0:
		NMAsset::g_encryptionRegistry->primaryInitializerEnd[-3](
			this, extendedHeader, path);
		return;
	case 1:
		NMAsset::g_encryptionRegistry->primaryInitializerEnd[-6](
			this, extendedHeader, path);
		return;
	default: {
		// Keep only the file-name part of the asset path.
		const char* separator;
		while ((separator = NMAsset::findSeparator(path)) != NULL) {
			path = separator;
		}
		NMAsset::g_encryptionRegistry->primaryInitializerEnd[-1](
			this, extendedHeader, path);
		return;
	}
	}
}

void CDecryptBaseClass::gotoOffset(u32 offset) {
	// Recompute and update your encryption context if we jump at a certain position into the encrypted stream.
	// gotoOffset is ALWAYS called BEFORE decrypt if a jump in the decoding stream occurs.
	NMAsset::PositionRoutine routine =
		NMAsset::g_encryptionRegistry->positioners[m_transformIndex];
	(this->*routine)(offset);
}

bool CDecryptBaseClass::isUserEncrypted() const {
	return m_useNew & 1;
}

namespace NMAsset {
	/*!
	    @brief  Publishes the header initializers into a dispatch registry.

	    The primary list is indexed forwards, by the header variant the
	    finishing pass reads, and the secondary list backwards from its end,
	    by the scheme nibble @c initializeSchemeDispatch pulls out of the
	    extended header, so the two lists are filled in opposite directions.
	    The slots left untouched here belong to the other installers.
	 */
	void installInitializers(EncryptionDispatchRegistry* registry) {
		registry->primaryInitializers[0] = initializeHeaderSeededInverted;
		registry->primaryInitializers[3] = initializeHeaderSeeded;
		registry->primaryInitializers[5] = initializeSchemeDispatch;

		registry->secondaryInitializerEnd[-2] = initializeHeaderKeyed;
		registry->secondaryInitializerEnd[-3] = initializePathKeyed;
		registry->secondaryInitializerEnd[-4] = initializePathKeyedDual;
		registry->secondaryInitializerEnd[-5] =
			CDecryptBaseClass::initializeUserKeyed;
		registry->secondaryInitializerEnd[-6] = initializeReservedA;
		registry->secondaryInitializerEnd[-7] = initializeReservedB;
	}
}

/*!
    @brief  Stream setup whose generator parameters come from the game script.

    An asset whose scheme counter runs past the end of the loaded application
    key, and every asset at all while the game has registered no asset filter,
    falls back to the same fixed constants the other keyed setups use.
    Otherwise the registered script function is called with the asset path and
    is expected to leave the eight generator parameters on the stack, the
    primary generator's three parameters and seed first.
 */
void CDecryptBaseClass::initializeUserKeyed(
	CDecryptBaseClass* decryptor,
	const u8* extendedHeader,
	const char* path)
{
	if (decryptor->m_schemeCounter <= NMAsset::g_nmAssetKeyLength
		&& NMAsset::g_assetFilterCallback != NULL)
	{
		CLuaState& lua = CKLBLuaEnv::getInstance().getState();
		if (lua.retcall(8, NMAsset::g_assetFilterCallback, "S", path)) {
			decryptor->m_primaryMultiplier = lua.getInt(-1);
			decryptor->m_primaryIncrement = lua.getInt(-2);
			decryptor->m_primaryShift = static_cast<u8>(lua.getInt(-3));
			decryptor->m_userCtx.m_primaryState = lua.getInt(-4);
			decryptor->m_secondaryMultiplier = lua.getInt(-5);
			decryptor->m_secondaryIncrement = lua.getInt(-6);
			decryptor->m_secondaryShift = static_cast<u8>(lua.getInt(-7));
			decryptor->m_userCtx.m_secondaryState = lua.getInt(-8);
			lua.pop(8);

			decryptor->m_userCtx.m_primaryInitialState =
				decryptor->m_userCtx.m_primaryState;
			decryptor->m_userCtx.m_secondaryInitialState =
				decryptor->m_userCtx.m_secondaryState;
			return;
		}
	}

	decryptor->m_primaryMultiplier = 0xabde1476;
	decryptor->m_primaryIncrement = 0x5421eb89;
	decryptor->m_primaryShift = 0x14;
	decryptor->m_userCtx.m_primaryState = 0xabde1476;
}

// The two reserved scheme slots are installed but unused by the shipped
// asset formats. They live here rather than beside the other algorithms so
// that installInitializers can address them with a direct relocation, as
// the shipped single translation unit did.
void NMAsset::initializeReservedA(CDecryptBaseClass*, const u8*, const char*) {
}

void NMAsset::initializeReservedB(CDecryptBaseClass*, const u8*, const char*) {
}

// The shipped decrypt registry and its algorithm bodies were compiled as one
// translation unit: its initializer takes direct PC-relative addresses of all
// ten routine pairs. Keep the readable implementation partitioned while
// reproducing that original compilation boundary.
#define KLB_ENCRYPT_FILE_ALGORITHMS_IMPLEMENTATION
#include "encryptFileAlgorithms.cpp"
#undef KLB_ENCRYPT_FILE_ALGORITHMS_IMPLEMENTATION

/*!
    @brief  Initializes the process-wide named-asset decryption machinery.

    The shipped application calls this once at the beginning of each engine
    initialization.  The mode argument is part of that call contract but is
    not consulted by this engine revision.  Constants are expanded from their
    obfuscated representations, the transform and initializer dispatch tables
    are populated with typed function pointers, and the application key is
    materialized as a NUL-terminated string.
 */
void initNMAsset(u32 /*initializationMode*/) {
	using namespace NMAsset;

	initializeMd5RoundConstants(0);
	initializeMd5InitialState(0);
	initializeMd5ShiftAmounts(0);

	// This registry is filled slot-by-slot below; default-initializing the POD
	// would add a whole-object clear that the shipped initializer does not do.
	EncryptionDispatchRegistry* registry = new EncryptionDispatchRegistry;
	registry->primaryInitializerEnd = &registry->primaryInitializers[6];
	registry->secondaryInitializerEnd = &registry->secondaryInitializers[8];
	g_encryptionRegistry = registry;
	for (s32 index = 0; index < s_md5ShiftAmountCount; ++index) {
		g_encryptionRegistry->md5ShiftAmounts[index] =
			g_md5ShiftAmounts[index];
	}

	initializeMinstdParameters(0);
	initializeStreamCipherParameters(0);

	for (s32 group = 0; group < 10; ++group) {
		DecryptRoutine decryptRoutine;
		PositionRoutine positionRoutine;
		switch (group) {
		case 0:
			decryptRoutine = &CDecryptBaseClass::decryptNone;
			positionRoutine = &CDecryptBaseClass::gotoOffsetNone;
			break;
		case 1:
			decryptRoutine = &CDecryptBaseClass::decryptCounterKey;
			positionRoutine = &CDecryptBaseClass::gotoOffsetCounterKey;
			break;
		case 2:
			decryptRoutine = &CDecryptBaseClass::decryptLehmer;
			positionRoutine = &CDecryptBaseClass::gotoOffsetLehmer;
			break;
		case 3:
			decryptRoutine = &CDecryptBaseClass::decryptLCG24;
			positionRoutine = &CDecryptBaseClass::gotoOffsetLCG;
			break;
		case 4:
			decryptRoutine = &CDecryptBaseClass::decryptLCG;
			positionRoutine = &CDecryptBaseClass::gotoOffsetLCGVariant;
			break;
		case 5:
			decryptRoutine = &CDecryptBaseClass::decryptFeedback;
			positionRoutine = &CDecryptBaseClass::gotoOffsetFeedback;
			break;
		case 6:
			decryptRoutine = &CDecryptBaseClass::decryptDualLCG;
			positionRoutine = &CDecryptBaseClass::gotoOffsetDualLCG;
			break;
		case 7:
			decryptRoutine = &CDecryptBaseClass::decryptDualLCGStride2;
			positionRoutine = &CDecryptBaseClass::gotoOffsetDualLCGVariant;
			break;
		case 8:
			decryptRoutine = &CDecryptBaseClass::decryptReservedA;
			positionRoutine = &CDecryptBaseClass::gotoOffsetReservedA;
			break;
		case 9:
			decryptRoutine = &CDecryptBaseClass::decryptReservedB;
			positionRoutine = &CDecryptBaseClass::gotoOffsetReservedB;
			break;
		}
		setDecryptRoutine(group, decryptRoutine);
		setPositionRoutine(group, positionRoutine);
	}

	installInitializers(g_encryptionRegistry);
	initializeApplicationKey(0);

	const s32 keyLength = static_cast<s32>(g_applicationKeyWords[0]);
	g_nmAssetKeyLength = keyLength;
	char* key = new char[keyLength + 1]();
	g_nmAssetKey = key;
	for (s32 index = 0; index < g_nmAssetKeyLength; ++index) {
		g_nmAssetKey[strlen(g_nmAssetKey)] =
			static_cast<char>(g_applicationKeyWords[index + 1]);
	}
}
