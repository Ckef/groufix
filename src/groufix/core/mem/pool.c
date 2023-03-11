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
 * and let them link all blocks into the pool's free list again.
 */
static void _gfx_unclaim_pool_blocks(_GFXPool* pool)
{
	for (
		_GFXPoolSub* sub = (_GFXPoolSub*)pool->subs.head;
		sub != NULL;
		sub = (_GFXPoolSub*)sub->list.next)
	{
		// If the block was full, the subordinate should already have linked
		// it in the full list, so here we link it into the free list.
		// We keep inserting at the beginning so hot blocks keep being used.
		// This way we don't instantly disperse over all available blocks.
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
	uint32_t sams = 1000;
	uint32_t combImgSams = 1000;
	uint32_t samImgs = 1000;
	uint32_t stoImgs = 1000;
	uint32_t uniTexBufs = 1000;
	uint32_t stoTexBufs = 1000;
	uint32_t uniBufs = 1000;
	uint32_t stoBufs = 1000;
	uint32_t uniBufDyns = 1000;
	uint32_t stoBufDyns = 1000;
	uint32_t inps = 1000;

	VkDescriptorPoolCreateInfo dpci = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,

		.pNext         = NULL,
		.flags         = 0,
		.maxSets       = 1000,
		.poolSizeCount = 11,

		.pPoolSizes = (VkDescriptorPoolSize[]){
			{ VK_DESCRIPTOR_TYPE_SAMPLER, sams },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, combImgSams },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, samImgs },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, stoImgs },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, uniTexBufs },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, stoTexBufs },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniBufs },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stoBufs },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, uniBufDyns },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, stoBufDyns },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, inps }
		}
	};

	_GFX_VK_CHECK(context->vk.CreateDescriptorPool(
		context->vk.device, &dpci, NULL, &block->vk.pool), goto clean);

	// Init the rest & return.
	gfx_list_init(&block->elems);
	block->full = 0;
	atomic_store_explicit(&block->sets, 0, memory_order_relaxed);

	// Weee.
	gfx_log_debug(
		"New Vulkan descriptor pool allocated:\n"
		"    #samplers: %"PRIu32".\n"
		"    #combined image samplers: %"PRIu32".\n"
		"    #sampled images: %"PRIu32".\n"
		"    #storage images: %"PRIu32".\n"
		"    #uniform texel buffers: %"PRIu32".\n"
		"    #storage texel buffers: %"PRIu32".\n"
		"    #uniform buffers: %"PRIu32".\n"
		"    #storage buffers: %"PRIu32".\n"
		"    #dynamic uniform buffers: %"PRIu32".\n"
		"    #dynamic storage buffers: %"PRIu32".\n"
		"    #attachment inputs: %"PRIu32".\n",
		sams, combImgSams,
		samImgs, stoImgs,
		uniTexBufs, stoTexBufs,
		uniBufs, stoBufs,
		uniBufDyns, stoBufDyns,
		inps);

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

	gfx_log_debug("Freed Vulkan descriptor pool.");
}

/****************************
 * Recycles a yet-unrecycled _GFXPoolElem object holding a descriptor set.
 * No subordinate may hold an allocating block (see _gfx_unclaim_pool_blocks)!
 * If its descriptor block is now fully recycled, it will be automatically
 * destroyed & freed.
 * @param map  Must be the hashtable elem is currently stored in.
 * @param elem Element to recycle, will not be in map anymore after this call.
 * @return Non-zero if recycled, zero if erased.
 */
static bool _gfx_recycle_pool_elem(_GFXPool* pool, GFXMap* map,
                                   _GFXPoolElem* elem)
{
	assert(pool != NULL);
	assert(elem != NULL);
	assert(map != NULL);
	assert(map != &pool->recycled);

	_GFXPoolBlock* block = elem->block;
	bool recycled = 1;

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
	// Make sure to use the fast variants of map_(move|erase), so
	// we can keep iterating outside this function!
	if (!gfx_map_fmove(
		map, &pool->recycled, elem, sizeof(_GFXRecycleKey), &key))
	{
		// If that failed, erase it entirely, it will never be used again.
		gfx_list_erase(&block->elems, &elem->list);
		gfx_map_ferase(map, elem);
		recycled = 0;
	}

	// Decrease the set count of its descriptor block.
	// If it hits zero, we can destroy the block.
	// Note it is an atomic variable, but this function does not need to be
	// thread safe at all, so in this case any side effects don't matter.
	if (atomic_fetch_sub_explicit(&block->sets, 1, memory_order_relaxed) == 1)
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
		// We can do this because no subordinate is allowed to hold a block!
		gfx_list_erase(
			block->full ? &pool->full : &pool->free,
			&block->list);

		// Then call the regular free.
		_gfx_free_pool_block(pool, block);
	}

	return recycled;
}

