/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/mem.h"
#include <assert.h>
#include <stdlib.h>
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


// Pushes a field to a map key being built.
#define _GFX_KEY_PUSH(field) \
	do { \
		if (!gfx_vec_push(&out, sizeof(field), &(field))) \
			goto clean; \
	} while (0)


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

/****************************
 * Allocates & builds a hashable key value from a Vk*CreateInfo struct
 * with given replace handles for non-hashable fields.
 * @return Key value, must call free() on success (NULL on failure).
 */
static _GFXCacheKey* _gfx_cache_alloc_key(const VkStructureType* createInfo,
                                          const void** handles)
{
	assert(createInfo != NULL);

	// We have no idea how large the key is gonna be,
	// so we build it into a vector container, and claim its memory afterwards.
	GFXVec out;
	gfx_vec_init(&out, 1);

	if (!gfx_vec_push(&out, sizeof(_GFXCacheKey), NULL))
		goto clean;

	// Based on type, push all the to-be-hashed data.
	// Here we try to minimize the data actually necessary to specify
	// a unique cache object, so everything will be packed tightly.
	// Plus we insert the given handles for fields we cannot hash.
	size_t currHandle = 0;

	// TODO: Continue implementing.
	switch (*createInfo)
	{
	case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO:
		_GFX_KEY_PUSH(*createInfo);
		break;

	case VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO:
		_GFX_KEY_PUSH(*createInfo);
		break;

	case VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO:
		_GFX_KEY_PUSH(*createInfo);
		break;

	case VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO:
		_GFX_KEY_PUSH(*createInfo);
		break;

	case VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO:
		_GFX_KEY_PUSH(*createInfo);
		break;

	default:
		break;
	}

	// Claim data, set length & return.
	// If sizeof(char) is not 1 (!?), data would be truncated...
	size_t len = (out.size - sizeof(_GFXCacheKey)) / sizeof(char);

	_GFXCacheKey* key = gfx_vec_claim(&out); // Implicitly clears.
	key->len = len;

	return key;


	// Cleanup on failure.
clean:
	gfx_vec_clear(&out);
	gfx_log_error("Could not allocate key for cached Vulkan object.");

	return NULL;
}

/****************************
 * Creates a new Vulkan object using the given Vk*CreateInfo struct and
 * outputs to the given _GFXCacheElem struct.
 * @return Non-zero on success.
 */
static int _gfx_cache_create_elem(_GFXCache* cache, _GFXCacheElem* elem,
                                  const VkStructureType* createInfo)
{
	assert(cache != NULL);
	assert(elem != NULL);
	assert(createInfo != NULL);

	_GFXContext* context = cache->context;

	// Firstly, set type.
	elem->type = *createInfo;

	// Then call the appropriate create function.
	switch (elem->type)
	{
	case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreateDescriptorSetLayout(context->vk.device,
				(const VkDescriptorSetLayoutCreateInfo*)createInfo, NULL,
				&elem->setLayout),
			goto error);
		break;

	case VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreatePipelineLayout(context->vk.device,
				(const VkPipelineLayoutCreateInfo*)createInfo, NULL,
				&elem->layout),
			goto error);
		break;

	case VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreateGraphicsPipelines(context->vk.device,
				cache->vk.cache, 1,
				(const VkGraphicsPipelineCreateInfo*)createInfo, NULL,
				&elem->pipeline),
			goto error);
		break;

	case VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreateComputePipelines(context->vk.device,
				cache->vk.cache, 1,
				(const VkComputePipelineCreateInfo*)createInfo, NULL,
				&elem->pipeline),
			goto error);
		break;

	case VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreateSampler(context->vk.device,
				(const VkSamplerCreateInfo*)createInfo, NULL,
				&elem->sampler),
			goto error);
		break;

	default:
		goto error;
	}

	return 1;


	// Error on failure.
error:
	gfx_log_error("Could not create cached Vulkan object.");
	return 0;
}

/****************************
 * Destroys the Vulkan object stored in the given _GFXCacheElem struct.
 */
static void _gfx_cache_destroy_elem(_GFXCache* cache, _GFXCacheElem* elem)
{
	assert(cache != NULL);
	assert(elem != NULL);

	_GFXContext* context = cache->context;

	// Call the appropriate destroy function from type.
	switch (elem->type)
	{
	case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO:
		context->vk.DestroyDescriptorSetLayout(
			context->vk.device, elem->setLayout, NULL);
		break;

	case VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO:
		context->vk.DestroyPipelineLayout(
			context->vk.device, elem->layout, NULL);
		break;

	case VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO:
	case VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO:
		context->vk.DestroyPipeline(
			context->vk.device, elem->pipeline, NULL);
		break;

	case VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO:
		context->vk.DestroySampler(
			context->vk.device, elem->sampler, NULL);
		break;

	default:
		break;
	}
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

	// Destroy all objects in the mutable cache.
	for (
		_GFXCacheElem* elem = gfx_map_first(&cache->mutable);
		elem != NULL;
		elem = gfx_map_next(&cache->mutable, elem))
	{
		_gfx_cache_destroy_elem(cache, elem);
	}

	// Destroy all objects in the immutable cache.
	for (
		_GFXCacheElem* elem = gfx_map_first(&cache->immutable);
		elem != NULL;
		elem = gfx_map_next(&cache->immutable, elem))
	{
		_gfx_cache_destroy_elem(cache, elem);
	}

	// Clear all other things.
	gfx_map_clear(&cache->immutable);
	gfx_map_clear(&cache->mutable);

	_gfx_mutex_clear(&cache->createLock);
	_gfx_mutex_clear(&cache->lookupLock);
}

