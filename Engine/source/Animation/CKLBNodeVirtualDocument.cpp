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
#include "CKLBNodeVirtualDocument.h"
#include <stddef.h>

#include "CKLBTexturePacker.h"
TexturePacker& mgrPacker = TexturePacker::getInstance();
static u32 s_documentCount = 0;
static u8* s_outlineBuffer = NULL;
static u32 s_outlineBufferSize = 0;
static int s_verticalBlurSums[3000];

class DocumentLifetimeRelease {
public:
	~DocumentLifetimeRelease() {
		if (--s_documentCount == 0) {
			delete[] s_outlineBuffer;
			s_outlineBuffer = NULL;
			s_outlineBufferSize = 0;
		}
	}
};

static u32 convertARGB32_RGBA8(u32 argb) {
	u32 dst;
	u8* pDst = (u8*)&dst;
	pDst[0]  = argb>>16;    // R
	pDst[1]  = argb>>8;     // G
	pDst[2]  = argb;        // B
	pDst[3]  = argb>>24;    // A
	return dst;
}

// RGBA8 32 bit encoded
u16 getTo4444(u32 color) {
	u8* pCol = (u8*)&color;

	// Base color.
	u16 col	= 	      ((pCol[2]     ) & 0x00F0)		// Blue
					| ((pCol[1] << 4) & 0x0F00)		// Green
					| ((pCol[0] << 8) & 0xF000)		// Red
					| ((pCol[3] >> 4))				// Alpha
					;
	return col;
}

void RenderContext::setClip	(s32 x0, s32 y0, s32 x1, s32 y1) {
	if (x0 < 0)				{ x0 = 0; }
	if (y0 < 0)				{ y0 = 0; }
	if (x1 > targetWidth)	{ x1 = targetWidth;		}
	if (y1 > targetHeight)	{ y1 = targetHeight;	}

	clipX0 = x0;
	clipY0 = y0;
	clipX1 = x1;
	clipY1 = y1;
}

void RenderContext::setPixelClip(s32 x1, s32 y1, u32 color) {
	if ((x1 < clipX0) || (y1 < clipY0) || (x1 >= clipX1) || (y1 >= clipY1)) {
		return;
	}

	u8* pCol = (u8*)&color;
	if (pCol[3] != 255) {
		int alpha	= pCol[3]; alpha += alpha>>7;		// 0..256
		int nalpha	= 256 - alpha;						// 256..0

		int cb		= alpha * pCol[0];
		int cg		= alpha * pCol[1];
		int cr		= alpha * pCol[2];
		int ca		= alpha * pCol[3];

		u32* pBuf = &pBuffer[x1 + (stride * y1)];
		u8* p8Buf = (u8*)pBuf;

		int b = ((p8Buf[0] * nalpha) + cb)>>8;
		int g = ((p8Buf[1] * nalpha) + cg)>>8;
		int r = ((p8Buf[2] * nalpha) + cr)>>8;
		int ar= ((p8Buf[3] * nalpha) + ca)>>8; // Do NOT use scale (255 vs 256)

		p8Buf	[0] = b;  // B
		p8Buf	[1] = g;  // G
		p8Buf	[2] = r;  // R
		p8Buf	[3] = ar; // A
	} else {
		pBuffer[x1 + (stride * y1)] = color;
	}
}

void RenderContext::setPixelClip4444(s32 x1, s32 y1, u16 color) {
	if ((x1 < clipX0) || (y1 < clipY0) || (x1 >= clipX1) || (y1 >= clipY1)) {
		return;
	}

	int alpha = color >> 12;
	if (alpha != 0xF) {
			alpha  += alpha>>3;							// 0..16
		int nalpha	= 16 - alpha;						// 16..0

		// 4.4 precision
		int cb		= alpha * (color & 0x00F0);
		int cg		= alpha * (color & 0x0F00);
		int cr		= alpha * (color & 0xF000);
		int ca		= alpha * (color & 0x000F);

		u16* pBuf = &((u16*)pBuffer)[x1 + (stride * y1)];
		u16  col = *pBuf;

		int b = (((col & 0x00F0) * nalpha) + cb)>>4;
		int g = (((col & 0x0F00) * nalpha) + cg)>>4;
		int r = (((col & 0xF000) * nalpha) + cr)>>4;
		int ar= (((col & 0x000F) * nalpha) + ca)>>4; // Do NOT use scale (255 vs 256)

		*pBuf++ = (r & 0xF000) | (g & 0x0F00) | (b & 0x00F0) | (ar & 0x000F);
	} else {
		((u16*)pBuffer)[x1 + (stride * y1)] = color;
	}
}

void RenderContext::drawLine(s32 x1, s32 y1, s32 x2, s32 y2, u32 color) {
	x2 -= offsetX;
	x1 -= offsetX;
	y2 -= offsetY;
	y1 -= offsetY;

	s32 d, dx, dy, delta, xincr, x, y;

	dx = x2 - x1; if (dx < 0) { dx = -dx; }
	dy = y2 - y1; if (dy < 0) { dy = -dy; }

	s32 inv = (dx >= dy);
	if (inv) {
		// Swap X and Y
		x = x1; x1 = y1; y1 = x;
		x = x2; x2 = y2; y2 = x;
	}
	
	if (y1 > y2) {
		x = x1; x1 = x2; x2 = x;
		y = y1; y1 = y2; y2 = y;
	}

	xincr = x2 > x1 ? 1 : -1;
	dy = y2 - y1;
	dx = x2 - x1;
	if (dx < 0) { dx = -dx; }
	delta = 2 * dx;
	d = delta - dy;

	x = x1;

	if (format == 4) {
		for (y = y1; y <= y2; ++y) {
			if (inv) {
				setPixelClip(y, x, color);
			} else {
				setPixelClip(x, y, color);
			}
		
			d += delta;
			if (d >= 0) {
				x += xincr;
				d -= (dy<<1);
			}
		}
	} else {
		u16 color16 = getTo4444(color);

		for (y = y1; y <= y2; ++y) {
			if (inv) {
				setPixelClip4444(y, x, color16);
			} else {
				setPixelClip4444(x, y, color16);
			}
		
			d += delta;
			if (d >= 0) {
				x += xincr;
				d -= (dy<<1);
			}
		}
	}
}

void RenderContext::drawRect(s32 x0, s32 y0, s32 x1, s32 y1, u32 color) {
	// Offset done internally.
	drawLine(x0,y0,x1,y0,color);
	drawLine(x1,y0,x1,y1,color);
	drawLine(x1,y1,x0,y1,color);
	drawLine(x0,y1,x0,y0,color);
}

void RenderContext::fillRect(s32 x0, s32 y0, s32 x1, s32 y1, u32 color, bool forceFill) {
	x0 -= offsetX;
	x1 -= offsetX;
	y0 -= offsetY;
	y1 -= offsetY;

	if (x0 < clipX0)	{ x0 = clipX0; }
	if (y0 < clipY0)	{ y0 = clipY0; }
	if (x1 > clipX1)	{ x1 = clipX1; }
	if (y1 > clipY1)	{ y1 = clipY1; }

	if (((color>>24) == 255) || (forceFill)) {
		//
		// OPTIMIZE : use fully ptr, avoid x,y increment, use memcpy32.
		//
		if (format == 4) {
			u32* pBuf = &pBuffer[x0 + (stride * y0)];

			s32 startX0 = x0;
			while (y0 < y1) {
				x0 = startX0;
				while (x0 < x1) {
					*pBuf++ = color;
					x0++;
				}
				pBuf += stride - (x1 - startX0);	// Delta Stride.
				y0++;
			}
		} else {
			u16* pBuf = (u16*)&(((u8*)pBuffer)[x0 + (stride * format * y0)]);

			u16 color16 = getTo4444(color);

			s32 startX0 = x0;
			while (y0 < y1) {
				x0 = startX0;
				while (x0 < x1) {
					*pBuf++ = color16;
					x0++;
				}
				pBuf += stride - (x1 - startX0);	// Delta Stride.
				y0++;
			}
		}
	} else {

		//
		// color is 8 Bit RGBA
		//
		u8* pCol	= (u8*)&color;

		//
		// PreAlpha
		//
		int alpha	= pCol[3]; alpha += alpha>>7;		// 0..256
		int nalpha	= 256 - alpha;						// 256..0

		int cb		= alpha * pCol[0];
		int cg		= alpha * pCol[1];
		int cr		= alpha * pCol[2];
		int ca		= alpha * pCol[3];

		//
		// Dst is also RGBA
		//
		if (format == 4) {
			u32* pBuf = &pBuffer[x0 + (stride * y0)];

			s32 startX0 = x0;
			while (y0 < y1) {
				x0 = startX0;
				while (x0 < x1) {
					u8* p8Buf = (u8*)pBuf;

					int b = ((p8Buf[0] * nalpha) + cb)>>8;
					int g = ((p8Buf[1] * nalpha) + cg)>>8;
					int r = ((p8Buf[2] * nalpha) + cr)>>8;
					int ar= ((p8Buf[3] * nalpha) + ca)>>8; // Do NOT use scale (255 vs 256)

					p8Buf	[0] = b;  // B
					p8Buf	[1] = g;  // G
					p8Buf	[2] = r;  // R
					p8Buf	[3] = ar; // A

					pBuf++;
					x0++;
				}
				pBuf += stride - (x1 - startX0);	// Delta Stride.
				y0++;
			}
		} else {
			u16* pBuf = (u16*)&(((u8*)pBuffer)[x0 + (stride * format * y0)]);

			// 8.8 -> 4.8 precision, then at correct place for addition
			cb >>= 4; cb <<= 4;
			cg >>= 4; cg <<= 8;
			cr >>= 4; cr <<= 12;
			ca >>= 4; // ca <<= 0;

			s32 startX0 = x0;
			while (y0 < y1) {
				x0 = startX0;
				while (x0 < x1) {
					u16  col = *pBuf;

					int b = (((col & 0x00F0) * nalpha) + cb)>>8;
					int g = (((col & 0x0F00) * nalpha) + cg)>>8;
					int r = (((col & 0xF000) * nalpha) + cr)>>8;
					int ar= (((col & 0x000F) * nalpha) + ca)>>8; // Do NOT use scale (255 vs 256)

					*pBuf++ = (r & 0xF000) | (g & 0x0F00) | (b & 0x00F0) | (ar & 0x000F);
					x0++;
				}
				pBuf += stride - (x1 - startX0);	// Delta Stride in pixel
				y0++;
			}
		}
	}
}

struct VDocClipContext {
	u32 outcode[5];
	int left;
	int top;
	int right;
	int bottom;
	u8* pixels;
	int stride;
};

void VDocIntersectClip(VDocClipContext* base,
					  const VDocClipContext* clip,
					  VDocClipContext* result) {
	const int clipLeft = clip->left;
	const int clipTop = clip->top;
	const int clipRight = clip->right;
	const int clipBottom = clip->bottom;
	const int baseLeft = base->left;
	const int baseTop = base->top;

	int left = (clipLeft < baseLeft) ? baseLeft : clipLeft;
	int top = (clipTop < baseTop) ? baseTop : clipTop;

	const int baseRight = base->right;
	int right = (clipRight <= baseRight) ? clipRight : baseRight;
	const int baseBottom = base->bottom;
	int bottom = (clipBottom <= baseBottom) ? clipBottom : baseBottom;

	const bool empty = (right < left) || (bottom < top);
	top = empty ? baseTop : top;
	right = empty ? baseLeft : right;
	bottom = empty ? baseTop : bottom;
	left = empty ? baseLeft : left;

	if(!result) {
		result = base;
	}
	result->left = left;
	result->top = top;
	result->right = right;
	result->bottom = bottom;
	int offset = left - baseLeft;
	const int stride = base->stride;
	offset += (top - baseTop) * stride;
	result->pixels = base->pixels + offset;
	result->stride = stride;
}

void VDocHorizontalBlur(u8* source, u8* destination, int width, int stride,
					   int height, u32 blur) {
	const int radius = blur >> 6;
	const int fraction = blur & 0x3f;
	const int diameter = radius * 2;
	const int rewind = diameter + 1;
	const int rewindOffset = -rewind;
	const int fractionalRewindOffset = -(radius + 1) * 2;
	const int normalization = 0x800000 / (int)(blur * 2 + 0x40);
	const int rowEnd = height + 4;
	if (rowEnd < -3) {
		return;
	}

	int row = -4;
	do {
		const int rowOffset = row * stride - radius;
		const u8* input = source + rowOffset;
		u8* output = destination + rowOffset;

		int sum = 0;
		for (u32 sample = 0; sample <= radius; ++sample) {
			sum += *input++;
		}
		u8* const centerEnd = output + (width + radius);

		if (fraction == 0) {
			for (int edge = radius; edge > 0; --edge) {
				*output++ = (u8)(((u32)(sum * normalization)) >> 17);
				sum += *input++;
			}

			while (output < centerEnd) {
				*output++ = (u8)(((u32)(sum * normalization)) >> 17);
				sum += (u32)*input - (u32)input[-diameter - 1];
				++input;
			}

			input += rewindOffset;
			for (int edge = radius; edge > 0; --edge) {
				*output++ = (u8)(((u32)(sum * normalization)) >> 17);
				sum -= *input++;
			}
		} else {
			for (int edge = radius; edge > 0; --edge) {
				const u32 fractionalEdge =
					((u32)*input * fraction) >> 6;
				*output++ =
					(u8)(((sum + fractionalEdge) * normalization) >> 17);
				sum += *input++;
			}

			while (output < centerEnd) {
				const u32 fractionalEdges =
					((u32)(input[fractionalRewindOffset] + *input) * fraction) >> 6;
				*output++ =
					(u8)(((sum + fractionalEdges) * normalization) >> 17);
				sum += (u32)*input - (u32)input[-diameter - 1];
				++input;
			}

			input += rewindOffset;
			for (int edge = radius; edge > 0; --edge) {
				const u32 fractionalEdge = ((u32)*input * fraction) >> 6;
				*output++ =
					(u8)(((sum + fractionalEdge) * normalization) >> 17);
				sum -= *input++;
			}
		}
		++row;
	} while (row != rowEnd);
}

