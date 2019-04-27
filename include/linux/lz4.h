#ifndef __LZ4_H__
#define __LZ4_H__
/*
 * LZ4 Kernel Interface
 *
 * Copyright (C) 2013, LG Electronics, Kyungsik Lee <kyungsik.lee@lge.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define LZ4_MEM_COMPRESS	(16384)
#define LZ4HC_MEM_COMPRESS	(262144 + (2 * sizeof(unsigned char *))

/*
 * lz4_compressbound()
 * Provides the maximum size that LZ4 may output in a "worst case" scenario
 * (input data not compressible)
 */
#define LZ4_MEMORY_USAGE 14

#define LZ4_MAX_INPUT_SIZE	0x7E000000 /* 2 113 929 216 bytes */
#define LZ4_COMPRESSBOUND(isize)	(\
	(unsigned int)(isize) > (unsigned int)LZ4_MAX_INPUT_SIZE \
	? 0 \
	: (isize) + ((isize)/255) + 16)

#define LZ4_ACCELERATION_DEFAULT 1
#define LZ4_HASHLOG	 (LZ4_MEMORY_USAGE-2)
#define LZ4_HASHTABLESIZE (1 << LZ4_MEMORY_USAGE)
#define LZ4_HASH_SIZE_U32 (1 << LZ4_HASHLOG)

#define LZ4HC_MIN_CLEVEL			3
#define LZ4HC_DEFAULT_CLEVEL			9
#define LZ4HC_MAX_CLEVEL			16

#define LZ4HC_DICTIONARY_LOGSIZE 16
#define LZ4HC_MAXD (1<<LZ4HC_DICTIONARY_LOGSIZE)
#define LZ4HC_MAXD_MASK (LZ4HC_MAXD - 1)
#define LZ4HC_HASH_LOG (LZ4HC_DICTIONARY_LOGSIZE - 1)
#define LZ4HC_HASHTABLESIZE (1 << LZ4HC_HASH_LOG)
#define LZ4HC_HASH_MASK (LZ4HC_HASHTABLESIZE - 1)

/*-************************************************************************
 *	STREAMING CONSTANTS AND STRUCTURES
 **************************************************************************/
#define LZ4_STREAMSIZE_U64 ((1 << (LZ4_MEMORY_USAGE - 3)) + 4)
#define LZ4_STREAMSIZE	(LZ4_STREAMSIZE_U64 * sizeof(unsigned long long))

#define LZ4_STREAMHCSIZE        262192
#define LZ4_STREAMHCSIZE_SIZET (262192 / sizeof(size_t))

#define LZ4_STREAMDECODESIZE_U64	4
#define LZ4_STREAMDECODESIZE		 (LZ4_STREAMDECODESIZE_U64 * \
	sizeof(unsigned long long))

/*
 * LZ4_stream_t - information structure to track an LZ4 stream.
 */
typedef struct {
	uint32_t hashTable[LZ4_HASH_SIZE_U32];
	uint32_t currentOffset;
	uint32_t initCheck;
	const uint8_t *dictionary;
	uint8_t *bufferStart;
	uint32_t dictSize;
} LZ4_stream_t_internal;
typedef union {
	unsigned long long table[LZ4_STREAMSIZE_U64];
	LZ4_stream_t_internal internal_donotuse;
} LZ4_stream_t;

/*
 * LZ4_streamHC_t - information structure to track an LZ4HC stream.
 */
typedef struct {
	unsigned int	 hashTable[LZ4HC_HASHTABLESIZE];
	unsigned short	 chainTable[LZ4HC_MAXD];
	/* next block to continue on current prefix */
	const unsigned char *end;
	/* All index relative to this position */
	const unsigned char *base;
	/* alternate base for extDict */
	const unsigned char *dictBase;
	/* below that point, need extDict */
	unsigned int	 dictLimit;
	/* below that point, no more dict */
	unsigned int	 lowLimit;
	/* index from which to continue dict update */
	unsigned int	 nextToUpdate;
	unsigned int	 compressionLevel;
} LZ4HC_CCtx_internal;
typedef union {
	size_t table[LZ4_STREAMHCSIZE_SIZET];
	LZ4HC_CCtx_internal internal_donotuse;
} LZ4_streamHC_t;

