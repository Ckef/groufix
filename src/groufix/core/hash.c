/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/mem.h"
#include <assert.h>
#include <string.h>

#if defined (GFX_WIN32)
	#include <intrin.h>
#endif


// 'Randomized' hash seed (generated on the web).
#define _GFX_HASH_SEED ((uint32_t)0x4ac093e6)


// Platform agnostic rotl.
#if defined (GFX_WIN32)
	#define _GFX_ROTL32(x, r) _rotl(x, r)
#else
	#define _GFX_ROTL32(x, r) ((x << r) | (x >> (32 - r)))
#endif


/****************************/
int _gfx_hash_cmp(const void* l, const void* r)
{
	const _GFXHashKey* kL = l;
	const _GFXHashKey* kR = r;

	// Non-zero = inequal.
	return kL->len != kR->len || memcmp(kL->bytes, kR->bytes, kL->len);
}

/****************************/
uint64_t _gfx_hash_murmur3(const void* key)
{
	_Static_assert(sizeof(uint32_t) == 4, "MurmurHash3 blocks must be 4 bytes.");

	const _GFXHashKey* cKey = key;
	const size_t nblocks = cKey->len / sizeof(uint32_t);

	uint32_t h = _GFX_HASH_SEED;

	const uint32_t c1 = 0xcc9e2d51;
	const uint32_t c2 = 0x1b873593;

	// Process the body in blocks of 4 bytes.
	const uint32_t* body = (const uint32_t*)cKey->bytes + nblocks;

	for (size_t i = nblocks; i; --i)
	{
		uint32_t k = *(body - i);

		k *= c1;
		k = _GFX_ROTL32(k, 15);
		k *= c2;

		h ^= k;
		h = _GFX_ROTL32(h, 13);
		h = h*5 + 0xe6546b64;
	}

	// Process the tail bytes.
	const uint8_t* tail = (const uint8_t*)body;

	uint32_t k = 0;

	switch (cKey->len & 3)
	{
	case 3:
		k ^= (uint32_t)tail[2] << 16;
	case 2:
		k ^= (uint32_t)tail[1] << 8;
	case 1:
		k ^= tail[0];

		k *= c1;
		k = _GFX_ROTL32(k, 15);
		k *= c2;

		h ^= k;
	}

	// Finalize.
	h ^= (uint32_t)cKey->len;

	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}

/****************************/
int _gfx_hash_builder(_GFXHashBuilder* builder)
{
	assert(builder != NULL);

	// We have no idea how large the key is gonna be,
	// so we build it in a vector and claim its memory afterwards.
	// Initialize with a _GFXHashKey as header.
	gfx_vec_init(&builder->out, 1);

	if (gfx_vec_push(&builder->out, sizeof(_GFXHashKey), NULL))
		return 1;

	gfx_vec_clear(&builder->out);
	return 0;
}

/****************************/
_GFXHashKey* _gfx_hash_builder_get(_GFXHashBuilder* builder)
{
	assert(builder != NULL);

	// Claim data, set length & return.
	// If sizeof(char) is not 1 (!?), data would be truncated...
	const size_t len =
		(builder->out.size - sizeof(_GFXHashKey)) / sizeof(char);

	_GFXHashKey* key = gfx_vec_claim(&builder->out); // Implicitly clears.
	key->len = len;

	return key;
}