void VDocVerticalBlur(u8* source, u8* destination, int width, int stride,
					 int height, u32 blur) {
	const u32 radius = blur >> 6;
	const u32 fraction = blur & 0x3f;
	const int margin = (int)radius + (fraction != 0);
	const int normalization = 0x800000 / (int)(blur * 2 + 0x40);
	const int columnCount = width + margin * 2;
	klb_assertNull(columnCount < 3000, "LABEL LARGER THAN 2 KB");

	if (columnCount >= 0) {
		memset(s_verticalBlurSums, 0,
			   (size_t)columnCount * sizeof(s_verticalBlurSums[0]));
	}

	for (int row = -margin; row < 0; ++row) {
		const int rowOffset = row * stride - margin;
		const u8* input = source + rowOffset;
		int* sum = s_verticalBlurSums;
		const int* const sumEnd = sum + columnCount;
		while (sum < sumEnd) {
			*sum++ += *input++;
		}
	}

	const int firstFollowingRow = ((int)radius + 1) * stride;
	const int firstIncludedRow = -(int)radius * stride;
	const int fractionalLeadingRow = -(int)(radius + 1) * stride;
	int row = -margin;
	while (row < height + margin) {
		const int rowOffset = row * stride - margin;
		const u8* input = source + rowOffset;
		u8* output = destination + rowOffset;
		if (fraction == 0) {
			if (row < 0) {
				const u8* following = input + firstFollowingRow;
				u8* out = output;
				int* sum = s_verticalBlurSums;
				const u8* const outEnd = out + columnCount;
				while (out < outEnd) {
					*out++ = (u8)((*sum * normalization) >> 17);
					*sum++ += *following++;
				}
			} else if (row < height) {
				const u8* following = input + firstFollowingRow;
				const u8* leading = input + firstIncludedRow;
				u8* out = output;
				int* sum = s_verticalBlurSums;
				const u8* const outEnd = out + columnCount;
				while (out < outEnd) {
					*out++ = (u8)((*sum * normalization) >> 17);
					*sum++ += (u32)*following++ - (u32)*leading++;
				}
			} else {
				const u8* leading = input + firstIncludedRow;
				u8* out = output;
				int* sum = s_verticalBlurSums;
				const u8* const outEnd = out + columnCount;
				while (out < outEnd) {
					*out++ = (u8)((*sum * normalization) >> 17);
					*sum++ -= *leading;
				}
			}
		} else {
			if (row < 0) {
				const u8* following = input + firstFollowingRow;
				u8* out = output;
				int* sum = s_verticalBlurSums;
				const u8* const outEnd = out + columnCount;
				while (out < outEnd) {
					const u32 edge = ((u32)*following * fraction) >> 6;
					*out++ = (u8)(((*sum + edge) * normalization) >> 17);
					*sum++ += *following++;
				}
			} else if (row < height) {
				const u8* following = input + firstFollowingRow;
				const u8* leading = input + firstIncludedRow;
				const u8* fractionalLeading = input + fractionalLeadingRow;
				u8* out = output;
				int* sum = s_verticalBlurSums;
				const u8* const outEnd = out + columnCount;
				while (out < outEnd) {
					const u32 edge =
						((u32)(*fractionalLeading + *following) * fraction) >> 6;
					*out++ = (u8)(((*sum + edge) * normalization) >> 17);
					*sum++ += (u32)*following++ - (u32)*leading++;
					++fractionalLeading;
				}
			} else {
				const u8* leading = input + firstIncludedRow;
				const u8* fractionalLeading = input + fractionalLeadingRow;
				u8* out = output;
				int* sum = s_verticalBlurSums;
				const u8* const outEnd = out + columnCount;
				while (out < outEnd) {
					const u32 edge =
						((u32)*fractionalLeading++ * fraction) >> 6;
					*out++ = (u8)(((*sum + edge) * normalization) >> 17);
					*sum++ -= *leading++;
				}
			}
		}
		++row;
	}
}

union VDocPixel {
	u32 value;
	struct {
		u8 blue;
		u8 green;
		u8 red;
		u8 alpha;
	} channel;
};

struct VDocFillRectangle {
	VDocFillRectangle() {}
	VDocFillRectangle(int left, int top, int right, int bottom)
	:	left(left),
		top(top),
		right(right),
		bottom(bottom)
	{}

	int left;
	int top;
	int right;
	int bottom;
};

static void VDocPushFillRectangle(VDocFillRectangle* rectangles, const u8** alphaStarts,
								  long index, const VDocClipContext* alphaMask,
								  int left, int top, int right, int bottom) {
	rectangles[index] = VDocFillRectangle(left, top, right, bottom);
	alphaStarts[index] = alphaMask->pixels
					   + (top - alphaMask->top) * alphaMask->stride
					   + (left - alphaMask->left);
}

void VDocFillClipMargins(VDocClipContext* covered, u32 color,
						 VDocClipContext* alphaMask, u8* destination,
						 int destinationStride) {
	const u32 classifyPoint = alphaMask->outcode[0];
	u32 startCode = classifyPoint;
	if (alphaMask->left > covered->left) {
		startCode |= alphaMask->outcode[1];
	} else if (alphaMask->right <= covered->left) {
		startCode |= alphaMask->outcode[2];
	}
	const int coveredTopOffset = covered->top - alphaMask->top;
	if (coveredTopOffset < 0) {
		startCode |= alphaMask->outcode[4];
	} else if (alphaMask->bottom <= covered->top) {
		startCode |= alphaMask->outcode[3];
	}

	u32 endCode = classifyPoint;
	const int coveredRightOffset = covered->right - alphaMask->left;
	if (coveredRightOffset < 0) {
		endCode |= alphaMask->outcode[1];
	} else if (alphaMask->right <= covered->right) {
		endCode |= alphaMask->outcode[2];
	}
	const int coveredBottomOffset = covered->bottom - alphaMask->top;
	if (coveredBottomOffset < 0) {
		endCode |= alphaMask->outcode[4];
	} else if (alphaMask->bottom <= covered->bottom) {
		endCode |= alphaMask->outcode[3];
	}

	if ((startCode & endCode) != 0) {
		return;
	}

	const u32 outsideLeft = covered->outcode[1];
	const u32 outsideRight = covered->outcode[2];
	const u32 outsideBottom = covered->outcode[3];
	const u32 outsideTop = covered->outcode[4];

	VDocFillRectangle rectangles[4];
	const u8* alphaStarts[4];
	long rectangleCount = 0;

	if (startCode == (outsideTop | outsideLeft)) {
		if (endCode == 0) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 covered->right, alphaMask->top,
					 alphaMask->right, covered->bottom);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->bottom,
					 alphaMask->right, alphaMask->bottom);
		} else if (endCode == outsideRight) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->bottom,
					 alphaMask->right, alphaMask->bottom);
		} else if (endCode == outsideBottom) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 covered->right, alphaMask->top,
					 alphaMask->right, alphaMask->bottom);
		} else {
			return;
		}
	} else if (startCode == outsideLeft) {
		if (endCode == outsideBottom) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 alphaMask->right, covered->top);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 covered->right, covered->top,
					 alphaMask->right, alphaMask->bottom);
		} else if (endCode == (outsideBottom | outsideRight)) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 alphaMask->right, covered->top);
		} else if (endCode == 0) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 alphaMask->right, covered->top);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->bottom,
					 alphaMask->right, alphaMask->bottom);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 covered->right, covered->top,
					 alphaMask->right, covered->bottom);
		} else if (endCode == outsideRight) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 alphaMask->right, covered->top);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->bottom,
					 alphaMask->right, alphaMask->bottom);
		} else {
			return;
		}
	} else if (startCode == outsideTop) {
		if (endCode == outsideRight) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->bottom,
					 alphaMask->right, alphaMask->bottom);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 covered->left, covered->bottom);
		} else if (endCode == outsideBottom) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 covered->left, alphaMask->bottom);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 covered->right, alphaMask->top,
					 alphaMask->right, alphaMask->bottom);
		} else if (endCode == (outsideBottom | outsideRight)) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 covered->left, alphaMask->bottom);
		} else {
			return;
		}
	} else if (startCode == 0) {
		if (endCode == 0) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 covered->right, covered->top,
					 alphaMask->right, covered->bottom);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->top,
					 covered->left, covered->bottom);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 alphaMask->right, covered->top);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->bottom,
					 alphaMask->right, alphaMask->bottom);
		} else if (endCode == (outsideBottom | outsideRight)) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 alphaMask->right, covered->top);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->top,
					 covered->left, alphaMask->bottom);
		} else if (endCode == outsideRight) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 alphaMask->right, covered->top);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->bottom,
					 alphaMask->right, alphaMask->bottom);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->top,
					 covered->left, covered->bottom);
		} else if (endCode == outsideBottom) {
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, alphaMask->top,
					 alphaMask->right, covered->top);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 alphaMask->left, covered->top,
					 covered->left, alphaMask->bottom);
			VDocPushFillRectangle(rectangles, alphaStarts, rectangleCount++, alphaMask,
					 covered->right, covered->top,
					 alphaMask->right, alphaMask->bottom);
		} else {
			return;
		}
	} else {
		return;
	}


	const VDocFillRectangle* rectangle = rectangles;
	for (long rectangleIndex = 0;
		 rectangleIndex < rectangleCount;
		 ++rectangleIndex, ++rectangle) {
		const int rowSpan =
			(rectangle->bottom - rectangle->top) * destinationStride;
		if (rowSpan <= 0) {
			continue;
		}

		u8* destinationRow =
			destination + rectangle->top * destinationStride
		  + rectangle->left * (int)sizeof(VDocPixel);
		u8* const destinationEnd = destinationRow + rowSpan;
		const int rectangleWidth = rectangle->right - rectangle->left;
		const u8* alphaRow = alphaStarts[rectangleIndex];
		do {
			const u8* alpha = alphaRow;
			const u8* const alphaEnd = alphaRow + rectangleWidth;
			VDocPixel* pixel = (VDocPixel*)destinationRow;
			while (alpha < alphaEnd) {
				const u8 coverage = *alpha++;
				pixel->value = color;
				pixel->channel.alpha = coverage;
				++pixel;
			}
			destinationRow += destinationStride;
			alphaRow += alphaMask->stride;
		} while (destinationRow < destinationEnd);
	}
}

static void VDocIntersectCompositeClip(VDocClipContext* base,
									  const VDocClipContext* clip,
									  VDocClipContext* result) {
	const int clipLeft = clip->left;
	const int clipTop = clip->top;
	const int clipRight = clip->right;
	const int clipBottom = clip->bottom;
	const int baseLeft = base->left;
	const int baseTop = base->top;

	int left = (clipLeft < baseLeft) ? baseLeft : clipLeft;
	int top = (clipTop < baseTop) ? baseTop : clipTop;

	const int baseRight = base->right;
	int right = (clipRight <= baseRight) ? clipRight : baseRight;
	const int baseBottom = base->bottom;
	int bottom = (clipBottom <= baseBottom) ? clipBottom : baseBottom;

	const bool empty = (right < left) || (bottom < top);
	top = empty ? baseTop : top;
	right = empty ? baseLeft : right;
	bottom = empty ? baseTop : bottom;
	left = empty ? baseLeft : left;

	if (!result) {
		result = base;
	}
	result->left = left;
	result->top = top;
	result->right = right;
	result->bottom = bottom;
	int offset = left - baseLeft;
	const int stride = base->stride;
	offset += (top - baseTop) * stride;
	result->pixels = base->pixels + offset;
	result->stride = stride;
}

void VDocCompositeAlphaLayers(VDocClipContext* destination,
							  VDocClipContext* body,
							  VDocClipContext* shadow,
							  u32 bodyColor,
							  u32 shadowColor) {
	const u32 bodyBlue = bodyColor & 0xff;
	const u32 bodyGreen = (bodyColor >> 8) & 0xff;
	const u32 bodyRed = (bodyColor >> 16) & 0xff;
	const u32 shadowBlue = shadowColor & 0xff;
	const u32 shadowGreen = (shadowColor >> 8) & 0xff;
	const u32 shadowRed = (shadowColor >> 16) & 0xff;
	const u32 packedBodyColor =
		(bodyBlue << 16) | (bodyGreen << 8) | bodyRed;
	const u32 packedShadowColor =
		(shadowBlue << 16) | (shadowGreen << 8) | shadowRed;

	VDocIntersectCompositeClip(body, destination, NULL);
	VDocIntersectCompositeClip(shadow, destination, NULL);

	VDocClipContext bodyOverlap = { { 0, 1, 2, 4, 8 } };
	VDocClipContext shadowOverlap = { { 0, 1, 2, 4, 8 } };
	VDocIntersectCompositeClip(body, shadow, &bodyOverlap);
	VDocIntersectCompositeClip(shadow, body, &shadowOverlap);

	const int overlapWidth = bodyOverlap.right - bodyOverlap.left;
	const int overlapHeight = bodyOverlap.bottom - bodyOverlap.top;
	const int bodySpan = overlapHeight * bodyOverlap.stride;
	if (bodySpan > 0) {
		u8* output =
			destination->pixels
		  + bodyOverlap.top * destination->stride
		  + bodyOverlap.left * 4;
		const u8* bodyRow = bodyOverlap.pixels;
		const u8* shadowRow = shadowOverlap.pixels;
		const u8* const bodyEnd = bodyRow + bodySpan;

		while (bodyRow < bodyEnd) {
			if (overlapWidth > 0) {
				const u8* bodyPixel = bodyRow;
				const u8* shadowPixel = shadowRow;
				u8* outputPixel = output;
				const u8* const bodyRowEnd = bodyPixel + overlapWidth;
				while (bodyPixel < bodyRowEnd) {
					const u32 bodyCoverage = *bodyPixel++;
					const u32 shadowCoverage = *shadowPixel++;
					const u32 visibleShadow =
						((0x100 - bodyCoverage) * shadowCoverage) >> 8;
					const u32 combinedAlpha = bodyCoverage + visibleShadow;
					const u32 unassignedWeight = 0xff - combinedAlpha;
					u32 bodyWeight = bodyCoverage;
					u32 shadowWeight = visibleShadow;
					if (bodyCoverage > shadowCoverage) {
						bodyWeight += unassignedWeight;
					} else {
						shadowWeight += unassignedWeight;
					}

					const u32 red =
						bodyWeight * bodyRed + shadowWeight * shadowRed;
					const u32 green =
						bodyWeight * bodyGreen + shadowWeight * shadowGreen;
					const u32 blue =
						bodyWeight * bodyBlue + shadowWeight * shadowBlue;
					outputPixel[0] = (u8)(red >> 8);
					outputPixel[1] = (u8)(green >> 8);
					outputPixel[2] = (u8)(blue >> 8);
					outputPixel[3] = (u8)combinedAlpha;
					outputPixel += 4;
				}
			}
			bodyRow += bodyOverlap.stride;
			shadowRow += shadowOverlap.stride;
			output += destination->stride;
		}
	}

	VDocFillClipMargins(body, packedShadowColor, shadow,
					   destination->pixels, destination->stride);
	VDocFillClipMargins(shadow, packedBodyColor, body,
					   destination->pixels, destination->stride);
}