/****************************/
int _gfx_cache_flush(_GFXCache* cache)
{
	assert(cache != NULL);

	// No need to lock anything, we just merge the tables.
	return gfx_map_merge(&cache->immutable, &cache->mutable);
}

/****************************/
int _gfx_cache_warmup(_GFXCache* cache,
                      const VkStructureType* createInfo,
                      const void** handles)
{
	assert(cache != NULL);
	assert(createInfo != NULL);

	// Firstly we create a key value & hash it.
	_GFXCacheKey* key = _gfx_cache_alloc_key(createInfo, handles);
	if (key == NULL) return 0;

	const uint64_t hash = cache->immutable.hash(key);

	// Here we do need to lock the immutable cache, as we want the function
	// to be reentrant. However we have no dedicated lock.
	// Luckily this function _does not_ need to be able to run concurrently
	// with _gfx_cache_get, so we abuse the lookup lock :)
	_gfx_mutex_lock(&cache->lookupLock);

	// Try to find a matching element first.
	_GFXCacheElem* elem = gfx_map_hsearch(&cache->immutable, key, hash);
	if (elem != NULL)
		// Found one, done, we do not care if it is completely built yet.
		_gfx_mutex_unlock(&cache->lookupLock);
	else
	{
		// If not found, insert a new element.
		// Then immediately unlock so other warmups can be performed.
		elem = gfx_map_hinsert(&cache->immutable, NULL,
			sizeof(_GFXCacheKey) + sizeof(char) * key->len, key, hash);

		_gfx_mutex_unlock(&cache->lookupLock);

		// THEN create it :)
		if (elem == NULL || !_gfx_cache_create_elem(cache, elem, createInfo))
		{
			// Failed.. I suppose we erase the element.
			if (elem != NULL)
			{
				_gfx_mutex_lock(&cache->lookupLock);
				gfx_map_erase(&cache->immutable, elem);
				_gfx_mutex_unlock(&cache->lookupLock);
			}

			free(key);
			return 0;
		}
	}

	// Free data & return.
	free(key);
	return 1;
}

/****************************/
_GFXCacheElem* _gfx_cache_get(_GFXCache* cache,
                              const VkStructureType* createInfo,
                              const void** handles)
{
	assert(cache != NULL);
	assert(createInfo != NULL);

	// Again, create a key value & hash it.
	_GFXCacheKey* key = _gfx_cache_alloc_key(createInfo, handles);
	if (key == NULL) return NULL;

	const uint64_t hash = cache->immutable.hash(key);

	// First we check the immutable cache.
	// Since this function is only allowed to run concurrently with itself,
	// we do not modify and therefore do not lock this cache :)
	_GFXCacheElem* elem = gfx_map_hsearch(&cache->immutable, key, hash);
	if (elem != NULL) goto found;

	// If not found in the immutable cache, check the mutable cache.
	// For this lookup we obviously do lock.
	_gfx_mutex_lock(&cache->lookupLock);
	elem = gfx_map_hsearch(&cache->mutable, key, hash);
	_gfx_mutex_unlock(&cache->lookupLock);

	if (elem != NULL) goto found;

	// If we did not find it yet, we need to insert a new element in the
	// mutable cache. We want other threads to still be able to query while
	// creating, so we lock for 'creation' separately.
	// But then we need to immediately check if the element already exists.
	// This because multiple threads could simultaneously decide to create
	// the same new element.
	// TODO: Have a more fine grained mechanism for locking, so we only
	// block when trying to create the SAME object, and not for ALL objects?
	_gfx_mutex_lock(&cache->createLock);

	_gfx_mutex_lock(&cache->lookupLock);
	elem = gfx_map_hsearch(&cache->mutable, key, hash);
	_gfx_mutex_unlock(&cache->lookupLock);

	if (elem != NULL)
	{
		_gfx_mutex_unlock(&cache->createLock);
		goto found;
	}

	// At this point we are the thread to actually create the new element.
	// We first create, then insert, so other threads don't accidentally
	// pick an incomplete element.
	_GFXCacheElem newElem;
	if (!_gfx_cache_create_elem(cache, &newElem, createInfo))
	{
		// Uh oh failed to create :(
		_gfx_mutex_unlock(&cache->createLock);
		free(key);
		return NULL;
	}

	// We created the thing, now insert the thing.
	// For this we block any lookups again.
	// When we're done we can also unlock for creation tho :)
	_gfx_mutex_lock(&cache->lookupLock);

	elem = gfx_map_hinsert(&cache->mutable, &newElem,
		sizeof(_GFXCacheKey) + sizeof(char) * key->len, key, hash);

	_gfx_mutex_unlock(&cache->lookupLock);
	_gfx_mutex_unlock(&cache->createLock);

	if (elem != NULL) goto found;

	// Ah, well, it is not in the map, away with it then...
	_gfx_cache_destroy_elem(cache, &newElem);
	free(key);
	return NULL;


	// Free data & return when found.
found:
	free(key);
	return elem;
}
