#include "api/yajl_parse.h"
#include "yajl_parser.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define BJSON_NO_ERROR		(yajl_status_ok)
#define BJSON_CANCEL		(yajl_status_client_canceled)
#define BJSON_ERROR			(yajl_status_error)

#define CHCK_PARSER(x)		if (!(x)) { \
								error = BJSON_CANCEL; parse = endStream; \
							}

#define CHCK_PARSER_LOOP(x)	if (!(x)) { \
								error = BJSON_CANCEL; parse = endStream; break; \
							}

static uint16_t read_u16_be(const unsigned char* input) {
	return ((uint16_t)input[0] << 8) | (uint16_t)input[1];
}

static uint32_t read_u32_be(const unsigned char* input) {
	return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16)
		| ((uint32_t)input[2] << 8) | (uint32_t)input[3];
}

static uint64_t read_u64_be(const unsigned char* input) {
	return ((uint64_t)read_u32_be(input) << 32) | read_u32_be(input + 4);
}

static int16_t read_i16_be(const unsigned char* input) {
	const uint16_t bits = read_u16_be(input);
	int16_t value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int32_t read_i32_be(const unsigned char* input) {
	const uint32_t bits = read_u32_be(input);
	int32_t value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int64_t read_i64_be(const unsigned char* input) {
	const uint64_t bits = read_u64_be(input);
	int64_t value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static float read_float_be(const unsigned char* input) {
	const uint32_t bits = read_u32_be(input);
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static double read_double_be(const unsigned char* input) {
	const uint64_t bits = read_u64_be(input);
	double value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}


int bjson_getCPCacheID(		yajl_handle		hand,
							int				cte_pool) {
	bjson_handle* bjsn	= &hand->bj;
	if ((cte_pool < bjsn->cp_count) && (cte_pool >= 0)) {
		return bjsn->cp[cte_pool].memberCache;
	} else {
		return -1;
	}
}

void bjson_setCPCacheID(	yajl_handle		hand,
							int				cte_pool,
							int				cacheID) {
	bjson_handle* bjsn	= &hand->bj;
	if ((cte_pool < bjsn->cp_count) && (cte_pool >= 0)) {
		bjsn->cp[cte_pool].memberCache = cacheID;
	} else {
		return;
	}
}

int bjson_parse(	yajl_handle		hand,
					const unsigned char*	jsonText,
					int						jsonTextLength
				)
{
	bjson_handle*	bjsn= &hand->bj;

	int error			= BJSON_NO_ERROR;
	const yajl_callbacks * callbacks = hand->callbacks;
	cp_entry*	cp		= 0;
	void*		ctx		= bjsn->ctx;

	const unsigned char* endStream;
	const unsigned char* parse;
	size_t offset = 0;
	size_t inputLength;
	uint32_t count;
	uint32_t n;

	if (!jsonText || jsonTextLength < 10) return BJSON_ERROR;
	inputLength = (size_t)jsonTextLength;
	endStream = jsonText + inputLength;
	if (jsonText[0] != 0xFF || jsonText[1] != 0xFF) return BJSON_ERROR;
	offset = 2;

	count = read_u32_be(jsonText + offset);
	offset += 4;
	/* The total constant-pool string size is informational. Individual entry
	 * lengths below are authoritative. */
	(void)read_u32_be(jsonText + offset);
	offset += 4;

	bjsn->cp_count = 0;
	bjsn->cp = NULL;
	bjsn->strs = NULL;
	if (count > (uint32_t)(inputLength / 4)
	 || count > (uint32_t)(SIZE_MAX / sizeof(cp_entry))) {
		return BJSON_ERROR;
	}
	if (count != 0) {
		bjsn->cp = (cp_entry*)hand->alloc.malloc(
			hand->alloc.ctx, (size_t)count * sizeof(cp_entry));
		if (!bjsn->cp) return BJSON_ERROR;
		cp = bjsn->cp;
		bjsn->cp_count = (int)count;
		for (n = 0; n < count; ++n) {
			uint32_t length;
			if (offset > inputLength || inputLength - offset < 4) {
				error = BJSON_ERROR;
				goto exit;
			}
			length = read_u32_be(jsonText + offset);
			offset += 4;
			if ((size_t)length > inputLength - offset) {
				error = BJSON_ERROR;
				goto exit;
			}
			cp[n].string = jsonText + offset;
			cp[n].stringlen = (int)length;
			cp[n].memberCache = -1;
			offset += length;
		}
	}
	parse = jsonText + offset;
	
	// Parse Inner stream
	while (parse < endStream) {
		// printf("Comm %4i %i %i\n",commandID++, *parse, error);

		switch (*parse++) {
		case BJSN_END:
			parse = endStream;
			break;
		case BJSN_OPEN_OBJ:
			{
				int size = (int)read_u32_be(parse);
				parse += 4;
				CHCK_PARSER(callbacks->yajl_start_map(ctx,size));
			}
			break;
		case BJSN_OPEN_ARR:
			{
				int size = (int)read_u32_be(parse);
				parse += 8; // Skip type mask
				CHCK_PARSER(callbacks->yajl_start_array(ctx, size));
			}
			break;
		case BJSN_CLOSE_OBJ:
			CHCK_PARSER(callbacks->yajl_end_map(ctx));
			break;
		case BJSN_CLOSE_ARR:
			CHCK_PARSER(callbacks->yajl_end_array(ctx));
			break;
		case BJSN_STRING:
			{
				int idx = (int)read_u32_be(parse);
				parse += 4; 
				CHCK_PARSER(callbacks->yajl_string(ctx, cp[idx].string, cp[idx].stringlen, idx));
			}
			break;
		case BJSN_STRING_DIRECT:
			{
				int len = (int)read_u32_be(parse);
				parse += 4;
				CHCK_PARSER(callbacks->yajl_string(ctx, parse, len, -1));
				parse += len;
			}
			break;
		case BJSN_MEMBER:
			{
				int idx = (int)read_u32_be(parse);
				parse += 4;
				CHCK_PARSER(callbacks->yajl_map_key(ctx, cp[idx].string, cp[idx].stringlen, idx));
			}
			break;
		case BJSN_MEMBER_DIRECT:
			{
				int len = (int)read_u32_be(parse);
				parse += 4;
				CHCK_PARSER(callbacks->yajl_map_key(ctx, parse, len, -1));
				parse += len;
			}
			break;
		case BJSN_NUMBER_I64:
			{
				long long int l64 = (long long int)read_i64_be(parse);
				parse += 8;
				CHCK_PARSER(callbacks->yajl_integer(ctx, l64));
			}
			break;
		case BJSN_NUMBER_DBL:
			{
				double value = read_double_be(parse);
				parse += 8;
				CHCK_PARSER(callbacks->yajl_double(ctx, value));
			}
			break;
		case BJSN_NUMBER_I32:
			{
				int idx = (int)read_i32_be(parse);
				parse += 4;
				CHCK_PARSER(callbacks->yajl_integer(ctx, idx));
			}
			break;
		case BJSN_NUMBER_I16:
			{
				int idx = (int)read_i16_be(parse);
				parse += 2;
				CHCK_PARSER(callbacks->yajl_integer(ctx, idx));
			}
			break;
		case BJSN_NUMBER_I8:
			{
				int c = (int)(int8_t)*parse++;
				CHCK_PARSER(callbacks->yajl_integer(ctx, c));
			}
			break;
		case BJSN_NUMBER_FLT:
			{
				float value = read_float_be(parse);
				parse += 4;
				CHCK_PARSER(callbacks->yajl_double(ctx, (double)value));
			}
			break;
		case BJSN_CTE_TRUE:
			{
				CHCK_PARSER(callbacks->yajl_boolean(ctx, 1));
			}
			break;
		case BJSN_CTE_FALSE:
			{
				CHCK_PARSER(callbacks->yajl_boolean(ctx, 0));
			}
			break;
		case BJSN_CTE_NULL:
			{
				CHCK_PARSER(callbacks->yajl_null(ctx));
			}
			break;
		case BJSN_NUMBER_I8_RLE:
			{
				int idx = (int)(int8_t)*parse++;
				int count = (int)read_u16_be(parse);
				int n;
				parse += 2;
				for (n=0; n < count; n++) {
					CHCK_PARSER_LOOP(callbacks->yajl_integer(ctx, idx));
				}
			}
			break;
		case BJSN_NUMBER_I16_RLE:
			{
				int count;
				int n;
				int idx = (int)read_i16_be(parse);
				parse += 2;
				count = (int)read_u16_be(parse);
				parse += 2;
				for (n=0; n < count; n++) {
					CHCK_PARSER_LOOP(callbacks->yajl_integer(ctx, idx));
				}
			}
			break;
		case BJSN_NUMBER_I32_RLE:
			{
				int n;
				int count;
				int idx = (int)read_i32_be(parse);
				parse += 4;
				count = (int)read_u16_be(parse);
				parse += 2;
				for (n=0; n < count; n++) {
					CHCK_PARSER_LOOP(callbacks->yajl_integer(ctx, idx));
				}
			}
			break;
		case BJSN_NUMBER_I64_RLE:
			{
				int count;
				int n;
				long long int l64 = (long long int)read_i64_be(parse);
				parse += 8;
				count = (int)read_u16_be(parse);
				parse += 2;
				for (n=0; n < count; n++) {
					CHCK_PARSER_LOOP(callbacks->yajl_integer(ctx, l64));
				}
			}
			break;
		case BJSN_CTE_TRUE_RLE:
			{
				int n;
				int count = (int)read_u16_be(parse);
				parse += 2;
				for (n=0; n < count; n++) {
					CHCK_PARSER_LOOP(callbacks->yajl_boolean(ctx, 1));
				}
			}
			break;
		case BJSN_CTE_FALSE_RLE:
			{
				int n;
				int count = (int)read_u16_be(parse);
				parse += 2;
				for (n=0; n < count; n++) {
					CHCK_PARSER_LOOP(callbacks->yajl_boolean(ctx, 0));
				}
			}
			break;
		default:
			error = BJSON_ERROR;
			goto exit;
		}
	}

exit:
	//
	// Free all allocated mem.
	//
	if (bjsn->cp_count) {
		if (bjsn->cp) {
			hand->alloc.free(hand->alloc.ctx, bjsn->cp);
			bjsn->cp = NULL;
		}

		if (bjsn->strs) {
			hand->alloc.free(hand->alloc.ctx, bjsn->strs);
			bjsn->strs = NULL;
		}
	}

	// Parse Post-CP
	return error;
}