void VDocRasterizeGlyphShadow(float scaleX, float scaleY,
							  RenderContext* context,
							  int x, int y, int width, int height, int ascent,
							  const char* text, u32 bodyColor, u32 shadowColor,
							  void* font, int shadowOffsetX, int shadowOffsetY,
							  u32 blur, int blurPasses) {
	if ((width <= 0) || (height <= 0)) {
		return;
	}

	const u32 blurAmount = (blur <= 0x100) ? blur : 0x100;
	const int blurMargin = (int)((blurAmount + 0x3f) >> 6);
	const int paddedStride = width + 8;
	const int alphaPlaneSize = (height + 8) * paddedStride;
	const int allocationSize = alphaPlaneSize + 0x20 + width * 4;

	u8* shadowAllocation = new u8[allocationSize];
	u8* blurAllocation = new u8[allocationSize];
	const int clearOffset = width * 2 + 0x10;
	u8* shadowClearStart = shadowAllocation + clearOffset;
	u8* blurClearStart = blurAllocation + clearOffset;
	const int coverageOffset = width * 4 + 0x24;
	u8* shadowCoverage = shadowClearStart + coverageOffset;
	u8* blurCoverage = blurClearStart + coverageOffset;
	memset(shadowClearStart, 0, alphaPlaneSize);
	memset(blurClearStart, 0, alphaPlaneSize);

	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	platform.setNativeFont(context->useNativeFont);
	platform.renderText(text, font, bodyColor,
						(u16)width, (u16)height, shadowCoverage,
						(s16)paddedStride, 0, (s16)ascent, 1,
						scaleX, scaleY);

	u8* bodyAllocation = NULL;
	u8* bodyCoverage = shadowCoverage;
	if (blurAmount != 0) {
		bodyAllocation = new u8[allocationSize];
		u8* bodyClearStart = bodyAllocation + clearOffset;
		memcpy(bodyClearStart, shadowClearStart, alphaPlaneSize);

		if (blurPasses != 0) {
			do {
				VDocHorizontalBlur(shadowCoverage, blurCoverage, width,
								   paddedStride, height, blurAmount);
				VDocVerticalBlur(blurCoverage, shadowCoverage, width,
								 paddedStride, height, blurAmount);
				--blurPasses;
			} while (blurPasses != 0);
		}
		bodyCoverage = bodyClearStart + coverageOffset;
	}

	VDocClipContext destinationContext;
	VDocClipContext shadowContext;
	VDocClipContext bodyContext;
	destinationContext.outcode[0] = 0;
	destinationContext.outcode[1] = 1;
	destinationContext.outcode[2] = 2;
	destinationContext.outcode[3] = 4;
	destinationContext.outcode[4] = 8;
	destinationContext.left = 0;
	destinationContext.top = 0;
	destinationContext.right = context->targetWidth;
	destinationContext.bottom = context->targetHeight;

	const int originX = x - context->offsetX;
	const int originY = y - ascent - context->offsetY;
	const int adjustedShadowX = shadowOffsetX - blurMargin;
	const int adjustedShadowY = shadowOffsetY - blurMargin;
	const int positiveShadowX =
		(adjustedShadowX < 0) ? 0 : adjustedShadowX;
	const int positiveShadowY =
		(adjustedShadowY < 0) ? 0 : adjustedShadowY;
	const int bodyCompensationX =
		(adjustedShadowX < 0) ? -adjustedShadowX : 0;
	const int bodyCompensationY =
		(adjustedShadowY < 0) ? -adjustedShadowY : 0;

	shadowContext.outcode[0] = 0;
	shadowContext.outcode[1] = 1;
	shadowContext.outcode[2] = 2;
	shadowContext.outcode[3] = 4;
	shadowContext.outcode[4] = 8;
	shadowContext.left = originX + positiveShadowX;
	shadowContext.top = originY + positiveShadowY;
	shadowContext.right = shadowContext.left + width + blurMargin;
	shadowContext.bottom = shadowContext.top + height + blurMargin;

	bodyContext.outcode[0] = 0;
	bodyContext.outcode[1] = 1;
	bodyContext.outcode[2] = 2;
	bodyContext.outcode[3] = 4;
	bodyContext.outcode[4] = 8;
	bodyContext.left = originX + bodyCompensationX;
	bodyContext.top = originY + bodyCompensationY;
	bodyContext.right = bodyContext.left + width;
	bodyContext.bottom = bodyContext.top + height;

	destinationContext.pixels = (u8*)context->pBuffer;
	destinationContext.stride = context->stride * (int)sizeof(VDocPixel);

	if (bodyCoverage) {
		bodyContext.pixels = bodyCoverage;
		bodyContext.stride = paddedStride;
		shadowContext.pixels =
			shadowCoverage - blurMargin * (paddedStride + 1);
		shadowContext.stride = paddedStride;
		VDocCompositeAlphaLayers(&destinationContext, &bodyContext,
								 &shadowContext, bodyColor, shadowColor);
	}

	delete[] bodyAllocation;
	delete[] blurAllocation;
	delete[] shadowAllocation;
}

#include "CKLBTextTempBuffer.h"

// #define INTERNAL_FILL_WITH_COLOR_TEXPACKER
#ifdef INTERNAL_FILL_WITH_COLOR_TEXPACKER

static u32 colorcount = 0;
u32 getTestColor() {
	colorcount = (colorcount+1) & 0xF;

	u32 colorFill = 0x80FFFFFF;

	switch (colorcount) {
	case 0:	colorFill = 0xFF00FF00; // Green
		break;
	case 1:	colorFill = 0xFFFF0000; // Red
		break;
	case 2:	colorFill = 0xFF0000FF; // Blue
		break;
	case 3:	colorFill = 0xFFFF00FF; // Magenta
		break;
	case 4:	colorFill = 0xFF00FFFF; // Yellow
		break;
	case 5:	colorFill = 0xFFFFFF00; // Cyan
		break;

	case 6:	colorFill = 0xFF80FF80;
		break;
	case 7:	colorFill = 0xFFFF8080;
		break;
	case 8:	colorFill = 0xFF8080FF;
		break;
	case 9:	colorFill = 0xFFFF80FF;
		break;
	case 10:colorFill = 0xFF80FFFF;
		break;
	case 11:colorFill = 0xFFFFFF80;
		break;

	case 12:colorFill = 0xFF80FF00;
		break;
	case 13:colorFill = 0xFFFF8000;
		break;
	case 14:colorFill = 0xFF0080FF;
		break;
	case 15:colorFill = 0xFFFF0080;
		break;
	}

	return colorFill;
}
#endif

void RenderContext::drawText(s32 x, s32 y, char* string, u32 color, void* font, float scaleX, float scaleY) {
	IPlatformRequest& platform = CPFInterface::getInstance().platform();

	x -= offsetX;
	y -= offsetY;

	platform.setNativeFont(useNativeFont);
	platform.renderText(	string,
							font,
							color,
							this->targetWidth,
							this->targetHeight,
							(u8*)pBuffer,
							this->stride * format,	// Byte stride
							x,
							y,
							format,
							scaleX,
							scaleY);
}

void RenderContext::drawImage(s32 x , s32 y , SDrawCommand* pCommand  , u8 alpha) {
	CKLBImageAsset* pImg = (CKLBImageAsset*)pCommand->ptr;

	x -= offsetX;
	y -= offsetY;

	// Screen Space size
	s32 sdx = pCommand->x1 - pCommand->x0;
	s32 sdy = pCommand->y1 - pCommand->y0;
	// Screen Image Corner
	s32 x0 = x;
	s32 y0 = y;
	s32 x1 = x+sdx;
	s32 y1 = y+sdy;
	// Texture Space Top Left
	s32 texX	= pCommand->sx0;
	s32 texY	= pCommand->sy0;

	// Completly outside : skip
	if ((pImg == NULL) || (x1 <= clipX0) || (y1 <= clipY0) || (x0 >= clipX1) || (y0 >= clipY1)) {
		return;
	}

	// Fully inside or partially clipped.
	s32 delta;

	//
	// Top Left Corner Test
	//

	delta = x0 - clipX0;
	// Shorten width  ?
	if (delta < 0)	{	sdx += delta;
						// Left corner in texture space
						if (pCommand->swap) { texY -= delta; } else { texX -= delta; }
						x0 = clipX0;
					}
	// Shorten height ?
	delta = y0 - clipY0;
	if (delta < 0)	{	sdy += delta;	x0 = clipX0;	
						// Left corner in texture space
						if (pCommand->swap) { texX -= delta; } else { texY -= delta; }
					}

	//
	// Bottom Right Corner Test
	//

	delta = clipX1 - x1;
	// Shorten width  ?
	if (delta < 0) { sdx += delta; }
	// Shorten height ?
	delta = clipY1 - y1;
	if (delta < 0) { sdy += delta; }

	u8* pix		= pImg->m_pTextureAsset->m_softTexture;
	s32 lstride	= pImg->m_pTextureAsset->m_width;

	pix = &pix[(texX + (texY * lstride)) * 4];

	lstride *= 4;

	//
	// === Draw ===
	//
	u8* pBuf = (u8*)&pBuffer[x0 + (stride * y0)];

	s32 jmpStrideBuf = (stride - sdx) * 4;
	s32 jmpStrideHoriz;
	s32 jmpStrideVert;

	if (pCommand->swap) {
		jmpStrideHoriz	= lstride;
		jmpStrideVert	= (lstride * (-sdx)) + 4; // Roll back Y pixel in texture space, advance +1 pixel
	} else {
		jmpStrideHoriz	= 4;
		jmpStrideVert	= lstride - (sdx * 4);
	}

	int galpha		= alpha + (alpha>>7);		// 0..256
	// int gnalpha		= 256 - galpha;				// 256..0

	if (format == 4) {
		for (int y=0; y < sdy; y++) {
			for (int x=0; x < sdx; x++) {
				// 0..255 Alpha
				int ca		= (galpha * pix[3] >> 8);	// Pixel Alpha * Global Alpha
				int alpha	= ca + (ca >> 7);
				int nAlpha  = 256 - alpha;

				pBuf[0] = ((pBuf[0] * nAlpha) + (alpha * pix[0])) >> 8; // R
				pBuf[1] = ((pBuf[1] * nAlpha) + (alpha * pix[1])) >> 8; // G
				pBuf[2] = ((pBuf[2] * nAlpha) + (alpha * pix[2])) >> 8; // B
				pBuf[3] = ((pBuf[3] * nAlpha) + (alpha * pix[3])) >> 8; // A

				pBuf+=4;
				pix += jmpStrideHoriz;
			}
			pBuf += jmpStrideBuf;
			pix  += jmpStrideVert;
		}
	} else {
		klb_assertAlways("IMAGE RENDER NOT SUPPORTED in 4444 for virtual doc");
	}
}

static inline void mergeAlphaMax(u8* destination, const u8* source, u32 pixelCount) {
	const u8* sourceEnd = source + ((long)pixelCount * 4);
	while (source < sourceEnd) {
		u8 alpha = *source;
		if (*source <= *destination) {
			alpha = *destination;
		}
		*destination = alpha;
		source += 4;
		destination += 4;
	}
}

u8* CKLBNodeVirtualDocument::ensureOutlineBuffer(u32 /*commandIndex*/, u32 requiredSize) {
	u8* buffer = s_outlineBuffer;
	if (s_outlineBufferSize < requiredSize) {
		delete[] buffer;
		buffer = new u8[requiredSize];
		s_outlineBuffer = buffer;
		s_outlineBufferSize = requiredSize;
	}
	return buffer;
}

void CKLBNodeVirtualDocument::copyOutlineRows(
	const void* source,
	void* destination,
	u32 height,
	u32 width,
	s32 sourceStride,
	s32 destinationStride
) {
	const u64 sourceSpan = (u32)(sourceStride * height);
	if (sourceSpan != 0) {
		u64 sourceOffset = 0;
		u8* destinationRow = (u8*)destination;
		do {
			memcpy(destinationRow, (const u8*)source + sourceOffset, width * 4);
			destinationRow += destinationStride;
			sourceOffset += sourceStride;
		} while (sourceSpan != sourceOffset);
	}
}

