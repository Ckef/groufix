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


// Maximum size for a heap to be considered 'small' (2 GiB).
// If a heap is 'small', blocks will be the size of the heap divided by 8.
#define _GFX_SMALL_HEAP_SIZE (2048ull * 1024 * 1024)

// Preferred memory block size of a 'large' heap (256 MiB).
#define _GFX_PREFERRED_MEM_BLOCK_SIZE (256ull * 1024 * 2014)


// Check whether a value is a power of two (0 counts).
#define _GFX_IS_POWER_OF_TWO(x) \
	((x & (x - 1)) == 0)

// Get the strictest alignment (i.e. the least significant bit) of an offset.
#define _GFX_GET_ALIGN(offset) \
	(offset == 0 ? UINT64_MAX : offset & (~offset + 1))

// Aligns offset up/down to the nearest multiple of align,
// which is assumed to be a power of two.
#define _GFX_ALIGN_UP(offset, align) \
	((offset + align - 1) & ~(align - 1))

#define _GFX_ALIGN_DOWN(offset, align) \
	(offset & ~(align - 1))


/****************************
 * Search tree key comparison function, key is of type uint64_t[2].
 * First element is the size, second is the offset.
 * Orders on size first, then strictest alignment (i.e. LSB of offset).
 */
static int _gfx_allocator_cmp(const void* l, const void* r)
{
	const uint64_t* _l = l;
	const uint64_t* _r = r;

	return
		(_l[0] < _r[0]) ? -1 : (_l[0] > _r[0]) ? 1 :
		(_GFX_GET_ALIGN(_l[1]) < _GFX_GET_ALIGN(_r[1])) ? -1 :
		(_GFX_GET_ALIGN(_l[1]) > _GFX_GET_ALIGN(_r[1])) ? 1 : 0;
}

/****************************
 * Find a memory type that includes all the required flags.
 * @param required Required memory property flags.
 * @param types    Supported (i.e. required) memory type bits to choose from.
 * @return UINT32_MAX on failure.
 */
static uint32_t _gfx_get_mem_type(_GFXAllocator* alloc,
                                  VkMemoryPropertyFlags required,
                                  uint32_t types)
{
	assert(alloc != NULL);
	assert(types != 0);

	VkPhysicalDeviceMemoryProperties* pdmp = &alloc->vk.properties;

	// We search in order, Vulkan orders subsets before supersets,
	// so we don't have to check for the least flags :)
	for (uint32_t t = 0; t < pdmp->memoryTypeCount; ++t)
	{
		// Not a supported type.
		if (!(((uint32_t)1 << t) & types))
			continue;

		// Does not include all required flags.
		if ((pdmp->memoryTypes[t].propertyFlags & required) != required)
			continue;

		return t;
	}

	return UINT32_MAX;
}

/****************************
 * Allocates and initializes a new Vulkan memory object.
 * @param minSize Use to force a minimum allocation (beyond default block sizes).
 * @return NULL on failure.
 */
static _GFXMemBlock* _gfx_alloc_mem_block(_GFXAllocator* alloc, uint32_t type,
                                          uint64_t minSize)
{
	assert(alloc != NULL);

	// Allocate and initialize.
	_GFXMemBlock* block = malloc(sizeof(_GFXMemBlock));
	if (block == NULL) return NULL;

	block->type = type;
	gfx_tree_init(&block->free, sizeof(uint64_t) * 2, _gfx_allocator_cmp);

	// TODO: Determine size, insert a single free node and allocate.

	// Link into the allocator's free block list.
	if (alloc->free != NULL)
		alloc->free->prev = block;

	block->next = alloc->free;
	block->prev = NULL;
	alloc->free = block;

	return block;
}

/****************************/
void _gfx_allocator_init(_GFXAllocator* alloc, _GFXDevice* device)
{
	assert(alloc != NULL);
	assert(device != NULL);
	assert(device->context != NULL);

	alloc->context = device->context;
	alloc->free = NULL;
	alloc->allocd = NULL;

	_groufix.vk.GetPhysicalDeviceMemoryProperties(
		device->vk.device,
		&alloc->vk.properties);
}

/****************************/
void _gfx_allocator_clear(_GFXAllocator* alloc)
{
	assert(alloc != NULL);

	// TODO: Implement.

	alloc->free = NULL;
	alloc->allocd = NULL;
}

/****************************/
int _gfx_allocator_alloc(_GFXAllocator* alloc, _GFXMemAlloc* mem,
                         VkMemoryRequirements reqs, VkMemoryPropertyFlags flags)
{
	assert(alloc != NULL);
	assert(mem != NULL);
	assert(reqs.size > 0);
	assert(_GFX_IS_POWER_OF_TWO(reqs.alignment));
	assert(reqs.memoryTypeBits != 0);
	assert(flags != 0);

	// Get memory type index.
	uint32_t type = _gfx_get_mem_type(alloc, flags, reqs.memoryTypeBits);
	if (type == UINT32_MAX)
	{
		gfx_log_error(
			"Could not find suitable Vulkan memory type for allocation of %llu bytes.",
			(unsigned long long)reqs.size);

		return 0;
	}

	// Construct a search key:
	// The key of the memory block will store two uint64_t's:
	// the first being the size, the second being the offset.
	// Alignment is computed from offset to compare, so we can insert alignment.
	// When found, we write back the actual offset to this key.
	uint64_t key[2] = {
		reqs.size,
		(reqs.alignment > 0) ? reqs.alignment : 1
	};

	// Find a free memory block with enough space.
	_GFXMemBlock* block;
	_GFXMemNode* node;

	for (block = alloc->free; block != NULL; block = block->next)
	{
		if (block->type != type)
			continue;

		// Search for free space.
		// If there are nodes with an exact size match, we need the least
		// strict alignment that is >= than the key's alignment,
		// so we search for a right match.
		// If no exact size match was found, the least strict alignment of
		// the next size class is returned, we need to linearly iterate
		// over all subsequent successor nodes until we find a match.
		for (
			node = gfx_tree_search(&block->free, key, GFX_TREE_MATCH_RIGHT);
			node != NULL;
			node = gfx_tree_succ(&block->free, node))
		{
			const uint64_t* fKey = gfx_tree_key(&block->free, node);

			// We align up because we can encounter less strict alignments.
			uint64_t offset = _GFX_ALIGN_UP(fKey[1], key[1]);
			uint64_t size = fKey[0] - (offset - fKey[1]);

			if (size >= key[0])
			{
				key[1] = offset; // Set the key's offset value.
				break;
			}
		}

		if (node != NULL)
			break;
	}

	// Allocate a new memory block.
	if (block == NULL)
	{
		block = _gfx_alloc_mem_block(alloc, type, key[0]);
		if (block == NULL)
		{
			gfx_log_error("Could not allocate a new Vulkan memory block.");
			return 0;
		}

		// There's 1 free node, the entire block, just pick it :)
		// We're at the beginning, so it always aligns, set offset of 0.
		node = block->free.root;
		key[1] = 0;
	}

	// Claim the memory.
	// i.e. modify the tree and output the allocation.
	// TODO: Implement.

	return 1;
}

/****************************/
void _gfx_allocator_free(_GFXAllocator* alloc, _GFXMemAlloc* mem)
{
	assert(alloc != NULL);
	assert(mem != NULL);

	// TODO: Implement.
}