/*
 * LZ4_streamDecode_t - information structure to track an
 *	LZ4 stream during decompression.
 *
 * init this structure using LZ4_setStreamDecode (or memset()) before first use
 */
typedef struct {
	const uint8_t *externalDict;
	size_t extDictSize;
	const uint8_t *prefixEnd;
	size_t prefixSize;
} LZ4_streamDecode_t_internal;
typedef union {
	unsigned long long table[LZ4_STREAMDECODESIZE_U64];
	LZ4_streamDecode_t_internal internal_donotuse;
} LZ4_streamDecode_t;

/*-************************************************************************
 *	SIZE OF STATE
 **************************************************************************/
#define LZ4_MEM_COMPRESS	LZ4_STREAMSIZE
#define LZ4HC_MEM_COMPRESS	LZ4_STREAMHCSIZE

/*-************************************************************************
 *	Compression Functions
 **************************************************************************/

/**
 * LZ4_compressBound() - Max. output size in worst case szenarios
 * @isize: Size of the input data
 *
 * Return: Max. size LZ4 may output in a "worst case" szenario
 * (data not compressible)
 */
static inline int LZ4_compressBound(size_t isize)
{
	return isize + (isize / 255) + 16;
}

/*
 * lz4_compress()
 *	src     : source address of the original data
 *	src_len : size of the original data
 *	dst	: output buffer address of the compressed data
 *		This requires 'dst' of size LZ4_COMPRESSBOUND.
 *	dst_len : is the output size, which is returned after compress done
 *	workmem : address of the working memory.
 *		This requires 'workmem' of size LZ4_MEM_COMPRESS.
 *	return  : Success if return 0
 *		  Error if return (< 0)
 *	note :  Destination buffer and workmem must be already allocated with
 *		the defined size.
 */
int lz4_compress(const unsigned char *src, size_t src_len,
		unsigned char *dst, size_t *dst_len, void *wrkmem);

 /*
  * lz4hc_compress()
  *	 src	 : source address of the original data
  *	 src_len : size of the original data
  *	 dst	 : output buffer address of the compressed data
  *		This requires 'dst' of size LZ4_COMPRESSBOUND.
  *	 dst_len : is the output size, which is returned after compress done
  *	 workmem : address of the working memory.
  *		This requires 'workmem' of size LZ4HC_MEM_COMPRESS.
  *	 return  : Success if return 0
  *		   Error if return (< 0)
  *	 note :  Destination buffer and workmem must be already allocated with
  *		 the defined size.
  */
int lz4hc_compress(const unsigned char *src, size_t src_len,
		unsigned char *dst, size_t *dst_len, void *wrkmem);

/*
 * lz4_decompress()
 *	src     : source address of the compressed data
 *	src_len : is the input size, whcih is returned after decompress done
 *	dest	: output buffer address of the decompressed data
 *	actual_dest_len: is the size of uncompressed data, supposing it's known
 *	return  : Success if return 0
 *		  Error if return (< 0)
 *	note :  Destination buffer must be already allocated.
 *		slightly faster than lz4_decompress_unknownoutputsize()
 */
int lz4_decompress(const unsigned char *src, size_t *src_len,
		unsigned char *dest, size_t actual_dest_len);

/*
 * lz4_decompress_unknownoutputsize()
 *	src     : source address of the compressed data
 *	src_len : is the input size, therefore the compressed size
 *	dest	: output buffer address of the decompressed data
 *	dest_len: is the max size of the destination buffer, which is
 *			returned with actual size of decompressed data after
 *			decompress done
 *	return  : Success if return 0
 *		  Error if return (< 0)
 *	note :  Destination buffer must be already allocated.
 */
int lz4_decompress_unknownoutputsize(const unsigned char *src, size_t src_len,
		unsigned char *dest, size_t *dest_len);
#endif