void CKLBNodeVirtualDocument::mergeOutlineRows(
	const u8* source,
	s32 sourceStride,
	u8* destination,
	s32 destinationStride,
	u32 width,
	u32 height
) {
	const u32 rowBytes = width * 4;
	for (u32 row = 0; row < height; ++row) {
		const u8* sourceEnd = source + rowBytes;
		const u8* sourcePixel = source;
		u8* destinationPixel = destination;
		while (sourcePixel < sourceEnd) {
			u8 alpha = *sourcePixel;
			if (*sourcePixel <= *destinationPixel) {
				alpha = *destinationPixel;
			}
			*destinationPixel = alpha;
			sourcePixel += 4;
			destinationPixel += 4;
		}
		source += sourceStride;
		destination += destinationStride;
	}
}

void CKLBNodeVirtualDocument::applyTextOutline(
	u32 commandIndex,
	u8 directions,
	void* destination,
	s32 width,
	s32 height,
	s32 byteStride
) {
	const u32 directionFlags = directions;
	const s32 packedStride = width * 4;
	const u32 requiredSize = (u32)(packedStride * height);

	u8* scratch = ensureOutlineBuffer(commandIndex, requiredSize);
	const u32 sourceSpan = (u32)(byteStride * height);
	copyOutlineRows(destination, scratch, (u32)height, (u32)width,
					byteStride, packedStride);

	u8* destinationAlpha = (u8*)destination + 3;

	if (directionFlags & 2) {
		if ((directionFlags & 4) == 0) {
			const s32 adjacentRowCount = (s32)height - 1;
			if (adjacentRowCount != 0) {
				const u8* sourceRow = scratch + 3;
				u8* targetRow = destinationAlpha;
				s32 row = 0;
				do {
					mergeAlphaMax(targetRow + byteStride, sourceRow, (u32)width);
					sourceRow += packedStride;
					targetRow += byteStride;
					++row;
				} while (row != adjacentRowCount);
			}
		}

		if ((directionFlags & 4) != 0 || height != 1) {
			const s32 adjacentRowCount = (s32)height - 1;
			if (adjacentRowCount != 0) {
				const u8* sourceRow = scratch + 3 + packedStride;
				u8* targetRow = destinationAlpha;
				s32 row = 0;
				do {
					mergeAlphaMax(targetRow, sourceRow, (u32)width);
					sourceRow += packedStride;
					targetRow += byteStride;
					++row;
				} while (row != adjacentRowCount);
			}
		}

		const s32 extendedRowCount = (s32)height - 2;
		if ((directionFlags & 0x20) != 0 && extendedRowCount != 0) {
			const u8* sourceRow = scratch + 3;
			u8* targetRow = destinationAlpha + (byteStride * 2);
			s32 row = 0;
			do {
				mergeAlphaMax(targetRow, sourceRow, (u32)width);
				sourceRow += packedStride;
				targetRow += byteStride;
				++row;
			} while (row != extendedRowCount);
			sourceRow = scratch + 3 + (packedStride * 2);
			targetRow = destinationAlpha;
			row = 0;
			do {
				mergeAlphaMax(targetRow, sourceRow, (u32)width);
				sourceRow += packedStride;
				targetRow += byteStride;
				++row;
			} while (row != extendedRowCount);
		}

		if ((directionFlags & 1) != 0 && sourceSpan != 0) {
			copyOutlineRows(destination, scratch, (u32)height, (u32)width,
							byteStride, packedStride);
		}
	}

	if (directionFlags & 1) {
		if (height != 0) {
			const u8* sourceRow = scratch + 7;
			u8* targetRow = destinationAlpha;
			s32 row = 0;
			do {
				mergeAlphaMax(targetRow, sourceRow, (u32)width - 1);
				sourceRow += packedStride;
				targetRow += byteStride;
				++row;
			} while (row != height);
			if ((directionFlags & 8) == 0) {
				sourceRow = scratch + 3;
				targetRow = (u8*)destination + 7;
				row = 0;
				do {
					mergeAlphaMax(targetRow, sourceRow, (u32)width - 1);
					sourceRow += packedStride;
					targetRow += byteStride;
					++row;
				} while (row != height);
			}
		}

		if ((directionFlags & 0x10) != 0 && height != 0) {
			const u8* sourceRow = scratch + 0xb;
			u8* targetRow = destinationAlpha;
			s32 row = 0;
			do {
				mergeAlphaMax(targetRow, sourceRow, (u32)width - 2);
				sourceRow += packedStride;
				targetRow += byteStride;
				++row;
			} while (row != height);
			sourceRow = scratch + 3;
			targetRow = (u8*)destination + 0xb;
			row = 0;
			do {
				mergeAlphaMax(targetRow, sourceRow, (u32)width - 2);
				sourceRow += packedStride;
				targetRow += byteStride;
				++row;
			} while (row != height);
		}
	}
}

void VDocApplyTextOutlineAlternate(
	u32 /*commandIndex*/,
	s32 strength,
	void* destination,
	s32 width,
	s32 height,
	s32 destinationStride
) {
	if (strength == 0) {
		return;
	}

	const u32 rowBytes = (u32)(width * 4);
	const s32 copyBytes = width * 4 - 3;
	u8* scratch = s_outlineBuffer;
	if (s_outlineBufferSize < rowBytes) {
		delete[] scratch;
		scratch = new u8[rowBytes];
		s_outlineBuffer = scratch;
		s_outlineBufferSize = rowBytes;
	}

	if (!scratch) {
		return;
	}
	const u32 destinationSpan = (u32)(height * destinationStride);
	if (destinationSpan < 4) {
		return;
	}

	u8* destinationAlpha = (u8*)destination + 3;
	u8* const destinationEnd = (u8*)destination + destinationSpan;
	do {
		memcpy(scratch, destinationAlpha, copyBytes);

		u32 alpha =
			(((u32)scratch[4] * strength) >> 9) + (u32)destinationAlpha[0];
		u8 result;
		if ((alpha & 0xff00) > 0xff) {
			result = 0xff;
		} else {
			result = (u8)alpha;
		}
		destinationAlpha[0] = result;

		const u8* scratchPixel = scratch + 4;
		const u8* const finalScratchPixel = scratch + (rowBytes - 4);
		u8* destinationPixel = destinationAlpha + 4;
		if (4 < rowBytes - 4) {
			do {
				const u32 neighbourAlpha =
					(u32)scratchPixel[4] + scratchPixel[-4];
				alpha =
					((neighbourAlpha * strength) >> 9)
				  + (u32)*scratchPixel;
				if ((alpha & 0xff00) > 0xff) {
					result = 0xff;
				} else {
					result = (u8)alpha;
				}
				*destinationPixel = result;
				destinationPixel += 4;
				scratchPixel += 4;
			} while (scratchPixel < finalScratchPixel);
		}

		alpha =
			(((u32)scratchPixel[-4] * strength) >> 9)
		  + (u32)*scratchPixel;
		if ((alpha & 0xff00) > 0xff) {
			result = 0xff;
		} else {
			result = (u8)alpha;
		}
		*destinationPixel = result;

		destinationAlpha += destinationStride;
	} while (destinationAlpha < destinationEnd);
}

#ifdef DEBUG_TEXTURE_PACKER

struct VirtualDocElement {
	static VirtualDocElement* getDocElement(CKLBNodeVirtualDocument* pDoc);
	VirtualDocElement*			pNext;
	CKLBNodeVirtualDocument*	pDoc;
};

VirtualDocElement*	gDocumentList = NULL;
CKLBNodeVirtualDocument*	pIgnore	= NULL;

void setIgnoreVirtualDoc(CKLBNodeVirtualDocument* pDoc) {
	pIgnore = pDoc;	
}

VirtualDocElement* VirtualDocElement::getDocElement(CKLBNodeVirtualDocument* pDoc) {
	VirtualDocElement* pElem = new VirtualDocElement();
	pElem->pDoc = pDoc;
	pElem->pNext= NULL;
	return pElem;
}

void registerVirtualDoc(CKLBNodeVirtualDocument* pDoc) {
	VirtualDocElement* pNewElem = VirtualDocElement::getDocElement(pDoc);
	if (pNewElem) {
		pNewElem->pNext	= gDocumentList;
		gDocumentList	= pNewElem;
	} else {
		klb_assertAlways("allo failure");
	}
}

void unregisterVirtualDoc(CKLBNodeVirtualDocument* pDoc) {
	VirtualDocElement* pElem = gDocumentList;
	VirtualDocElement* pPrev = NULL;

	while (pElem) {
		if (pElem->pDoc == pDoc) {
			break;
		}
		pPrev = pElem;
		pElem = pElem->pNext;
	}

	if (pElem) {
		if (pPrev == NULL) {
			gDocumentList = pElem->pNext;
		} else {
			pPrev->pNext  = pElem->pNext;
		}
		delete pElem;
	} else {
		klb_assertAlways("Item not found : already removed from list");
	}
}

void checkVirtualDocState() {
	VirtualDocElement* pElem = gDocumentList;
	while (pElem) {
		if (pElem->pDoc != pIgnore) {
			pElem->pDoc->check();
		}
		pElem = pElem->pNext;
	}
}

void dumpVirtualDocState(void* pDoc) {
	printf("=== Dump Virtual Doc List ===\n");
	VirtualDocElement* pElem = gDocumentList;
	while (pElem) {
		if (pElem->pDoc == pDoc) {
			printf("Obj : %8X FOUND !!! \n", pElem->pDoc);
		} else {
			printf("Obj : %8X\n", pElem->pDoc);
		}
		pElem = pElem->pNext;
	}
	printf("=== End Dump ===\n");
}

#endif

CKLBNodeVirtualDocument::CKLBNodeVirtualDocument(u32 texturePackerIndex) {
	m_listHead = NULL;
	m_fontScaleX = 1.0f;
	m_fontScaleY = 1.0f;
	m_posX = 0;
	m_posY = 0;
	m_commandArray = NULL;
	m_commandMaxCount = 0;
	m_CurrentCommand = 0;
	s_documentCount++;
	m_bgColor = 0;
	m_documentWidth = 0;
	m_documentHeight = 0;
	m_scrollStep = 0.0f;
	m_scrollOffset = 0.0f;
	m_scrollPosition = 0.0f;
	m_viewPortHeight = 0;
	m_viewPortWidth = 0;
	m_currTile = 0;
	m_prevTile = 0x7FFF;
	m_scroll = 0;
	m_prevScroll = 1;
	m_bDisplay = true;
	m_bHasChanged = false;
	m_marqueeState = -1;
	m_marqueeTimeAccum = 0;
	m_marqueeV = 0;
	m_marqueePos = 0;
	m_marqueeA = 0;
	m_marqueeB = 0;
	m_marqueeC = 0;
	m_texturePackerIndex = texturePackerIndex;
	m_marqueeStopped = true;
	m_useNativeFont = false;
	m_nativeFontSize = 3;
	m_deleteRender = false; // Force own management of sprite on destruction.
	m_drawarea[0].tile = NULL;
	m_drawarea[0].softwareBufTile = NULL;
	m_isVertical = false;
	m_bScroll = false;
	m_fitEnabled = false;
	m_fitModeExtended = false;
	m_drawarea[0].surf_handle = NULL_IDX;
	m_drawarea[1].tile = NULL;
	m_drawarea[1].softwareBufTile = NULL;
	m_drawarea[1].surf_handle = NULL_IDX;
	/*
	m_softwareBufferTile[0] = NULL;
	m_softwareBufferTile[1] = NULL;
	m_tile				[0] = NULL;
	m_tile				[1] = NULL;
	*/
	for (int n = 0; n < 5; n++) {
		font[n] = NULL;
	}
	m_format = TexturePacker::getCurrentModeTexture();
	m_listPrev = this;
	m_listNext = this;
	renderContext.useNativeFont = false;
#ifdef DEBUG_TEXTURE_PACKER
	registerVirtualDoc(this);
#endif
}

TexturePacker& CKLBNodeVirtualDocument::texturePacker() {
	return TexturePacker::getInstance(static_cast<u8>(m_texturePackerIndex));
}

CKLBNodeVirtualDocument::~CKLBNodeVirtualDocument() {
	DocumentLifetimeRelease releaseDocumentLifetime;
	clearRessources();
	clearFontResources();
	clearRenderResources();
	freeDocument();
#ifdef DEBUG_TEXTURE_PACKER
	unregisterVirtualDoc(this);
	texturePacker().scan(this);
#endif
}

void CKLBNodeVirtualDocument::clearRessources() {
	TexturePacker& packer = texturePacker();
	CKLBNodeVirtualDocument* previous = m_listPrev;
	CKLBNodeVirtualDocument* next = m_listNext;
	previous->m_listNext = next;
	next->m_listPrev = previous;

	bool listBecomesEmpty = previous == next && previous == this;
	CKLBNodeVirtualDocument* newOwner = listBecomesEmpty ? NULL : next;
	if (m_listHead) {
		*m_listHead = newOwner;
	}
	m_listNext = this;
	m_listPrev = this;

	if (listBecomesEmpty) {
		if (m_drawarea[0].surf_handle != NULL_IDX) {
			packer.releaseSurface(m_drawarea[0].surf_handle);
		}
		if (m_drawarea[1].surf_handle != NULL_IDX) {
			packer.releaseSurface(m_drawarea[1].surf_handle);
		}
	} else {
		klb_assertNull(newOwner, "Should never be null here");
		if (m_drawarea[0].surf_handle != NULL_IDX) {
			packer.setSurfaceOwner(m_drawarea[0].surf_handle, newOwner);
		}
		if (m_drawarea[1].surf_handle != NULL_IDX) {
			packer.setSurfaceOwner(m_drawarea[1].surf_handle, newOwner);
		}
	}

	m_drawarea[0].surf_handle = NULL_IDX;
	m_drawarea[0].softwareBufTile = NULL;
	m_drawarea[1].surf_handle = NULL_IDX;
	m_drawarea[1].softwareBufTile = NULL;
}

