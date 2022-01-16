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


/****************************
 * Cache hashtable key definition.
 */
typedef struct _GFXCacheKey
{
	size_t len;
	char bytes[];

} _GFXCacheKey;


/****************************
 * Hashtable key comparison function,
 * key is of type _GFXCacheKey*, assumes packed data.
 */
static int _gfx_cache_cmp(const void* l, const void* r)
{
	const _GFXCacheKey* kL = l;
	const _GFXCacheKey* kR = r;

	// Non-zero = inequal.
	return kL->len != kR->len || memcmp(kL->bytes, kR->bytes, kL->len);
}

/****************************
 * MurmurHash3 (32 bits) implementation as hashtable hash function,
 * key is of type _GFXCacheKey*.
 */
static uint64_t _gfx_cache_murmur3(const void* key)
{
	_Static_assert(sizeof(uint32_t) == 4, "MurmurHash3 blocks must be 4 bytes.");

	const _GFXCacheKey* cKey = key;
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
int _gfx_cache_init(_GFXCache* cache, _GFXDevice* device)
{
	assert(cache != NULL);
	assert(device != NULL);
	assert(device->context != NULL);

	cache->context = device->context;
	cache->vk.cache = VK_NULL_HANDLE;

	// Initialize the locks.
	if (!_gfx_mutex_init(&cache->lookupLock))
		return 0;

	if (!_gfx_mutex_init(&cache->createLock))
	{
		_gfx_mutex_clear(&cache->lookupLock);
		return 0;
	}

	// Initialize the hashtables.
	// Take the largest alignment of the key and element types.
	size_t align =
		GFX_MAX(_Alignof(_GFXCacheKey), _Alignof(_GFXCacheElem));

	gfx_map_init(&cache->immutable,
		sizeof(_GFXCacheElem), align, _gfx_cache_murmur3, _gfx_cache_cmp);

	gfx_map_init(&cache->mutable,
		sizeof(_GFXCacheElem), align, _gfx_cache_murmur3, _gfx_cache_cmp);

	return 1;
}

/****************************/
void _gfx_cache_clear(_GFXCache* cache)
{
	assert(cache != NULL);

	// TODO: Destroy all objects.

	gfx_map_clear(&cache->immutable);
	gfx_map_clear(&cache->mutable);

	_gfx_mutex_clear(&cache->createLock);
	_gfx_mutex_clear(&cache->lookupLock);
}
