/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/mem.h"
#include <string.h>

#if defined (GFX_WIN32)
	#include <intrin.h>
#endif


// Hash block size compatibility.
static_assert(sizeof(uint32_t) == 4, "MurmurHash3 blocks must be 4 bytes.");


// 'Randomized' hash seed (generated on the web).
#define GFX_HASH_SEED_ ((uint32_t)0x4ac093e6)


// Platform agnostic rotl.
#if defined (GFX_WIN32)
	#define GFX_ROTL32_(x, r) _rotl(x, r)
#else
	#define GFX_ROTL32_(x, r) ((x << r) | (x >> (32 - r)))
#endif


/****************************/
int gfx_hash_cmp_(const void* l, const void* r)
{
	const GFXHashKey_* kL = l;
	const GFXHashKey_* kR = r;

	// Non-zero = inequal.
	return kL->len != kR->len || memcmp(kL->bytes, kR->bytes, kL->len);
}

/****************************/
uint64_t gfx_hash_murmur3_(const void* key)
{
	const GFXHashKey_* cKey = key;
	const size_t nblocks = cKey->len / sizeof(uint32_t);

	uint32_t h = GFX_HASH_SEED_;

	const uint32_t c1 = 0xcc9e2d51;
	const uint32_t c2 = 0x1b873593;

	// Process the body in blocks of 4 bytes.
	const uint32_t* body = (const uint32_t*)cKey->bytes + nblocks;

	for (size_t i = nblocks; i; --i)
	{
		uint32_t k = *(body - i);

		k *= c1;
		k = GFX_ROTL32_(k, 15);
		k *= c2;

		h ^= k;
		h = GFX_ROTL32_(h, 13);
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
		k = GFX_ROTL32_(k, 15);
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
bool gfx_hash_builder_(GFXHashBuilder_* builder)
{
	assert(builder != NULL);

	// We have no idea how large the key is gonna be,
	// so we build it in a vector and claim its memory afterwards.
	// Initialize with a GFXHashKey_ as header.
	gfx_vec_init(&builder->out, 1);

	if (gfx_vec_push(&builder->out, sizeof(GFXHashKey_), NULL))
		return 1;

	gfx_vec_clear(&builder->out);
	return 0;
}

/****************************/
GFXHashKey_* gfx_hash_builder_get_(GFXHashBuilder_* builder)
{
	assert(builder != NULL);

	// Claim data, set length & return.
	const size_t len = builder->out.size - sizeof(GFXHashKey_);
	GFXHashKey_* key = gfx_vec_claim(&builder->out); // Implicitly clears.
	key->len = len;

	return key;
}