void CKLBNodeVirtualDocument::clearFontResources() {
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	for (int i = 0; i < 5; i++) {
		if (font[i]) {
			platform.setNativeFont(m_useNativeFont);
			platform.deleteFontResource(font[i]);
		}
		font[i] = NULL;
	}
}

void CKLBNodeVirtualDocument::clearRenderResources() {
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	if (m_drawarea[0].tile) {
		setRender(NULL, 0);
		renderingManager.releaseCommand(m_drawarea[0].tile);
	}
	if (m_drawarea[1].tile) {
		setRender(NULL, 1);
		renderingManager.releaseCommand(m_drawarea[1].tile);
	}
	m_drawarea[0].tile = NULL;
	m_drawarea[1].tile = NULL;
}

void CKLBNodeVirtualDocument::addToDocumentList(CKLBNodeVirtualDocument** listHead) {
	klb_assertNull(listHead, "Null Ptr");
	klb_assertNull(m_listPrev == this, "Invalid");
	klb_assertNull(m_listNext == this, "Invalid");
	CKLBNodeVirtualDocument* head = *listHead;
	if (head) {
		CKLBNodeVirtualDocument* tail = head->m_listPrev;
		m_listNext = head;
		m_listPrev = tail;
		tail->m_listNext = this;
		head->m_listPrev = this;
	} else {
		*listHead = this;
	}
}

bool CKLBNodeVirtualDocument::removeFromDocumentList(CKLBNodeVirtualDocument** listHead) {
	CKLBNodeVirtualDocument* previous = m_listPrev;
	CKLBNodeVirtualDocument* next = m_listNext;
	previous->m_listNext = next;
	next->m_listPrev = previous;
	bool onlyDocument = previous == next;
	bool wasHead = previous == this;
	bool listBecomesEmpty = onlyDocument && wasHead;
	CKLBNodeVirtualDocument* newHead = listBecomesEmpty ? NULL : next;
	*listHead = newHead;
	if (m_listHead) {
		*m_listHead = newHead;
	}
	m_listNext = this;
	m_listPrev = this;
	return listBecomesEmpty;
}

void CKLBNodeVirtualDocument::setDocumentList(CKLBNodeVirtualDocument** listHead) {
	if (m_listHead != listHead) {
		if (m_listHead) {
			clearRessources();
		}
		m_listHead = listHead;
		if (listHead) {
			addToDocumentList(listHead);
		}
	}
}

void CKLBNodeVirtualDocument::check() {
	klb_assert(m_drawarea[0].softwareBufTile, "VIRTUAL DOC NOT FREED BUT SET TO NULL");
	klb_assert(m_drawarea[0].tile,            "VIRTUAL DOC NOT FREED BUT SET TO NULL");

/*
	if (m_softwareBufferTile[0] == 0) {
		klb_assertAlways("VIRTUAL DOC NOT FREED BUT SET TO NULL");
	}
	if (m_tile[0]==0) {
		klb_assertAlways("VIRTUAL DOC NOT FREED BUT SET TO NULL");
	}
*/
}

void CKLBNodeVirtualDocument::docTextureCompaction(void* ctx, u32 oldsurface, u32 newSurface) {
	CKLBNodeVirtualDocument* owner = (CKLBNodeVirtualDocument*)ctx;
	CKLBNodeVirtualDocument* document = owner;
	if (document->m_listHead) {
		document = *document->m_listHead;
	}
	TexturePacker& packer = owner->texturePacker();
	CKLBNodeVirtualDocument* first = document;
	do {
		u32 index = 0;
		bool second = false;
		if (document->m_drawarea[0].surf_handle != (u16)oldsurface) {
			index = 1;
			second = true;
			if (document->m_drawarea[1].surf_handle != (u16)oldsurface) {
				document = document->m_listNext;
				continue;
			}
		}

		VDOCDRAW& draw = document->m_drawarea[index];
		draw.surf_handle = newSurface;
		float u0;
		float v0;
		float u1;
		float v1;
		float stepU;
		float stepV;
		u32* softwareBuffer;
		packer.getSurfaceInfo(newSurface, softwareBuffer,
						  u0, v0, u1, v1, stepU, stepV);
		klb_assertNull(softwareBuffer == draw.softwareBufTile,
					   "Consistency of the buffer cannot be taken. ");

		draw.leftU = u0;
		draw.upV = v0;
		draw.rightU = u1;
		draw.bottomV = v1;
		draw.stepU = stepU;
		draw.stepV = stepV;
		if (draw.tile) {
			draw.tile->m_pTexture = packer.getTextureUsage(newSurface);
		}
		document->updateDynSprites(second);
		document = document->m_listNext;
	} while (document != first);
}

void CKLBNodeVirtualDocument::docTextureRelease(void* ctx) {
	CKLBNodeVirtualDocument* document = (CKLBNodeVirtualDocument*)ctx;
	if (!document) {
		return;
	}
	if (document->m_listHead) {
		document = *document->m_listHead;
	}
	CKLBNodeVirtualDocument* first = document;
	do {
		document->m_drawarea[0].surf_handle = NULL_IDX;
		document->m_drawarea[1].surf_handle = NULL_IDX;
		if (document->m_drawarea[0].softwareBufTile) {
			document->m_drawarea[0].softwareBufTile = NULL;
		}
		if (document->m_drawarea[1].softwareBufTile) {
			document->m_drawarea[1].softwareBufTile = NULL;
		}
		document->clearRenderResources();
		document = document->m_listNext;
	} while (document != first);
}

bool CKLBNodeVirtualDocument::setViewPortSize(u32 width, u32 height, float alignOffsetX, float alignOffsetY, u32 priority, bool doScroll) {
	/* TODO : This optimization was buggy, rolled back to old code, but should be looked at.
	if (!((width > m_viewPortWidth) || (height > m_viewPortHeight))) {
		return true;
	}*/

	bool success = true;
#ifdef DEBUG_TEXTURE_PACKER
	setIgnoreVirtualDoc(this);
#endif

	m_viewPortHeight	= height;
	m_viewPortWidth		= width;
	if ((width == 0) || (height == 0)) {
		//m_bDisplay = true;
		return false;
	}

	m_alignOffsetX = alignOffsetX;
	m_alignOffsetY = alignOffsetY;

	//
	// Allocate Dynamic sprite.
	//
	CKLBRenderingManager& pRdrMgr = CKLBRenderingManager::getInstance();
	CKLBDynSprite* pSprA = m_drawarea[0].tile;
	CKLBDynSprite* pSprB = m_drawarea[1].tile;
	if (!pSprA) {
		pSprA = pRdrMgr.allocateCommandDynSprite(4, 6);
	}
	if (!pSprB) {
		pSprB = pRdrMgr.allocateCommandDynSprite(4, 6);
	}

	if (pSprA && pSprB) {
		m_drawarea[0].tile = pSprA;
		m_drawarea[1].tile = pSprB;
		u16* indices = pSprA->getSrcIndexBuffer();
		indices[0] = 0;
		indices[1] = 1;
		indices[2] = 3;
		indices[3] = 1;
		indices[4] = 2;
		indices[5] = 3;
		indices = pSprB->getSrcIndexBuffer();
		indices[0] = 0;
		indices[1] = 1;
		indices[2] = 3;
		indices[3] = 1;
		indices[4] = 2;
		indices[5] = 3;
		if (this->setRenderSlotCount(2)) {
			this->setRender(pSprA,0);
			pSprA->changeOrder(pRdrMgr, priority);
			if (doScroll) {
				this->setRender(pSprB,1);
				pSprB->changeOrder(pRdrMgr, priority);
			} else {
				this->setRender(NULL, 1);
			}
		} else {
			success = false;
		}
	} else {
		success = false;
		if (pSprA) {
			pRdrMgr.releaseCommand(pSprA);
			m_drawarea[0].tile = NULL;
		}
		if (pSprB) {
			pRdrMgr.releaseCommand(pSprB);
			m_drawarea[1].tile = NULL;
		}
	}

	TexturePacker& mgrPacker = texturePacker();
	mgrPacker.setFormat(m_format);
	CKLBNodeVirtualDocument* surfaceOwner = NULL;
	if (m_listHead) {
		CKLBNodeVirtualDocument* listOwner = *m_listHead;
		if (listOwner != this) {
			surfaceOwner = listOwner;
		}
	}

	if (m_drawarea[0].surf_handle == NULL_IDX) {
		if (surfaceOwner) {
			m_drawarea[0].surf_handle = surfaceOwner->m_drawarea[0].surf_handle;
		} else {
			m_drawarea[0].surf_handle = mgrPacker.allocateSurface(width, height, this, docTextureCompaction, docTextureRelease);
		}
		/* Special render state associated to text, not needed now.
		if (m_format == VDFORMAT_8) {
			pSprA->setRenderState(pRdrMgr->getTextState());
		} */
	} else {
		if (surfaceOwner) {
			m_drawarea[0].surf_handle = surfaceOwner->m_drawarea[0].surf_handle;
		} else {
			m_drawarea[0].surf_handle = mgrPacker.reallocateSurface(m_drawarea[0].surf_handle, width, height, this, docTextureCompaction, docTextureRelease);
		}
	}

	if (m_drawarea[0].surf_handle != NULL_IDX) {
		// Success.
		mgrPacker.getSurfaceInfo(m_drawarea[0].surf_handle,
								 m_drawarea[0].softwareBufTile,
								 m_drawarea[0].leftU,
								 m_drawarea[0].upV,
								 m_drawarea[0].rightU,
								 m_drawarea[0].bottomV,
								 m_drawarea[0].stepU,
								 m_drawarea[0].stepV);
		m_drawarea[0].format = m_format;
	}

	m_bScroll = doScroll;

	if (doScroll) {
		if (m_drawarea[1].surf_handle == NULL_IDX) {
			if (surfaceOwner) {
				m_drawarea[1].surf_handle = surfaceOwner->m_drawarea[1].surf_handle;
			} else {
				m_drawarea[1].surf_handle = mgrPacker.allocateSurface(width, height, this, docTextureCompaction, docTextureRelease);
			}
			/* Special render state associated to text, not needed now.
			if (m_format == VDFORMAT_8) {
				 pSprB->setRenderState(pRdrMgr->getTextState());
			} */
		} else {
			if (surfaceOwner) {
				m_drawarea[1].surf_handle = surfaceOwner->m_drawarea[1].surf_handle;
			} else {
				m_drawarea[1].surf_handle = mgrPacker.reallocateSurface(m_drawarea[1].surf_handle, width, height, this, docTextureCompaction, docTextureRelease);
			}
		}
		m_drawarea[1].format = m_format;

		if (m_drawarea[1].surf_handle == NULL_IDX) {
			success = false;
		}
	} else {
		if (m_drawarea[1].surf_handle != NULL_IDX) {
			mgrPacker.releaseSurface(m_drawarea[1].surf_handle);
			m_drawarea[1].surf_handle = NULL_IDX;
		}
	}

	success = success && (m_drawarea[0].surf_handle != NULL_IDX);

	if (success) {
		if (doScroll) {
			mgrPacker.getSurfaceInfo(m_drawarea[1].surf_handle,
									 m_drawarea[1].softwareBufTile,
									 m_drawarea[1].leftU,
									 m_drawarea[1].upV,
									 m_drawarea[1].rightU,
									 m_drawarea[1].bottomV,
									 m_drawarea[1].stepU,
									 m_drawarea[1].stepV);

		} else {
			m_drawarea[1].softwareBufTile = m_drawarea[0].softwareBufTile;
			m_drawarea[1].stepU			= m_drawarea[0].stepU;
			m_drawarea[1].stepV			= m_drawarea[0].stepV;
			m_drawarea[1].leftU			= m_drawarea[0].leftU;
			m_drawarea[1].upV			= m_drawarea[0].upV;
			m_drawarea[1].rightU		= m_drawarea[0].rightU;
			m_drawarea[1].bottomV		= m_drawarea[0].bottomV;
		}

		//
		// For now we allocate a seperate texture
		// But we could later on write a texture chunk allocator.
		//
		/*
		u32 realTexW = nearest2Pow(texW);
		u32 realTexH = nearest2Pow(texH);
		
		stepUPix = 1.0f / realTexW;
		stepVPix = 1.0f / realTexH;

		*/

		// this->m_textureX[0] = 0;
		// this->m_textureY[0] = 0;

		
		pSprA->m_pTexture = mgrPacker.getTextureUsage(m_drawarea[0].surf_handle);
		if (doScroll) {
			pSprB->m_pTexture = mgrPacker.getTextureUsage(m_drawarea[1].surf_handle);
		}
	}

#ifdef DEBUG_TEXTURE_PACKER
	// Failed : free everything.
	setIgnoreVirtualDoc(NULL);
#endif

	if (!success) {
		clearRessources();
		clearRenderResources();
	}

	this->renderContext.stride			= m_viewPortWidth; // in pixel
	this->renderContext.targetWidth		= m_viewPortWidth;
	this->renderContext.targetHeight	= m_viewPortHeight;

	m_bHasChanged = true;

	return success;
}

