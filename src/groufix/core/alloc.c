/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/mem.h"
#include <assert.h>


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
 * @param flags    Output flags, all property flags associated with found index.
 * @param index    Output memory type index.
 * @return Non-zero on success.
 */
static int _gfx_get_mem_type(VkPhysicalDevice device,
                             VkMemoryPropertyFlags required, uint32_t types,
                             VkMemoryPropertyFlags* flags, uint32_t* index)
{
	assert(types != 0);
	assert(flags != NULL);
	assert(index != NULL);

	VkPhysicalDeviceMemoryProperties pdmp;
	_groufix.vk.GetPhysicalDeviceMemoryProperties(device, &pdmp);

	// We search in order, Vulkan orders subsets before supersets,
	// so we don't have to check for the least flags
	for (uint32_t t = 0; t < pdmp.memoryTypeCount; ++t)
	{
		// Not a supported type.
		if (!(((uint32_t)1 << t) & types))
			continue;

		// Does not include all required flags.
		if ((pdmp.memoryTypes[t].propertyFlags & required) != required)
			continue;

		*flags = pdmp.memoryTypes[t].propertyFlags;
		*index = t;

		return 1;
	}

	return 0;
}

/****************************/
void _gfx_allocator_init(_GFXAllocator* alloc, _GFXDevice* device)
{
	assert(alloc != NULL);
	assert(device != NULL);
	assert(device->context != NULL);

	alloc->device = device;
	alloc->context = device->context;
	alloc->free = NULL;
	alloc->allocd = NULL;
}

/****************************/
void _gfx_allocator_clear(_GFXAllocator* alloc)
{
	assert(alloc != NULL);

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

	_GFXDevice* device = alloc->device;

	// If larger than a memory block (?!?).
	// TODO: Allocate a dedicated memory block?
	if (reqs.size > _GFX_PREFERRED_MEM_BLOCK_SIZE)
	{
		gfx_log_error(
			"Cannot allocate a Vulkan memory object larger than %llu bytes.",
			_GFX_PREFERRED_MEM_BLOCK_SIZE);

		return 0;
	}

	// TODO: Find memory block before calling _gfx_get_mem_type,
	// so we don't needlessly call vkGetPhysicalDeviceMemoryProperties.

	// Get memory type index.
	uint32_t typeIndex;
	VkMemoryPropertyFlags typeFlags;

	if (!_gfx_get_mem_type(
		device->vk.device, flags, reqs.memoryTypeBits, &typeFlags, &typeIndex))
	{
		gfx_log_error(
			"Could not find suitable Vulkan memory type for allocation of %llu bytes.",
			(unsigned long long)reqs.size);

		return 0;
	}

	// Construct a search key:
	// The key of the memory block will store two uint64_t's.
	// The first being the size, the second being the offset.
	// However offset is never compared, instead alignment is, this allows us
	// to search based on alignment as well.
	uint64_t key[2] = {
		reqs.size,
		(reqs.alignment > 0) ? reqs.alignment : 1
	};

	// Find a free memory block with enough space.
	_GFXMemBlock* block;
	for (block = alloc->free; block != NULL; block = block->next)
	{
	}

	// Allocate a new Vulkan memory block.
	if (block == NULL)
	{
	}

	return 0;
}

/****************************/
void _gfx_allocator_free(_GFXAllocator* alloc, _GFXMemAlloc* mem)
{
	assert(alloc != NULL);
	assert(mem != NULL);
}
