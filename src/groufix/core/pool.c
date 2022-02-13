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


/****************************
 * Allocates and initializes a new block (i.e. Vulkan descriptor pool).
 * @return NULL on failure.
 *
 * The block is not linked into the free or full list of the pool!
 * Must manually be claimed by either the pool or a subordinate.
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
	gfx_list_init(&block->elems);

	return block;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not allocate a new Vulkan descriptor pool.");
	free(block);

	return NULL;
}

/****************************
 * Frees a descriptor block, freeing memory of all descriptor sets.
 * Does not unlink from any list, must first be manually removed from any list!
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

	// TODO: Obviously free all blocks, undo all subordinates,
	// and free all elements in the hashtables n such.

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

	// TODO: Implement.

	return 0;
}

/****************************/
void _gfx_pool_reset(_GFXPool* pool)
{
	assert(pool != NULL);

	// TODO: Implement.
}

/****************************/
int _gfx_pool_sub(_GFXPool* pool, _GFXPoolSub* sub)
{
	assert(pool != NULL);
	assert(sub != NULL);

	// TODO: Implement.

	return 0;
}

/****************************/
void _gfx_pool_unsub(_GFXPool* pool, _GFXPoolSub* sub)
{
	assert(pool != NULL);
	assert(sub != NULL);

	// TODO: Implement.
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