void CKLBNodeVirtualDocument::setViewPortPos(s32 x, s32 y) {
	if ((m_viewPortWidth == 0) || (m_viewPortHeight == 0)) {
		return;
	}

	if (x < 0) {
		x = 0;
	}

	if (y < 0) {
		y = 0;
	}

	// Non scrollable type -> Force no scroll.
	if (!m_bScroll) {
		x = 0;
		y = 0;
	}

	const bool scrolling = (x || y) && m_bScroll;

	if (m_bDisplay) {
		if (!m_isVertical) {
			m_currTile	= x / m_viewPortWidth;
			m_scroll	= (x % m_viewPortWidth);
		} else {
			m_currTile	= y / m_viewPortHeight;
			m_scroll	= (y % m_viewPortHeight);
		}
		int currIdx = m_currTile & 1;
		m_currBuff = currIdx;

		//printf("tile:%i pos:%i\n", m_currTile, m_scroll);

		m_posX = x;
		m_posY = y;

		if ((m_currTile != m_prevTile) || m_bHasChanged /* || (m_prevScroll != m_scroll) || (!m_bScroll) */) {
			if(m_bHasChanged) {
				m_bHasChanged = false;
			}

			if (scrolling) {

				if (m_currTile == m_prevTile + 1) {
					// Copy Next Tile to Curr Tile
					if (!m_isVertical) {
						renderDocument(1-currIdx, (m_currTile + 1) * m_viewPortWidth, 0);
					} else {
						renderDocument(1-currIdx, 0, (m_currTile + 1) * m_viewPortHeight);
					}
				} else
				if (m_currTile == m_prevTile - 1) {
					// Copy Curr Tile to Next Tile
					if (!m_isVertical) {
						renderDocument(currIdx, m_currTile * m_viewPortWidth, 0);
					} else {
						renderDocument(currIdx, 0, m_currTile * m_viewPortHeight);
					}
				} else /*if (m_currTile != m_prevTile)*/	// Complete refresh both.
				{
					// Redraw Both tile.
					if (!m_isVertical) {
						renderDocument(currIdx,  m_currTile      * m_viewPortWidth, 0);
						renderDocument(1 - currIdx, (m_currTile + 1) * m_viewPortWidth, 0);
					} else {
						renderDocument(currIdx, 0, m_currTile       * m_viewPortHeight);
						renderDocument(1 - currIdx, 0, (m_currTile + 1) * m_viewPortHeight);
					}
				}
			} else {
				renderDocument(currIdx, 0, m_currTile * m_viewPortHeight);
			}
		}
		// Refresh only the XY and UV. Texture may NOT be updated.
		updateDynSprites(currIdx);
		updateDynSprites(1 - currIdx);

		m_prevTile		= m_currTile;
		m_prevScroll	= m_scroll;
	} else {
		klb_assertAlways( "create document, CKLBNodeVirtualDocument::lock()/unlockDocument()");
	}
}

void CKLBNodeVirtualDocument::setDocumentSize	(u32 width, u32 height, bool scrollVertical) {
	m_documentWidth		= width;
	m_documentHeight	= height;
	m_isVertical		= scrollVertical;
	m_prevTile			= 0x7fff;
}

bool CKLBNodeVirtualDocument::createDocument	(u16 maxCommandCount, u8 format) {
	freeDocument();

	m_commandArray		= KLBNEWA(SDrawCommand,maxCommandCount);
	m_CurrentCommand	= 0;
	m_format			= format;
	if (m_commandArray) {
		m_commandMaxCount = maxCommandCount;
		return true;
	} else {
		m_commandMaxCount = 0;
		return false;
	}
}

void CKLBNodeVirtualDocument::freeDocument		() {
	if (m_commandArray) {
		for (u32 n = 0; n < m_CurrentCommand; n++) {
			if (m_commandArray[n].command == DRAWTEXT) {
				if (m_commandArray[n].ptr) {
					KLBDELETEA((char *)(m_commandArray[n].ptr));
				}
			}
		}
		KLBDELETEA(m_commandArray);
		m_commandArray = NULL;
	}
	m_CurrentCommand	= 0;
	m_bDisplay			= true;
}

void CKLBNodeVirtualDocument::freeFont() {
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	for (int n=0; n<5; n++) {
		if (font[n]) {
			platform.deleteFontSystem(font[n]);
		}
	}
}

void CKLBNodeVirtualDocument::lockDocument() {
	klb_assertNull(m_bDisplay,		"Document is already locked");
	klb_assertNull(m_commandArray,	"CKLBNodeVirtualDocument::createDocument not called.");

	// Do nothing in our implementation.
	m_bDisplay = false;
}

void CKLBNodeVirtualDocument::emptyDocument() {
/*
	2012.11.29  
	m_CurrentCommand	= 0;
*/
	if (m_commandArray) {
		for (u32 n = 0; n < m_CurrentCommand; n++) {
			if (m_commandArray[n].command == DRAWTEXT) {
				if (m_commandArray[n].ptr) {
					KLBDELETEA((char *)(m_commandArray[n].ptr));
				}
			}
		}
	}
	m_CurrentCommand = 0;
}

void CKLBNodeVirtualDocument::unlockDocument() {
	TexturePacker& mgrPacker = texturePacker();
	// --> Optimize data structure in optimized version.
	m_bDisplay      = true;
	m_bHasChanged   = true;
}

void CKLBNodeVirtualDocument::clear		(u32 fillColor) {
	m_bgColor		= convertARGB32_RGBA8(fillColor);
	m_bHasChanged   = true;
}

void CKLBNodeVirtualDocument::drawLine		(s16 x0, s16 y0, s16 x1,    s16 y1,     u32 color) {
	if (m_CurrentCommand < m_commandMaxCount) {
		SDrawCommand* drw = &m_commandArray[m_CurrentCommand++];
		drw->x0			= x0;
		drw->x1			= x1;
		drw->y0			= y0;
		drw->y1			= y1;
		drw->color		= convertARGB32_RGBA8(color);
		drw->command	= DRAWLINE;
	} else {
		klb_assertAlways("Reached max command in virtual document");
	}
}

void CKLBNodeVirtualDocument::drawRect		(s16 x0, s16 y0, u16 width, u16 height, u32 color) {
	if (m_CurrentCommand < m_commandMaxCount) {
		SDrawCommand* drw = &m_commandArray[m_CurrentCommand++];
		drw->x0			= x0;
		drw->x1			= x0 + width - 1;
		drw->y0			= y0;
		drw->y1			= y0 + height - 1;
		drw->color		= convertARGB32_RGBA8(color);
		drw->command	= DRAWRECT;
	} else {
		klb_assertAlways("Reached max command in virtual document");
	}
}

void CKLBNodeVirtualDocument::fillRect		(s16 x0, s16 y0, u16 width, u16 height, u32 color, bool fill) {
	if (m_CurrentCommand < m_commandMaxCount) {
		SDrawCommand* drw = &m_commandArray[m_CurrentCommand++];
		drw->x0			= x0;
		drw->x1			= x0 + width;
		drw->y0			= y0;
		drw->y1			= y0 + height;
		drw->color		= convertARGB32_RGBA8(color);
		if (fill) {
			drw->command = FILLRECTFORCE;	// Fill
		} else {
			drw->command = FILLRECT;			// Blend
		}
	} else {
		klb_assertAlways("Reached max command in virtual document");
	}
}

void CKLBNodeVirtualDocument::drawImage	(s16 x0, s16 y0, CKLBImageAsset* img, u8 Alpha) {
	if (img->getVertexCount() == 4) {
		if (m_CurrentCommand < m_commandMaxCount) {
			SDrawCommand* drw = &m_commandArray[m_CurrentCommand++];
			drw->x0			= x0;
			drw->y0			= y0;
			drw->ptr		= img;

			//
			// Preprocess image once to extract all information for
			// software rasterization process.
			//
			CKLBTextureAsset* pAsset = img->getTexture();
			float fx0; float fy0;
			float fx1; float fy1;
			img->getXY(0,&fx0,&fy0);
			img->getXY(2,&fx1,&fy1);
			for (int n=0; n < 4; n+=2) {
				float u; float v;
				img->getUV(n,&u,&v);			// Read UV vertex 0 and vertex 2
				s32 px = pAsset->m_width  * u;		// Get X,Y from UV
				s32 py = pAsset->m_height * v;

				if (n == 0) {
					drw->sx0		= px;
					drw->sy0		= py;
				} else {
					s32 dx = (s32)(fx1-fx0);
					s32 dy = (s32)(fy1-fy0);
					drw->x1			= x0 + dx;
					drw->y1			= y0 + dy;
					drw->sdx		= px - drw->sx0;
					drw->sdy		= py - drw->sy0;

					if ((drw->sdx == dx) || (drw->sdy == dy)) {
						// Normal
						drw->swap = false;
					} else if ((drw->sdy == dx) || (drw->sdx == dy)) {
						// Swap X,Y
						drw->swap = true;
					} else {
						// Error
						klb_assertAlways("Imcompatible Bitmap with 4 vertex");
					}
				}
			}

			drw->ptr		= img;
			drw->color		= Alpha;
			drw->command	= DRAWIMAGE; 
		} else {
			klb_assertAlways("Reached max command in virtual document");
		}
	} else {
		klb_assertAlways("Image has more than 4 vertex");
	}
}

void CKLBNodeVirtualDocument::drawTileImage(s16 x0, s16 y0, u16 width, u16 height, CKLBImageAsset* img, u8 Alpha) {
	if (m_CurrentCommand < m_commandMaxCount) {
		SDrawCommand* drw = &m_commandArray[m_CurrentCommand++];
		drw->x0			= x0;
		drw->x1			= x0 + width;		// NO -1
		drw->y0			= y0;
		drw->y1			= y0 + height;		// NO -1
		drw->ptr		= img;
		drw->color		= Alpha;
		drw->command	= DRAWIMAGETILED;
	} else {
		klb_assertAlways("Reached max command in virtual document");
	}
}

void CKLBNodeVirtualDocument::setFontScale(float scaleX, float scaleY) {
	m_fontScaleX = scaleX;
	m_fontScaleY = scaleY;
}

#define	CR	(0x0D)
#define LF	(0x0A)

//
// Inline formatting tokens embedded in the text are extracted while the string
// is split into display lines. '{b0}'..'{b9}' select a blur strength, while
// '{bX}' / '{bY}' / '{bZ}' (and their lowercase two pixel variants) select the
// outline directions used by applyTextOutline(). Each token is recorded as the
// byte offset inside its line where the following run of characters starts.
//
struct SVDocTextTag {
	u16				offset;			// Byte offset of the run inside its line.
	u16				code;			// Blur strength, or outline directions | 0x8000.
};

struct SVDocTextLine {
	char*			text;			// NULL for an empty line.
	SVDocTextTag*	tagFirst;
	SVDocTextTag*	tagLast;
};

struct SVDocTextSplit {
	char*			text;			// Writable copy of the text, cut in place.
	SVDocTextLine*	lineArray;
	SVDocTextTag*	tagArray;
	u16				lineArraySize;
	u16				tagArraySize;
	u16				lineCount;		// Result.
};

static const u32 VDOC_TEXT_LINE_MAX		= 256;
static const u32 VDOC_TEXT_TAG_MAX		= 512;
static const u32 VDOC_TEXT_PARSER_SIZE	= (VDOC_TEXT_LINE_MAX * sizeof(SVDocTextLine))
										+ (VDOC_TEXT_TAG_MAX  * sizeof(SVDocTextTag));

char*	g_vdocTextParser		= NULL;
u32		g_vdocTextParserSize	= 0;

// Scratch storage shared by every document, grown once and kept alive.
static char* VDocTextParserBuffer() {
	char* parser = g_vdocTextParser;
	if (g_vdocTextParserSize >= VDOC_TEXT_PARSER_SIZE) { return parser; }
	KLBDELETEA(parser);
	g_vdocTextParser		= KLBNEWA(char, VDOC_TEXT_PARSER_SIZE);
	g_vdocTextParserSize	= VDOC_TEXT_PARSER_SIZE;
	return g_vdocTextParser;
}

//
// Cut the text in place into lines and extract the inline formatting tokens.
// Returns false when either output array is too small for the given text.
//
bool VDocSplitTextLines(SVDocTextSplit* split) {
	char*			cursor	= split->text;
	SVDocTextLine*	line	= split->lineArray;
	SVDocTextTag*	tag		= split->tagArray;
	SVDocTextLine*	lineEnd	= split->lineArray + split->lineArraySize;
	SVDocTextTag*	tagEnd	= split->tagArray  + split->tagArraySize;

	split->lineCount = 0;

	s32		code	= 0;
	bool	hadCR	= false;

	while (*cursor) {
		if (*cursor == CR) {
			*cursor++ = 0;
			if (line == lineEnd) { return false; }
			line->text		= NULL;
			line->tagFirst	= NULL;
			line->tagLast	= NULL;
			line++;
			hadCR = true;
			continue;
		} else if (*cursor == LF) {
			*cursor++ = 0;
			if (!hadCR) {
				if (line == lineEnd) { return false; }
				line->text		= NULL;
				line->tagFirst	= NULL;
				line->tagLast	= NULL;
				line++;
			}
			hadCR = false;
			continue;
		} else if ((*cursor == '\\') && (cursor[1] == 'n')) {
			*cursor++ = 0;
			*cursor++ = 0;
			if (line == lineEnd) { return false; }
			line->text		= NULL;
			line->tagFirst	= NULL;
			line->tagLast	= NULL;
			line++;
			hadCR = false;
			continue;
		}

		if (line == lineEnd) { return false; }
		line->text		= cursor;
		line->tagFirst	= tag;

		if (tag == tagEnd) { return false; }
		tag->code	= (u8)code;
		tag->offset	= 0;
		tag++;

		char*	term			= cursor;
		bool	skipTerminator	= false;

		for (;;) {
			if (*term == 0) {
				hadCR = false;
				break;
			}
			// Every line terminator ends with the same three steps, so the
			// arms only pick the carriage-return state and the cut point.
			if (*term == LF) {
				hadCR			= false;
			} else if (*term == CR) {
				hadCR			= true;
			} else if ((term[0] == '\\') && (term[1] == 'n')) {
				*term++			= 0;
				hadCR			= false;
			} else {
				if ((term[0] == '\\') && (term[1] == '\\')) {
					memmove(term, term + 1, strlen(term));	// convert \ + \ into a single \ for display
					term++;									// skip \ char
				}
				if ((term[0] == '{') && (term[1] == 'b')) {
					char	token	= term[2];
					bool	isBlur	= ((token >= '0') && (token <= '9'));
					if ((isBlur
						 || (token == 'X') || (token == 'Y') || (token == 'Z')
						 || (token == 'x') || (token == 'y') || (token == 'z'))
						&& (term[3] == '}')) {
						if (isBlur) {
							code = ((token - '0') * 255) / 9;
						} else {
							s16 directions = ((token >= 'X') && (token <= 'Z'))
											? (s16)(token - 'W')
											: (s16)((token - 'w') * 0x11);
							code = directions | 0x8000;
						}
						const size_t tail = strlen(term) - 3;	// remove the token itself
						memmove(term, term + 4, tail);
						if (tag == tagEnd) { return false; }
						tag->code	= code;
						tag->offset	= (u16)(term - cursor);
						tag++;
						term--;
					}
				}
				term++;
				continue;
			}
			*term			= 0;
			skipTerminator	= true;
			break;
		}

		if (tag == tagEnd) { return false; }
		tag->code	= (u8)code;
		tag->offset	= (u16)(term - cursor);
		tag++;

		line->tagLast = tag;
		line++;

		cursor = skipTerminator ? (term + 1) : term;
	}

	split->lineCount = (u16)(line - split->lineArray);
	return true;
}