/****************************
 * Makes yet-unstale _GFXPoolElem objects holding a descriptor set stale,
 * causing it to never be returned by _gfx_pool_get until truly recycled.
 * Might recycle the element immediately!
 * @see _gfx_recycle_pool_elem.
 * @param flushes Gets truly recycled after #flushes.
 */
static bool _gfx_make_pool_elem_stale(_GFXPool* pool, GFXMap* map,
                                      _GFXPoolElem* elem, unsigned int flushes)
{
	assert(pool != NULL);
	assert(elem != NULL);
	assert(map != NULL);
	assert(map != &pool->stale);

	// First check if the element was already flushed enough times.
	// If so, immediately recycle.
	const unsigned int flushed =
		pool->flushes -
		atomic_load_explicit(&elem->flushes, memory_order_relaxed);

	if (flushed >= flushes)
		return _gfx_recycle_pool_elem(pool, map, elem);

	// Try to move the element to the stale hashtable.
	// Make sure to use the fast variants of map_(move|erase), so
	// we can keep iterating outside this function!
	if (!gfx_map_fmove(
		map, &pool->stale, elem, 0, NULL))
	{
		// If that failed, erase it entirely, it will never be used again.
		gfx_list_erase(&elem->block->elems, &elem->list);
		gfx_map_ferase(map, elem);
		return 0;
	}

	// And set its new flush count on success.
	atomic_store_explicit(
		&elem->flushes, flushes - flushed, memory_order_relaxed);

	return 1;
}

/****************************/
bool _gfx_pool_init(_GFXPool* pool, _GFXDevice* device, unsigned int flushes)
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

	gfx_map_init(&pool->immutable,
		sizeof(_GFXPoolElem), _gfx_hash_murmur3, _gfx_hash_cmp);
	gfx_map_init(&pool->stale,
		sizeof(_GFXPoolElem), _gfx_hash_murmur3, _gfx_hash_cmp);
	gfx_map_init(&pool->recycled,
		sizeof(_GFXPoolElem), _gfx_hash_murmur3, _gfx_hash_cmp);

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
	gfx_map_clear(&pool->stale);
	gfx_map_clear(&pool->recycled);

	gfx_list_clear(&pool->free);
	gfx_list_clear(&pool->full);
	gfx_list_clear(&pool->subs);

	_gfx_mutex_clear(&pool->recLock);
	_gfx_mutex_clear(&pool->subLock);
}

/****************************/
bool _gfx_pool_flush(_GFXPool* pool)
{
	assert(pool != NULL);

	// Firstly unclaim all subordinate blocks,
	// in case any subordinate doesn't need to allocate anymore!
	// Also allows us to recycle elements below :)
	_gfx_unclaim_pool_blocks(pool);

	// So we keep track of success.
	// This so at least all the flush counts of all elements in the
	// immutable hashtable are updated.
	bool success = 1;

	// So we loop over all subordinates and flush them.
	for (
		_GFXPoolSub* sub = (_GFXPoolSub*)pool->subs.head;
		sub != NULL;
		sub = (_GFXPoolSub*)sub->list.next)
	{
		success = success &&
			gfx_map_merge(&pool->immutable, &sub->mutable);
	}

	if (!success) gfx_log_warn(
		"Pool flush failed to make cache available to all threads.");

	// Then recycle all descriptor sets that need to be.
	// We are moving nodes from immutable to recycled, but gfx_map_fmove
	// guarantees the node order stays the same.
	// We use this to loop 'over' the moved nodes.
	size_t lost = 0;

	// Start at the immutable table.
	GFXMap* map = &pool->immutable;
	_GFXPoolElem* elem;

recycle:
	elem = gfx_map_first(map);

	while (elem != NULL)
	{
		_GFXPoolElem* next = gfx_map_next(map, elem);

		// Recycle it if it has no more flushes to do (i.e. reaches 0).
		if (atomic_fetch_sub_explicit(&elem->flushes, 1, memory_order_relaxed) == 1)
			lost += !_gfx_recycle_pool_elem(pool, map, elem);

		elem = next;
	}

	// Continue to the stale table.
	if (map == &pool->immutable)
	{
		map = &pool->stale;
		goto recycle;
	}

	// Shrink the immutable & stale hashtables back down.
	gfx_map_shrink(&pool->immutable);
	gfx_map_shrink(&pool->stale);

	if (lost > 0) gfx_log_warn(
		"Pool flush failed, lost %"GFX_PRIs" Vulkan descriptor sets. "
		"Will remain unavailable until blocks are reset or fully recycled.",
		lost);

	return success && (lost == 0);
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
	gfx_map_clear(&pool->stale);
	gfx_map_clear(&pool->recycled);

	for (
		_GFXPoolSub* sub = (_GFXPoolSub*)pool->subs.head;
		sub != NULL;
		sub = (_GFXPoolSub*)sub->list.next)
	{
		gfx_map_clear(&sub->mutable);
	}

	// Then move all the full blocks to the free list.
	while (pool->full.head != NULL)
	{
		_GFXPoolBlock* block = (_GFXPoolBlock*)pool->full.head;
		gfx_list_erase(&pool->full, &block->list);
		gfx_list_insert_after(&pool->free, &block->list, NULL);

		// Reset the full flag.
		block->full = 0;
	}

	// And reset all the blocks and their Vulkan descriptor pools.
	// TODO: Free pools based on how many recycled descriptors there were.
	for (
		_GFXPoolBlock* block = (_GFXPoolBlock*)pool->free.head;
		block != NULL;
		block = (_GFXPoolBlock*)block->list.next)
	{
		gfx_list_clear(&block->elems);
		atomic_store_explicit(&block->sets, 0, memory_order_relaxed);

		context->vk.ResetDescriptorPool(
			context->vk.device, block->vk.pool, 0);
	}
}

