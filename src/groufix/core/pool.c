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


/****************************
 * Mirrors _GFXHashKey, but containing only one _GFXCacheElem*.
 */
typedef struct _GFXRecycleKey
{
	size_t len;
	char bytes[sizeof(_GFXCacheElem*)];

} _GFXRecycleKey;


/****************************
 * Helper to make all subordinates unclaim their allocating descriptor block,
 * and let them link all blocks into the pool's free or full list again.
 */
static void _gfx_unclaim_pool_blocks(_GFXPool* pool)
{
	assert(pool != NULL);

	for (
		_GFXPoolSub* sub = (_GFXPoolSub*)pool->subs.head;
		sub != NULL;
		sub = (_GFXPoolSub*)sub->list.next)
	{
		// If the block was full, the subordinate should already have linked
		// it in the full list, so here we link it into the free list.
		if (sub->block != NULL)
		{
			gfx_list_insert_before(&pool->free, &sub->block->list, NULL);
			sub->block = NULL;
		}
	}
}

/****************************
 * Allocates and initializes a new block (i.e. Vulkan descriptor pool).
 * @return NULL on failure.
 *
 * The block is not linked into the free or full list of the pool,
 * must manually be claimed by either the pool or a subordinate!
 */
static _GFXPoolBlock* _gfx_alloc_pool_block(_GFXPool* pool)
{
	assert(pool != NULL);

	_GFXContext* context = pool->context;

	// Allocate block.
	_GFXPoolBlock* block = malloc(sizeof(_GFXPoolBlock));
	if (block == NULL)
		goto clean;

	// Create descriptor pool.
	// TODO: Come up with something to determine all the pool sizes.
	VkDescriptorPoolCreateInfo dpci = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,

		.pNext         = NULL,
		.flags         = 0,
		.maxSets       = 1000,
		.poolSizeCount = 11,

		.pPoolSizes = (VkDescriptorPoolSize[]){
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
		}
	};

	_GFX_VK_CHECK(context->vk.CreateDescriptorPool(
		context->vk.device, &dpci, NULL, &block->vk.pool), goto clean);

	// Init the rest & return.
	block->sets = 0;
	block->full = 0;
	gfx_list_init(&block->elems);

	return block;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not allocate a new Vulkan descriptor pool.");
	free(block);

	return NULL;
}

/****************************
 * Frees a descriptor block, freeing GPU memory of all descriptor sets.
 * _GFXPoolElem objects from this pool are not erased from their hashtables!
 * Does not unlink self from pool, must first be manually removed from any list!
 */
static void _gfx_free_pool_block(_GFXPool* pool, _GFXPoolBlock* block)
{
	assert(pool != NULL);
	assert(block != NULL);

	_GFXContext* context = pool->context;

	// Destroy descriptor pool, frees all descriptor sets for us.
	context->vk.DestroyDescriptorPool(
		context->vk.device, block->vk.pool, NULL);

	gfx_list_clear(&block->elems);
	free(block);
}

/****************************
 * Recycles a yet-unrecycled _GFXPoolElem object holding a descriptor set.
 * No subordinate must hold an allocating block (see _gfx_unclaim_pool_blocks)!
 * If its descriptor block is now fully recycled, it will be automatically
 * destroyed & freed.
 * @param map  Must be the hashtable elem is currently stored in.
 * @param elem Element to recycle, will not be in map anymore after this call.
 */
static int _gfx_recycle_pool_elem(_GFXPool* pool, GFXMap* map,
                                  _GFXPoolElem* elem)
{
	assert(pool != NULL);
	assert(elem != NULL);
	assert(map != NULL);
	assert(map != &pool->recycled);

	_GFXPoolBlock* block = elem->block;
	int recycled = 1;

	// Build a new key, only containing the cache element storing the
	// descriptor set layout, this way we do not search for specific
	// descriptors anymore, but only for the layout.
	// To get this, we know the first few bytes of a given key are required
	// to hold this cache element :)
	const _GFXHashKey* elemKey = gfx_map_key(map, elem);
	_GFXRecycleKey key;

	key.len = sizeof(key.bytes);
	memcpy(key.bytes, elemKey->bytes, sizeof(key.bytes));

	// Try to move the element to the recycled hashtable.
	if (!gfx_map_move(
		map, &pool->recycled, elem, sizeof(_GFXRecycleKey), &key))
	{
		// If that failed, erase it entirely, it will never be used again.
		gfx_list_erase(&block->elems, &elem->list);
		gfx_map_erase(map, elem);
		recycled = 0;
	}

	// Decrease the set count of its descriptor block.
	// If it hits zero, we can destroy the block.
	if ((--block->sets) == 0)
	{
		// Loop over all elements and erase them from the recycled hashtable.
		// We know they are all in recycled as the number of in-use sets is 0.
		while (block->elems.head != NULL)
		{
			_GFXPoolElem* bElem = (_GFXPoolElem*)block->elems.head;
			gfx_list_erase(&block->elems, &bElem->list);
			gfx_map_erase(&pool->recycled, bElem);
		}

		// Unlink itself from the pool.
		// We can do this because no subordinate must hold any block!
		gfx_list_erase(
			block->full ? &pool->full : &pool->free,
			&block->list);

		// Then call the regular free.
		_gfx_free_pool_block(pool, block);
	}

	return recycled;
}