void CKLBNodeVirtualDocument::drawText		(s16 x0, s16 y0, const char* string, u32 color, u8 fontIndex,
								s32 letterSpacing, u8 align_mode, s16 align_width,
								u32 effectParam, s32 effectMode, s32 effectExtra,
								s32 borderSize, s32 effectPassCount
								) {
	klb_assertNull(fontIndex < 5, "Maximum 5 fonts per document");
	if (string && font[fontIndex]) {
		size_t length    = strlen(string) + 1;
		char* newString = KLBNEWA(char, length);
		memcpy(newString, string, length);

		char* parserBuffer = VDocTextParserBuffer();

		SVDocTextSplit split;
		split.text			= newString;
		split.lineArray		= (SVDocTextLine*)parserBuffer;
		split.tagArray		= (SVDocTextTag*)(parserBuffer
							+ (VDOC_TEXT_LINE_MAX * sizeof(SVDocTextLine)));
		split.lineArraySize	= VDOC_TEXT_LINE_MAX;
		split.tagArraySize	= VDOC_TEXT_TAG_MAX;

		klb_assert(VDocSplitTextLines(&split), "TEXT TOO LONG, PARSER ALLOCATION ISSUES.");

		s32 characterEndX26_6      [1000];
		s32 characterEndByteOffsets[1000];

		STextInfo textInfo;
		textInfo.characterEndX26_6			= characterEndX26_6;
		textInfo.characterEndByteOffsets	= (u32*)characterEndByteOffsets;

		IPlatformRequest& platform = CPFInterface::getInstance().platform();

		float scaleX = m_fontScaleX;
		float scaleY = m_fontScaleY;

		textInfo.characterCount = 1000;
		platform.setNativeFont(m_useNativeFont);
		CPFInterface::getInstance().platform().getTextInfo(newString, font[fontIndex],
														  &textInfo, scaleX, scaleY);

		// 1 line size.
		s32 heightF	= (s32)(textInfo.ascent - textInfo.descent);
		s32 lineY	= 0;

		u16 lineCount = split.lineCount;

		if (m_fitEnabled) {
			float fitScale = 1.0f;
			for (s32 n = 0; n < lineCount; n++) {
				textInfo.characterCount = 1000;
				platform.setNativeFont(m_useNativeFont);
				CPFInterface::getInstance().platform().getTextInfo(split.lineArray[n].text,
																  font[fontIndex], &textInfo,
																  1.0f, 1.0f);
				float lineScale = (textInfo.width < (float)align_width)
								? 1.0f
								: ((textInfo.width == 0.0f)
									? 1.0f
									: ((float)align_width / textInfo.width));
				if (lineScale < fitScale) { fitScale = lineScale; }
			}
			if (lineCount) {
				textInfo.characterCount = 1000;
				platform.setNativeFont(m_useNativeFont);
				CPFInterface::getInstance().platform().getTextInfo(newString, font[fontIndex],
																  &textInfo, fitScale, fitScale);
				s32 fitHeight = (s32)(textInfo.ascent - textInfo.descent);
				lineY = -((heightF - fitHeight) >> 1);
				if (m_fitModeExtended) { heightF = fitHeight; }
			}
			scaleX = fitScale;
			scaleY = fitScale;
		}

		// Vertical step between two lines, enlarged by the border and shadow spread.
		s32 borderPixels = (borderSize + 63) >> 6;
		s32 spreadAbove  = effectExtra - borderPixels;
		s32 spreadBelow  = heightF + effectExtra + borderPixels;
		if (spreadBelow < heightF) { spreadBelow = heightF; }
		if (spreadAbove > 0)       { spreadAbove = 0; }
		s32 lineStep = letterSpacing - spreadAbove + spreadBelow;

		for (s32 n = 0; n < lineCount; n++) {
			char* lineText = split.lineArray[n].text;
			if (lineText) {
				if (m_CurrentCommand >= m_commandMaxCount) { break; }

				klb_assertNull(strlen(lineText) < 1000, "Too long string chunk.");

				textInfo.characterCount = 1000;
				platform.setNativeFont(m_useNativeFont);
				CPFInterface::getInstance().platform().getTextInfo(lineText, font[fontIndex],
																  &textInfo, scaleX, scaleY);

				SDrawCommand* drw = &m_commandArray[m_CurrentCommand++];
				switch(align_mode) {
				default:	drw->x0 = x0;	break;
				case 1:		drw->x0 = x0 + (align_width - (s32)textInfo.width) / 2;	break;
				case 2:		drw->x0 = x0 + align_width - (s32)textInfo.width;		break;
				}
				s32 ascent		= (s32)textInfo.ascent;
				drw->y0			= y0 - ascent + lineY;
				drw->x1			= drw->x0 + (s32)textInfo.width;
				m_scrollPosition = textInfo.width;
				drw->y1			= y0 - (s32)textInfo.descent + lineY;
				drw->ptr		= (n == 0) ? (void*)newString : NULL;
				drw->txt		= lineText;

				drw->color		= color;
				drw->effectParam = effectParam;
				drw->effectMode  = effectMode;
				drw->effectExtra = effectExtra;
				drw->borderSize  = borderSize;
				drw->effectPassCount = effectPassCount;
				drw->command	= DRAWTEXT;
				drw->fntIdx		= fontIndex;
				drw->ascent		= ascent;
				drw->scaleX		= scaleX;
				drw->scaleY		= scaleY;

				//
				// Replay the inline formatting runs as separate commands covering
				// the pixels the tagged characters occupy.
				//
				s32 tagCount = (s32)(split.lineArray[n].tagLast - split.lineArray[n].tagFirst);
				if (tagCount >= 2) {
					u32 byteCursor	= 0;
					s32 glyph		= 0;
					for (s32 t = 0; t < (tagCount - 1); t++) {
						if (split.lineArray[n].tagFirst[t].code
							&& (m_CurrentCommand < m_commandMaxCount)) {
							u32 runStart = split.lineArray[n].tagFirst[t    ].offset;
							u32 runEnd   = split.lineArray[n].tagFirst[t + 1].offset;
							if (runStart != runEnd) {
								SDrawCommand* eff = &m_commandArray[m_CurrentCommand++];
								while (byteCursor != runStart) {
									byteCursor = textInfo.characterEndByteOffsets[glyph] + 1;
									glyph++;
								}
								u32 startX = 0;
								if (glyph > 0) { startX = textInfo.characterEndX26_6[glyph - 1]; }
								while ((byteCursor != runEnd)
									   && (glyph < (s32)textInfo.characterCount)) {
									byteCursor = textInfo.characterEndByteOffsets[glyph] + 1;
									glyph++;
								}
								u32 endX = textInfo.characterEndX26_6[glyph - 1];

								eff->x0 = (startX >> 6) + drw->x0;
								eff->x1 = (endX   >> 6) + drw->x0;
								if (lineCount == 1) {
									eff->y0 = 0;
									eff->y1 = m_viewPortHeight;
								} else {
									eff->y0 = drw->y0 - 1;
									eff->y1 = drw->y1 + 1;
								}
								eff->borderSize	= split.lineArray[n].tagFirst[t].code;
								eff->command	= DRAWTEXTEFFECT;
							}
						}
					}
				}
			}
			lineY += lineStep;
		}

		// No draw instruction was generated.
		if (lineCount == 0) {
			KLBDELETEA(newString);
		}
	}
}

void CKLBNodeVirtualDocument::setFont(u8 index, const char* fontName, u16 fontSize) {
	klb_assertNull(index < 5, "Maximum 5 fonts per document");
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	if(font[index]) {
		platform.setNativeFont(m_useNativeFont);
		platform.deleteFontResource(font[index]);
		font[index] = NULL;
	}
	platform.setNativeFont(m_useNativeFont);
	font[index] = platform.getFont(fontSize, fontName, m_nativeFontSize);
	m_bHasChanged = true;
}

void CKLBNodeVirtualDocument::setVertex(CKLBDynSprite* pSpr, u32 idx4, float x0, float y0, float u, float v) {
	x0 += m_alignOffsetX;
	y0 += m_alignOffsetY;
	pSpr->setVertexXY(idx4, x0, y0);
	pSpr->setVertexUV(idx4, u, v);
}

void CKLBNodeVirtualDocument::dumpVirtualDocumentNode(CKLBNodeVirtualDocument* document, FILE* stream) {
	VDOCDRAW& area0 = document->m_drawarea[0];
	VDOCDRAW& area1 = document->m_drawarea[1];
	CKLBDynSprite* sprite0 = area0.tile;
	CKLBDynSprite* sprite1 = area1.tile != sprite0 ? area1.tile : NULL;
	float uv00 = -10000.0f;
	float uv01 = -10000.0f;
	float uv08 = -10000.0f;
	float uv09 = -10000.0f;
	float uv10 = -10000.0f;
	float uv11 = -10000.0f;
	float uv18 = -10000.0f;
	float uv19 = -10000.0f;
	if (sprite0) {
		float* vertices = sprite0->getSrcUVBuffer();
		uv00 = vertices[0];
		uv01 = vertices[1];
		uv08 = vertices[8];
		uv09 = vertices[9];
	}
	if (sprite1) {
		float* vertices = sprite1->getSrcUVBuffer();
		uv10 = vertices[0];
		uv11 = vertices[1];
		uv18 = vertices[8];
		uv19 = vertices[9];
	}

	fprintf(stream,
			"VDOC(%p) UV-Area0-%i [%f,%f,%f,%f] UV-Area1-%i [%f,%f,%f,%f] "
			"Sprite0(%p) Sprite1(%p) UVSpr0:%f,%f,%f,%f UVSpr1:%f,%f,%f,%f\n",
			document,
			area0.surf_handle, area0.leftU, area0.upV, area0.rightU, area0.bottomV,
			area1.surf_handle, area1.leftU, area1.upV, area1.rightU, area1.bottomV,
			sprite0, sprite1,
			uv00, uv01, uv08, uv09, uv10, uv11, uv18, uv19);
}

void CKLBNodeVirtualDocument::dumpVirtualDocumentTree(CKLBNode* node, FILE* stream, u32 depth) {
	CKLBNode* child = node->getChild();
	if (node->getClassID() == CLS_KLBNODEVIRTUALDOC) {
		for (u32 n = 0; n < depth; n++) {
			fwrite("  ", 2, 1, stream);
		}
		dumpVirtualDocumentNode((CKLBNodeVirtualDocument*)node, stream);
	}
	if (child) {
		depth++;
		do {
			dumpVirtualDocumentTree(child, stream, depth);
			child = child->getBrother();
		} while (child);
	}
}

void CKLBNodeVirtualDocument::forceRefresh() {
	if (m_drawarea[0].surf_handle != NULL_IDX) {
		// Success.
		mgrPacker.getSurfaceInfo(m_drawarea[0].surf_handle,
								 m_drawarea[0].softwareBufTile,
								 m_drawarea[0].leftU,
								 m_drawarea[0].upV,
								 m_drawarea[0].rightU,
								 m_drawarea[0].bottomV,
								 m_drawarea[0].stepU,
								 m_drawarea[0].stepV);
		updateDynSprites(0);
		mgrPacker.updateTexture(m_drawarea[0].surf_handle);
	}
	
	if (m_drawarea[1].surf_handle != NULL_IDX) {
		mgrPacker.getSurfaceInfo(m_drawarea[1].surf_handle,
									m_drawarea[1].softwareBufTile,
									m_drawarea[1].leftU,
									m_drawarea[1].upV,
									m_drawarea[1].rightU,
									m_drawarea[1].bottomV,
									m_drawarea[1].stepU,
									m_drawarea[1].stepV);

		updateDynSprites(1);
		mgrPacker.updateTexture(m_drawarea[1].surf_handle);
	}
	
	markUpMatrixAndColor();
}

void CKLBNodeVirtualDocument::setFitMode(u8 fitMode) {
	m_fitEnabled = fitMode != 0;
	m_fitModeExtended = fitMode > 1;
}

void CKLBNodeVirtualDocument::setMarquee(s16 startDelay, s16 endDelay, u8 mode, float speed) {
	m_marqueeA = startDelay;
	m_marqueeB = endDelay;
	m_marqueeC = mode;
	m_scrollStep = speed;
	m_scrollOffset = 0.0f;
}

void CKLBNodeVirtualDocument::setMarqueeActive(bool active, s32 width, bool loop) {
	m_marqueeActive = active;
	m_marqueeState = active ? 0 : -1;
	m_marqueeFlag = loop;
	m_marqueeV = width;
	m_marqueeStopped = !active;
}

void CKLBNodeVirtualDocument::setMarqueePos(s16 position) {
	m_marqueePos = position;
}

float CKLBNodeVirtualDocument::getMarqueeWidth() {
	return m_scrollPosition;
}