/****************************/
void _gfx_pool_sub(_GFXPool* pool, _GFXPoolSub* sub)
{
	assert(pool != NULL);
	assert(sub != NULL);

	// Initialize the subordinate.
	gfx_map_init(&sub->mutable,
		sizeof(_GFXPoolElem), _gfx_hash_murmur3, _gfx_hash_cmp);

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
	// If it did not want to merge, the descriptor sets are lost...
	if (!gfx_map_merge(&pool->immutable, &sub->mutable))
	{
		// Try to make every element stale instead...
		// Same as in _gfx_pool_flush, we loop 'over' the moved nodes.
		size_t lost = 0;
		_GFXPoolElem* elem = gfx_map_first(&sub->mutable);

		while (elem != NULL)
		{
			_GFXPoolElem* next = gfx_map_next(&sub->mutable, elem);

			// We don't actually know any #flushes to use for this,
			// so just use the global #flushes of the pool.
			lost += !_gfx_make_pool_elem_stale(
				pool, &sub->mutable, elem, pool->flushes);

			elem = next;
		}

		if (lost > 0) gfx_log_warn(
			"Partial pool flush failed, lost %"GFX_PRIs" Vulkan descriptor sets. "
			"Will remain unavailable until blocks are reset or fully recycled.",
			lost);
	}

	gfx_map_clear(&sub->mutable);

	// Unlink subordinate from the pool.
	gfx_list_erase(&pool->subs, &sub->list);
}

/****************************/
void _gfx_pool_recycle(_GFXPool* pool,
                       const _GFXHashKey* key, unsigned int flushes)
{
	assert(pool != NULL);
	assert(key != NULL);

	const uint64_t hash = pool->immutable.hash(key);

	// First unclaim all subordinate blocks, so we can recycle elements.
	_gfx_unclaim_pool_blocks(pool);

	// Then find all matching elements in all tables and make them stale!
	// Obviously we only check all subordinate hashtables & the immutable one.
	size_t lost = 0;

	for (
		_GFXPoolSub* sub = (_GFXPoolSub*)pool->subs.head;
		sub != NULL;
		sub = (_GFXPoolSub*)sub->list.next)
	{
		// Again, gfx_map_fmove guarantees the node order stays the same.
		// We use this to loop 'over' the moved nodes.
		_GFXPoolElem* elem = gfx_map_hsearch(&sub->mutable, key, hash);
		while (elem != NULL)
		{
			_GFXPoolElem* next = gfx_map_next_equal(&sub->mutable, elem);

			lost += !_gfx_make_pool_elem_stale(
				pool, &sub->mutable, elem, flushes);

			elem = next;
		}
	}

	// Same search structure as above.
	_GFXPoolElem* elem = gfx_map_hsearch(&pool->immutable, key, hash);
	while (elem != NULL)
	{
		_GFXPoolElem* next = gfx_map_next_equal(&pool->immutable, elem);

		lost += !_gfx_make_pool_elem_stale(
			pool, &pool->immutable, elem, flushes);

		elem = next;
	}

	// Note: no need to shrink any maps, flushing will :)
	// Even the subordinate maps will be shrunk when merged!

	if (lost > 0) gfx_log_warn(
		"Pool recycling failed, lost %"GFX_PRIs" Vulkan descriptor sets. "
		"Will remain unavailable until blocks are reset or fully recycled.",
		lost);
}