/****************************/
int _gfx_pool_init(_GFXPool* pool, _GFXDevice* device, unsigned int flushes)
{
	assert(pool != NULL);
	assert(device != NULL);
	assert(device->context != NULL);

	pool->context = device->context;
	pool->flushes = flushes;

	// Initialize the locks.
	if (!_gfx_mutex_init(&pool->subLock))
		return 0;

	if (!_gfx_mutex_init(&pool->recLock))
	{
		_gfx_mutex_clear(&pool->subLock);
		return 0;
	}

	// Initialize all the lists & hashtables.
	gfx_list_init(&pool->free);
	gfx_list_init(&pool->full);
	gfx_list_init(&pool->subs);

	// Take the largest alignment of the key and element types.
	const size_t align =
		GFX_MAX(_Alignof(_GFXHashKey), _Alignof(_GFXPoolElem));

	gfx_map_init(&pool->immutable,
		sizeof(_GFXPoolElem), align, _gfx_hash_murmur3, _gfx_hash_cmp);
	gfx_map_init(&pool->recycled,
		sizeof(_GFXPoolElem), align, _gfx_hash_murmur3, _gfx_hash_cmp);

	return 1;
}

/****************************/
void _gfx_pool_clear(_GFXPool* pool)
{
	assert(pool != NULL);

	// Free all descriptor blocks.
	// For this we first loop over all subordinates.
	for (
		_GFXPoolSub* sub = (_GFXPoolSub*)pool->subs.head;
		sub != NULL;
		sub = (_GFXPoolSub*)sub->list.next)
	{
		if (sub->block != NULL)
			_gfx_free_pool_block(pool, sub->block);

		// While we're at it, clear the mutable hashtables.
		gfx_map_clear(&sub->mutable);
	}

	// Then free all remaining blocks.
	while (pool->free.head != NULL)
	{
		_GFXPoolBlock* block = (_GFXPoolBlock*)pool->free.head;
		gfx_list_erase(&pool->free, &block->list);
		_gfx_free_pool_block(pool, block);
	}

	while (pool->full.head != NULL)
	{
		_GFXPoolBlock* block = (_GFXPoolBlock*)pool->full.head;
		gfx_list_erase(&pool->full, &block->list);
		_gfx_free_pool_block(pool, block);
	}

	// Clear all the things.
	gfx_map_clear(&pool->immutable);
	gfx_map_clear(&pool->recycled);

	gfx_list_clear(&pool->free);
	gfx_list_clear(&pool->full);
	gfx_list_clear(&pool->subs);

	_gfx_mutex_clear(&pool->recLock);
	_gfx_mutex_clear(&pool->subLock);
}

/****************************/
int _gfx_pool_flush(_GFXPool* pool)
{
	assert(pool != NULL);

	// Firstly unclaim all subordinate blocks,
	// in case any subordinate doesn't need to allocate anymore!
	// Also allows us to recycle elements below :)
	_gfx_unclaim_pool_blocks(pool);

	// So we keep track of success.
	// This so at least all the flush counts of all elements in the
	// immutable hashtable are updated.
	int success = 1;

	// So we loop over all subordinates and flush them.
	for (
		_GFXPoolSub* sub = (_GFXPoolSub*)pool->subs.head;
		sub != NULL;
		sub = (_GFXPoolSub*)sub->list.next)
	{
		success = success &&
			gfx_map_merge(&pool->immutable, &sub->mutable);
	}

	// Then recycle all descriptor sets that need to be.
	for (
		_GFXPoolElem* elem = gfx_map_first(&pool->immutable);
		elem != NULL;
		elem = gfx_map_next(&pool->immutable, elem))
	{
		// Recycle it if it exceeds the max number of flushes.
		if (atomic_fetch_add(&elem->flushes, 1) + 1 >= pool->flushes)
			success = success &&
				_gfx_recycle_pool_elem(pool, &pool->immutable, elem);
	}

	return success;
}

/****************************/
void _gfx_pool_reset(_GFXPool* pool)
{
	assert(pool != NULL);

	_GFXContext* context = pool->context;

	// Firstly unclaim all subordinate blocks, just easier that way.
	_gfx_unclaim_pool_blocks(pool);

	// Ok so get rid of all the _GFXPoolElem objects in all hashtables.
	// As they will soon store non-existent descriptor sets.
	gfx_map_clear(&pool->immutable);
	gfx_map_clear(&pool->recycled);

	for (
		_GFXPoolSub* sub = (_GFXPoolSub*)pool->subs.head;
		sub != NULL;
		sub = (_GFXPoolSub*)sub->list.next)
	{
		gfx_map_clear(&sub->mutable);
	}

	// Then reset all the blocks and their Vulkan descriptor pools!
	for (
		_GFXPoolBlock* block = (_GFXPoolBlock*)pool->free.head;
		block != NULL;
		block = (_GFXPoolBlock*)block->list.next)
	{
		block->sets = 0;
		block->full = 0;

		gfx_list_clear(&block->elems);
		context->vk.ResetDescriptorPool(context->vk.device, block->vk.pool, 0);
	}

	for (
		_GFXPoolBlock* block = (_GFXPoolBlock*)pool->full.head;
		block != NULL;
		block = (_GFXPoolBlock*)block->list.next)
	{
		block->sets = 0;
		block->full = 0;

		gfx_list_clear(&block->elems);
		context->vk.ResetDescriptorPool(context->vk.device, block->vk.pool, 0);
	}
}