bool CKLBNodeVirtualDocument::isMarqueeStopped() {
	return m_marqueeStopped;
}

void CKLBNodeVirtualDocument::updateMarquee(u32 deltaT) {
	if (m_marqueeState >= 0) m_bHasChanged = true;
	switch (m_marqueeState) {
	case 0:
		m_scrollOffset = 0.0f;
		m_marqueeTimeAccum += deltaT;
		if (m_marqueeTimeAccum >= (s32)m_marqueeA) {
			if (m_scrollStep != 0.0f) m_marqueeState = 1;
			else m_marqueeState = 2;
		}
		break;
	case 1:
		if (m_scrollOffset >= (float)((s32)m_marqueeV + (s32)m_marqueePos)) {
			m_marqueeTimeAccum += deltaT;
			switch (m_marqueeC) {
			case 1:
				m_scrollOffset = 0.0f;
				m_marqueeState = 2;
				break;
			case 2:
				if (m_marqueeTimeAccum >= (s32)m_marqueeB) {
					if (m_marqueeFlag) {
						m_scrollOffset = 0.0f;
						m_marqueeState = 0;
						m_marqueeTimeAccum = 0;
						if ((s32)m_marqueeA <= 0) {
							if (m_scrollStep != 0.0f) m_marqueeState = 1;
							else m_marqueeState = 2;
						}
					} else {
						m_scrollOffset = (float)(-(s32)m_marqueePos);
					}
				}
				break;
			case 3:
			case 4:
				if (m_marqueeTimeAccum >= (s32)m_marqueeB) {
					m_marqueeState = 3;
					m_marqueeTimeAccum = 0;
				}
				break;
			}
		} else {
			m_marqueeTimeAccum = 0;
			m_scrollOffset += (float)deltaT * m_scrollStep;
		}
		break;
	case 2:
		m_marqueeStopped = true;
		m_marqueeState = -1;
		break;
	case 3:
		const bool atOrigin = (m_scrollOffset <= 0.0f);
		m_marqueeTimeAccum = 0;
		if (atOrigin) {
			switch (m_marqueeC) {
			case 3:
				m_scrollOffset = 0.0f;
				m_marqueeState = 2;
				break;
			case 4:
				m_scrollOffset = 0.0f;
				m_marqueeState = 0;
				if ((s32)m_marqueeA <= 0) {
					if (m_scrollStep != 0.0f) m_marqueeState = 1;
					else m_marqueeState = 2;
				}
				break;
			}
		} else {
			m_scrollOffset -= (float)deltaT * m_scrollStep;
		}
		break;
	}
	if (m_bHasChanged) setViewPortPos(0, 0);
}

void CKLBNodeVirtualDocument::setUseNativeFont(bool useNative, s32 size) {
	if (useNative != m_useNativeFont || size != m_nativeFontSize) {
		clearFontResources();
		renderContext.useNativeFont = useNative;
		m_useNativeFont = useNative;
		m_nativeFontSize = size;
	}
}

void CKLBNodeVirtualDocument::renderDocument(u8 index, s32 offsetX, s32 offsetY) {
	// klb_assert((_CrtCheckMemory() != 0), "Heap Error !");

	if (!m_bDisplay)	{ return; }

	u32* softwareBuffer = m_drawarea[index].softwareBufTile;
	if (!softwareBuffer)						{ return; }
	const s32 viewportHeight = m_viewPortHeight;
	const s32 viewportWidth = m_viewPortWidth;
	if (m_listHead && (*m_listHead != this))	{ return; }
	const s32 tileXEnd = offsetX + viewportWidth;
	const s32 tileYEnd = offsetY + viewportHeight;

	RenderContext* pCtx = &renderContext;
	// pCtx->pBuffer		= m_softwareBufferTile[m_currBuff];
	pCtx->pBuffer		= softwareBuffer;
	pCtx->offsetX		= offsetX;
	pCtx->offsetY		= offsetY;
	pCtx->format		= m_drawarea[index].format;

	// Clipping absolute buffer coordinate.
	pCtx->setClip	(0, 0, viewportWidth, viewportHeight);

	// Force tile fill at current coordinate. (relative coordinate fill, different from absolute instruction)
	pCtx->fillRect	(	offsetX,
						offsetY,
						tileXEnd,
						tileYEnd, m_bgColor, true);

	// Render at absolute coordinate.
	for (u32 n = 0; n < m_CurrentCommand; n++) {
		SDrawCommand* pDCom = &m_commandArray[n];
		s16 x0, x1, y0, y1;
		if(pDCom->x0 <= pDCom->x1) { x0 = pDCom->x0; x1 = pDCom->x1; } else { x0 = pDCom->x1; x1 = pDCom->x0; }
		if(pDCom->y0 <= pDCom->y1) { y0 = pDCom->y0; y1 = pDCom->y1; } else { y0 = pDCom->y1; y1 = pDCom->y0; }

		// Marquee scrolling only shifts the text run horizontally.
		x0 = (s16)(s32)((float)x0 - m_scrollOffset);
		x1 = (s16)(s32)((float)x1 - m_scrollOffset);

		// x0 <= x1, y0 < y1
		const ECOMMAND command = m_commandArray[n].command;
		if (((x1 <= offsetX) | (x0 >= tileXEnd) | (y1 <= offsetY) | (y0 >= tileYEnd))
		 && ((command == DRAWTEXT) || (command == DRAWTEXTEFFECT)))
		{
			// skip instruction.
			continue;
		}
		switch (command) {
		default:
			klb_assertAlways("Unknow draw command.");
			break;
		case DRAWLINE:	pCtx->drawLine	(pDCom->x0,pDCom->y0, pDCom->x1, pDCom->y1, pDCom->color);	break;
		case DRAWRECT:	pCtx->drawRect	(pDCom->x0,pDCom->y0, pDCom->x1, pDCom->y1, pDCom->color);	break;
		case FILLRECT:	pCtx->fillRect	(pDCom->x0,pDCom->y0, pDCom->x1, pDCom->y1, pDCom->color, false); break;
		case FILLRECTFORCE
					:	pCtx->fillRect	(pDCom->x0,pDCom->y0, pDCom->x1, pDCom->y1, pDCom->color, true); break;
		case DRAWIMAGE: pCtx->drawImage	(pDCom->x0,pDCom->y0, pDCom, pDCom->color);	break;
		case DRAWTEXT:	{
				if ((pDCom->effectMode != 0) || (pDCom->borderSize != 0)) {
					// Shadow / blurred glyph : rasterized through its own
					// alpha planes then composited into the tile.
					VDocRasterizeGlyphShadow(pDCom->scaleX, pDCom->scaleY, pCtx,
								 x0, pDCom->y0 + pDCom->ascent,
								 x1 - x0, pDCom->y1 - pDCom->y0, pDCom->ascent,
								 (const char*)pDCom->txt, pDCom->color, pDCom->effectParam,
								 font[pDCom->fntIdx], pDCom->effectMode, pDCom->effectExtra,
								 pDCom->borderSize / pDCom->effectPassCount, pDCom->effectPassCount);
				} else {
					s32 ascent = (s32)pDCom->ascent;
					pCtx->drawText	(x0, pDCom->y0 + ascent, (char*)pDCom->txt,
								 pDCom->color, font[pDCom->fntIdx], pDCom->scaleX, pDCom->scaleY);
				}
			}
			break;
		case DRAWIMAGETILED:
			{
				// Set clip.
				pCtx->setClip(pDCom->x0, pDCom->y0, pDCom->x1, pDCom->y1);
				
				// Draw Images
				for (s32 y = 0; y < pDCom->y1; y += (pDCom->y1 - pDCom->y0)) {
					for (s32 x = 0; x < pDCom->x1; x += (pDCom->x1 - pDCom->x0)) {
						pCtx->drawImage	(x, y, pDCom, pDCom->color);
					}
				}

				// Restore clip
				pCtx->setClip(0,0,this->m_viewPortWidth,this->m_viewPortHeight);
			}
			break;
		case DRAWTEXTEFFECT:
			{
				// Post process the pixels the matching text run has just
				// written : borderSize carries the inline {b..} effect code.
				// Clamp the run box to the render target first.
				if (x0 < 0)	{ x0 = 0; }
				if (y0 < 0)	{ y0 = 0; }
				if (x0 > pCtx->targetWidth)		{ x0 = (s16)pCtx->targetWidth;	}
				if (y0 > pCtx->targetHeight)	{ y0 = (s16)pCtx->targetHeight;	}
				if (x1 < 0)	{ x1 = 0; }
				if (y1 < 0)	{ y1 = 0; }
				if (x1 > pCtx->targetWidth)		{ x1 = (s16)pCtx->targetWidth;	}
				if (y1 > pCtx->targetHeight)	{ y1 = (s16)pCtx->targetHeight;	}

				s32 width = x1 - x0;
				if (width < 3)		{ break; }
				s32 height = y1 - y0;
				if (height <= 0)	{ break; }

				u32* pixels = pCtx->pBuffer + (y0 * pCtx->stride + x0);
				if (pDCom->borderSize >= 0) {
					VDocApplyTextOutlineAlternate(n, pDCom->borderSize, pixels,
												  width, height, pCtx->stride * 4);
				} else {
					applyTextOutline(n, (u8)pDCom->borderSize, pixels,
									 width, height, pCtx->stride * 4);
				}
			}
			break;
		}
	}

	texturePacker().updateTexture(m_drawarea[index].surf_handle);
}

void CKLBNodeVirtualDocument::updateDynSprites(u8 currBuff) {
	VDOCDRAW * pDraw = &m_drawarea[currBuff];
	CKLBDynSprite* pSpr = pDraw->tile;

	if (pSpr) {
		if (m_currBuff == currBuff) {
			//
			// Up clipping.
			//

			//
			// 4 Vertex numbering
			//
			// 0--1 /////////////////////
			// |  | <----- Top is clipped
			// 3--2
			//
			if (m_isVertical) {
				s32		remainV		= m_viewPortHeight - m_scroll;
				float	remainVF	= (float)remainV;
				float	vpWidthF	= (float)m_viewPortWidth;
				//
				// Dyn Sprite has 6 vertex. which are equiv to :
				//
				// 0---1/3 /////////////////////
				// |    | <----- Top is clipped
				//5/2---4

				float clipV = pDraw->upV + (m_scroll * pDraw->stepV);
				setVertex(pSpr,0, 0,		0,			pDraw->leftU,		clipV);
				setVertex(pSpr,1, vpWidthF,	0,			pDraw->rightU,		clipV);
				setVertex(pSpr,2, vpWidthF,	remainVF,	pDraw->rightU,		pDraw->bottomV);
				setVertex(pSpr,3, 0,		remainVF,	pDraw->leftU,		pDraw->bottomV);
			} else {
				// 0----1.3
				// |//|  |
				// |//|  |
				//5.2----4
				//   /|
				//   /|
				s32		remainH		= m_viewPortWidth - m_scroll;
				float	remainHF	= (float)remainH;
				float	vpHeightF	= (float)m_viewPortHeight;

				float clipU = pDraw->leftU + (m_scroll * pDraw->stepU);

				setVertex(pSpr,0, 0,		0,			clipU,					pDraw->upV);
				setVertex(pSpr,1, remainHF,	0,			pDraw->rightU,			pDraw->upV);
				setVertex(pSpr,2, remainHF,	vpHeightF,	pDraw->rightU,			pDraw->bottomV);
				setVertex(pSpr,3, 0,		vpHeightF,	clipU,					pDraw->bottomV);
			}
		} else {
			float	vpWidthF	= (float)m_viewPortWidth;
			float	vpHeightF	= (float)m_viewPortHeight;
			//
			// 0--1
			// |  | <----- Top is clipped
			// 3--2 /////////////////////
			//
			if (m_isVertical) {
				float	vOffsetF	= (float)(m_viewPortHeight - m_scroll);
				s32		remainV		= m_scroll;
				float	remainVF	= (float)remainV;

				float clipV = pDraw->upV + (remainVF * pDraw->stepV);

				setVertex(pSpr,0, 0,		vOffsetF,			  pDraw->leftU,		pDraw->upV);
				setVertex(pSpr,1, vpWidthF,	vOffsetF,			  pDraw->rightU,	pDraw->upV);
				setVertex(pSpr,2, vpWidthF,	vOffsetF + remainVF,  pDraw->rightU,	clipV);
				setVertex(pSpr,3, 0,		vOffsetF + remainVF,  pDraw->leftU,		clipV);
			} else {
				float	hOffsetF	= (float)(m_viewPortWidth - m_scroll);
				s32		remainH		= m_scroll;
				float	remainHF	= (float)remainH;

				float clipU = pDraw->leftU + (remainH * pDraw->stepU);

				setVertex(pSpr, 0, hOffsetF,			0,			pDraw->leftU,	pDraw->upV);
				setVertex(pSpr, 1, hOffsetF + remainHF,	0,			clipU,			pDraw->upV);
				setVertex(pSpr, 2, hOffsetF + remainHF,	vpHeightF,	clipU,			pDraw->bottomV);
				setVertex(pSpr, 3, hOffsetF,			vpHeightF,	pDraw->leftU,	pDraw->bottomV);
			}
		}

		pSpr->mark(CKLBDynSprite::MARK_CHANGE_UV | CKLBDynSprite::MARK_CHANGE_XY | FLAG_BUFFERSHIFT);
		markUpMatrixAndColor();
	}
}

void CKLBNodeVirtualDocument::setPriority(u32 renderPriority)
{
	CKLBRenderingManager& pRdrMgr = CKLBRenderingManager::getInstance();
	if (m_drawarea[0].tile) {
		m_drawarea[0].tile->changeOrder(pRdrMgr, renderPriority);
	}
	if (m_drawarea[1].tile) {
		m_drawarea[1].tile->changeOrder(pRdrMgr, renderPriority);
	}
}