/****************************/
_GFXPoolElem* _gfx_pool_get(_GFXPool* pool, _GFXPoolSub* sub,
                            const _GFXCacheElem* setLayout,
                            const _GFXHashKey* key, const void* update)
{
	assert(pool != NULL);
	assert(sub != NULL);
	assert(setLayout != NULL);
	assert(setLayout->type == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
	assert(key != NULL);

	_GFXContext* context = pool->context;
	const uint64_t hash = pool->immutable.hash(key);

	// First we check the pool's immutable table.
	// We check this first because elements will always be flushed to this,
	// meaning our element will most likely be here after 1 frame.
	// Given this function is only allowed to run concurrently with itself,
	// we don't need any locks :)
	_GFXPoolElem* elem = gfx_map_hsearch(&pool->immutable, key, hash);
	if (elem != NULL) goto found;

	// If not found, we check the subordinate's table.
	elem = gfx_map_hsearch(&sub->mutable, key, hash);
	if (elem != NULL) goto found;

	// If still not found, go check the recycled table.
	// When an element is found, we need to move it to the subordinate.
	// Therefore the recycled table can change, and we need to lock it.
	// First create a key real quick tho (from the first few bytes of `key`).
	_GFXRecycleKey recKey;
	recKey.len = sizeof(recKey.bytes);
	memcpy(recKey.bytes, key->bytes, sizeof(recKey.bytes));

	_gfx_mutex_lock(&pool->recLock);

	elem = gfx_map_search(&pool->recycled, &recKey);
	if (elem != NULL)
		// If a compatible descriptor set layout is found,
		// move it to the subordinate so we can unlock.
		if (!gfx_map_hmove(
			&pool->recycled, &sub->mutable,
			elem, _gfx_hash_size(key), key, hash))
		{
			_gfx_mutex_unlock(&pool->recLock);
			return NULL;
		}

	_gfx_mutex_unlock(&pool->recLock);

	// If we STILL have no element, allocate a new descriptor set.
	if (elem == NULL)
	{
		// Goto here to try another descriptor block.
	try_block:

		// To do this, we need a descriptor block.
		// If we don't have one, go claim one from the free list.
		// We need to lock for this again.
		if (sub->block == NULL)
		{
			_gfx_mutex_lock(&pool->subLock);

			sub->block = (_GFXPoolBlock*)pool->free.head;
			if (sub->block != NULL)
				gfx_list_erase(&pool->free, &sub->block->list);

			_gfx_mutex_unlock(&pool->subLock);

			// If we didn't manage to claim a block, make one ourselves...
			if (sub->block == NULL)
				if ((sub->block = _gfx_alloc_pool_block(pool)) == NULL)
				{
					// ...
					if (elem != NULL) gfx_map_erase(&sub->mutable, elem);
					return NULL;
				}
		}

		// Quickly try to get a map element if we didn't already.
		if (elem == NULL)
		{
			elem = gfx_map_hinsert(
				&sub->mutable, NULL, _gfx_hash_size(key), key, hash);

			if (elem == NULL) return NULL;
		}

		// Now allocate a descriptor set from this block/pool.
		// Note that the descriptor block is now claimed by this subordinate,
		// nothing else will access it but this subordinate.
		// Except maybe the `sets` field by other recycling threads.
		VkDescriptorSetAllocateInfo dsai = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,

			.pNext              = NULL,
			.descriptorPool     = sub->block->vk.pool,
			.descriptorSetCount = 1,
			.pSetLayouts        = &setLayout->vk.setLayout
		};

		VkResult result = context->vk.AllocateDescriptorSets(
			context->vk.device, &dsai, &elem->vk.set);

		// If the descriptor pool was out of memory,
		// move the descriptor block to the full list and try again.
		// We must lock for this again..
		if (
			result == VK_ERROR_FRAGMENTED_POOL ||
			result == VK_ERROR_OUT_OF_POOL_MEMORY)
		{
			_gfx_mutex_lock(&pool->subLock);

			// Don't forget to set the full flag!
			sub->block->full = 1;
			gfx_list_insert_after(&pool->full, &sub->block->list, NULL);

			_gfx_mutex_unlock(&pool->subLock);

			sub->block = NULL;
			goto try_block;
		}

		// Success?
		_GFX_VK_CHECK(result,
			{
				gfx_map_erase(&sub->mutable, elem);
				return NULL;
			});

		// And link the element and block together.
		elem->block = sub->block;
		gfx_list_insert_after(&sub->block->elems, &elem->list, NULL);
	}

	// Now that we surely have an element, initialize it!
	// Icrease the set count of its descriptor block.
	// Note that it NEEDS to be atomic, any thread can access any block if
	// they all happen to grab recycled sets.
	atomic_fetch_add_explicit(&elem->block->sets, 1, memory_order_relaxed);

	// Ok now it's just a matter of updating the actual Vulkan descriptors!
	// Note that it can be an empty set, check template existence.
	if (setLayout->vk.template != VK_NULL_HANDLE)
		context->vk.UpdateDescriptorSetWithTemplate(
			context->vk.device, elem->vk.set, setLayout->vk.template, update);

	// Reset #flushes of the element & return when found.
found:
	atomic_store_explicit(&elem->flushes, pool->flushes, memory_order_relaxed);
	return elem;
}