/****************************/
void _gfx_pool_sub(_GFXPool* pool, _GFXPoolSub* sub)
{
	assert(pool != NULL);
	assert(sub != NULL);

	// Initialize the subordinate.
	// Same alignment as the pool's hashtables.
	const size_t align =
		GFX_MAX(_Alignof(_GFXHashKey), _Alignof(_GFXPoolElem));

	gfx_map_init(&sub->mutable,
		sizeof(_GFXPoolElem), align, _gfx_hash_murmur3, _gfx_hash_cmp);

	sub->block = NULL;

	// Lastly to link the subordinate into the pool.
	gfx_list_insert_after(&pool->subs, &sub->list, NULL);
}

/****************************/
void _gfx_pool_unsub(_GFXPool* pool, _GFXPoolSub* sub)
{
	assert(pool != NULL);
	assert(sub != NULL);

	// First unclaim all subordinate blocks,
	// mostly so we can recycle on failure.
	_gfx_unclaim_pool_blocks(pool);

	// Flush this subordinate & clear the hashtable.
	// If it did not want to merge, the descriptor sets are lost,
	// instead we will try to recycle them!
	if (!gfx_map_merge(&pool->immutable, &sub->mutable))
	{
		size_t lost = 0;

		// Try to recycle every element instead...
		// We keep getting the first element, as the recycle call will empty
		// the hashtable!
		for (
			_GFXPoolElem* elem = gfx_map_first(&sub->mutable);
			elem != NULL;
			elem = gfx_map_first(&sub->mutable))
		{
			if (!_gfx_recycle_pool_elem(pool, &sub->mutable, elem))
				++lost;
		}

		if (lost > 0) gfx_log_warn(
			"Partial pool flush failed, lost %"GFX_PRIs" Vulkan descriptor sets. "
			"Will remain unavailable until block is reset or fully recycled.",
			lost);
	}

	gfx_map_clear(&sub->mutable);

	// Unlink subordinate from the pool.
	gfx_list_erase(&pool->subs, &sub->list);
}

/****************************/
_GFXPoolElem* _gfx_pool_get(_GFXPool* pool, _GFXPoolSub* sub,
                            const _GFXCacheElem* setLayout,
                            const _GFXHashKey* key,
                            const void* update)
{
	assert(pool != NULL);
	assert(sub != NULL);
	assert(setLayout != NULL);
	assert(setLayout->type == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
	assert(key != NULL);
	assert(update != NULL);

	// TODO: Implement.

	return NULL;
}

/****************************/
void _gfx_pool_recycle(_GFXPool* pool,
                       const _GFXCacheElem* setLayout,
                       const _GFXHashKey* key)
{
	assert(pool != NULL);
	assert(setLayout != NULL);
	assert(setLayout->type == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
	assert(key != NULL);

	// First unclaim all subordinate blocks, so we can recycle elements.
	_gfx_unclaim_pool_blocks(pool);

	// Then find all matching elements in all hashtables and recycle them!
	// Obviously we only check all subordinate hashtables & the immutable one.
	// If any element gets recycled, it will be moved to the recycled table!
	const uint64_t hash = pool->immutable.hash(key); // TODO: Implement _gfx_pool_hrecycle?
	size_t lost = 0;

	for (
		_GFXPoolSub* sub = (_GFXPoolSub*)pool->subs.head;
		sub != NULL;
		sub = (_GFXPoolSub*)sub->list.next)
	{
		// We keep searching, as elements can be erased from the map
		// we have to do a fresh search every time.
		for (
			_GFXPoolElem* elem = gfx_map_hsearch(&sub->mutable, key, hash);
			elem != NULL;
			elem = gfx_map_hsearch(&sub->mutable, key, hash))
		{
			if (!_gfx_recycle_pool_elem(pool, &sub->mutable, elem))
				++lost;
		}
	}

	// Same search structure as above.
	for (
		_GFXPoolElem* elem = gfx_map_hsearch(&pool->immutable, key, hash);
		elem != NULL;
		elem = gfx_map_hsearch(&pool->immutable, key, hash))
	{
		if (!_gfx_recycle_pool_elem(pool, &pool->immutable, elem))
			++lost;
	}

	if (lost > 0) gfx_log_warn(
		"Recycling a pool element failed, lost %"GFX_PRIs" Vulkan descriptor "
		"sets. Will remain unavailable until block is reset or fully recycled.",
		lost);
}
