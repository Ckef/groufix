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
#define _GFX_MAX_SMALL_HEAP_SIZE (2048ull * 1024 * 1024)

// Preferred memory block size of a 'large' heap (256 MiB).
#define _GFX_DEF_LARGE_HEAP_BLOCK_SIZE (256ull * 1024 * 2014)


// Get the size of a key (is an lvalue).
#define _GFX_KEY_SIZE(key) (key)[0]

// Get the offset of a key (is an lvalue).
#define _GFX_KEY_OFFSET(key) (key)[1]

// Get the strictest alignment (i.e. the least significant bit) of a key.
// Returns UINT64_MAX on no alignment, this so it always compares as stricter.
#define _GFX_KEY_ALIGN(key) \
	((key)[1] == 0 ? UINT64_MAX : (key)[1] & (~((key)[1]) + 1))


// Check whether a value is a power of two (0 counts).
#define _GFX_IS_POWER_OF_TWO(x) \
	(((x) & (x - 1)) == 0)

// Aligns offset up/down to the nearest multiple of align,
// which is assumed to be a power of two.
#define _GFX_ALIGN_UP(offset, align) \
	((offset + align - 1) & ~(align - 1))

#define _GFX_ALIGN_DOWN(offset, align) \
	((offset) & ~(align - 1))


/****************************
 * Search tree key comparison function, key is of type uint64_t[2].
 * First element is the size, second is the offset.
 * Orders on size first, then strictest alignment (i.e. LSB of offset).
 */
static int _gfx_allocator_cmp(const void* l, const void* r)
{
	const uint64_t* kL = l;
	const uint64_t* kR = r;

	return
		(_GFX_KEY_SIZE(kL) < _GFX_KEY_SIZE(kR)) ? -1 :
		(_GFX_KEY_SIZE(kL) > _GFX_KEY_SIZE(kR)) ?  1 :
		(_GFX_KEY_ALIGN(kL) < _GFX_KEY_ALIGN(kR)) ? -1 :
		(_GFX_KEY_ALIGN(kL) > _GFX_KEY_ALIGN(kR)) ?  1 : 0;
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
 * Find the best fitting memory type based on optimal and required flags.
 * @see _gfx_get_mem_type.
 */
static uint32_t _gfx_get_opt_mem_type(_GFXAllocator* alloc,
                                      VkMemoryPropertyFlags required,
                                      VkMemoryPropertyFlags optimal,
                                      uint32_t types)
{
	uint32_t type =
		_gfx_get_mem_type(alloc, optimal, types);

	return (type != UINT32_MAX) ? type :
		_gfx_get_mem_type(alloc, required, types);
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
	assert(sizeof(uint64_t) >= sizeof(VkDeviceSize)); // Needs to be true...

	_GFXContext* context = alloc->context;
	VkPhysicalDeviceMemoryProperties* pdmp = &alloc->vk.properties;

	// Validate that we have enough memory.
	VkDeviceSize heapSize =
		pdmp->memoryHeaps[pdmp->memoryTypes[type].heapIndex].size;

	if (minSize > heapSize)
	{
		gfx_log_error(
			"Memory heap of %llu bytes is too small to allocate %llu bytes from.",
			(unsigned long long)heapSize,
			(unsigned long long)minSize);

		return NULL;
	}

	// Calculate block size in Vulkan units.
	// If it is a 'small' heap, we allocate the heap's size divided by 8.
	// Lastly, if minimum requested size is greater than half the block size,
	// we instead just allocate a dedicated block for it.
	VkDeviceSize prefBlockSize =
		(heapSize <= _GFX_MAX_SMALL_HEAP_SIZE) ?
		heapSize / 8 :
		_GFX_DEF_LARGE_HEAP_BLOCK_SIZE;

	VkDeviceSize blockSize =
		(prefBlockSize / 2 < minSize) ? minSize : prefBlockSize;

	// Allocate handle & actual Vulkan memory object.
	// If the allocation failed, we try again at 1/2, 1/4 and 1/8 of the size.
	// During the process we keep track of the actual allocated size.
	_GFXMemBlock* block = malloc(sizeof(_GFXMemBlock));
	if (block == NULL) goto error;

	for (unsigned int i = 0; 1; ++i) // Note: condition is always true!
	{
		// TODO: Support Vulkan dedicated allocation?
		// TODO: Check against Vulkan's allocation limit.

		VkMemoryAllocateInfo mai = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,

			.pNext           = NULL,
			.allocationSize  = blockSize,
			.memoryTypeIndex = type
		};

		VkResult result = context->vk.AllocateMemory(
			context->vk.device, &mai, NULL, &block->vk.memory);

		if (result != VK_ERROR_OUT_OF_DEVICE_MEMORY)
		{
			// Success?
			_GFX_VK_CHECK(result, goto clean);
			break;
		}

		// Try smaller size class.
		// Smallest size class we go is 1/8 (i.e. we divided by two 3 times).
		// Can't go smaller than the minimum requested either..
		if (i >= 3 || blockSize <= minSize)
		{
			_GFX_VK_CHECK(result, {});
			goto clean;
		}

		blockSize /= 2;
		blockSize = (blockSize < minSize) ? minSize : blockSize;
	}

	// At this point we have memory!
	// Initialize and insert a single free memory node.
	uint64_t key[2] = { blockSize, 0 };
	block->type = type;
	block->size = blockSize;
	gfx_tree_init(&block->nodes.free, sizeof(key), _gfx_allocator_cmp);

	_GFXMemNode* node = gfx_tree_insert(
		&block->nodes.free, sizeof(_GFXMemNode), NULL, key);

	if (node == NULL)
	{
		// Ah well..
		gfx_tree_clear(&block->nodes.free);
		context->vk.FreeMemory(context->vk.device, block->vk.memory, NULL);

		goto clean;
	}

	node->free = 1;
	gfx_list_init(&block->nodes.list);
	gfx_list_insert_after(&block->nodes.list, &node->list, NULL);

	// Woop woop.
	gfx_log_debug(
		"New Vulkan memory object allocated:\n"
		"    Memory block size: %llu bytes.\n"
		"    Preferred block size: %llu bytes.\n",
		(unsigned long long)blockSize,
		(unsigned long long)prefBlockSize);

	// Finally link the block into the allocator.
	gfx_list_insert_after(&alloc->free, &block->list, NULL);

	return block;


	// Cleanup on failure.
clean:
	free(block);
error:
	gfx_log_error(
		"Could not allocate a new Vulkan memory object of %llu bytes.",
		(unsigned long long)blockSize);

	return NULL;
}

/****************************
 * Frees a memory block, also freeing the Vulkan memory object.
 */
static void _gfx_free_mem_block(_GFXAllocator* alloc, _GFXMemBlock* block)
{
	assert(alloc != NULL);
	assert(block != NULL);

	_GFXContext* context = alloc->context;

	// Unlink from the allocator.
	gfx_list_erase(
		(block->nodes.free.root == NULL) ? &alloc->allocd : &alloc->free,
		&block->list);

	gfx_list_clear(&block->nodes.list);
	gfx_tree_clear(&block->nodes.free);
	context->vk.FreeMemory(context->vk.device, block->vk.memory, NULL);

	gfx_log_debug(
		"Freed Vulkan memory object of %llu bytes.",
		(unsigned long long)block->size);

	free(block);
}

/****************************/
void _gfx_allocator_init(_GFXAllocator* alloc, _GFXDevice* device)
{
	assert(alloc != NULL);
	assert(device != NULL);
	assert(device->context != NULL);

	alloc->context = device->context;
	gfx_list_init(&alloc->free);
	gfx_list_init(&alloc->allocd);

	_groufix.vk.GetPhysicalDeviceMemoryProperties(
		device->vk.device,
		&alloc->vk.properties);
}

/****************************/
void _gfx_allocator_clear(_GFXAllocator* alloc)
{
	assert(alloc != NULL);

	// Free all memory.
	while (alloc->free.head != NULL)
		_gfx_free_mem_block(alloc, (_GFXMemBlock*)alloc->free.head);

	while (alloc->allocd.head != NULL)
		_gfx_free_mem_block(alloc, (_GFXMemBlock*)alloc->allocd.head);

	// Kind of a no-op, but for consistency.
	gfx_list_clear(&alloc->free);
	gfx_list_clear(&alloc->allocd);
}

/****************************/
int _gfx_alloc(_GFXAllocator* alloc, _GFXMemAlloc* mem,
               VkMemoryPropertyFlags required, VkMemoryPropertyFlags optimal,
               VkMemoryRequirements reqs)
{
	assert(alloc != NULL);
	assert(mem != NULL);
	assert(reqs.size > 0);
	assert(_GFX_IS_POWER_OF_TWO(reqs.alignment));
	assert(reqs.memoryTypeBits != 0);

	// Alignment of 0 means 1.
	reqs.alignment = (reqs.alignment > 0) ? reqs.alignment : 1;

	// Get memory type index.
	uint32_t type =
		_gfx_get_opt_mem_type(alloc, required, optimal, reqs.memoryTypeBits);

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
	// When found, we override this alignment with the resulting offset.
	uint64_t key[2] = { reqs.size, reqs.alignment };

	// Find a free memory block with enough space.
	_GFXMemBlock* block;
	_GFXMemNode* node = NULL;

	for (
		block = (_GFXMemBlock*)alloc->free.head;
		block != NULL;
		block = (_GFXMemBlock*)block->list.next)
	{
		if (block->type != type)
			continue;

		// Search for free space.
		// If there are nodes with an exact size match, we need the least
		// strict alignment that is >= than the key's alignment,
		// so we search for a right match.
		// If no exact size match was found, the least strict alignment of
		// the next size class is returned, we need to linearly iterate
		// over all successor nodes until we find a match.
		for (
			node = gfx_tree_search(&block->nodes.free, key, GFX_TREE_MATCH_RIGHT);
			node != NULL;
			node = gfx_tree_succ(&block->nodes.free, node))
		{
			const uint64_t* fKey = gfx_tree_key(&block->nodes.free, node);

			// We align up because we can encounter less strict alignments.
			// Note: do not use the _GFX_KEY_ALIGN macro, we need 0 here!
			uint64_t offset =
				_GFX_ALIGN_UP(_GFX_KEY_OFFSET(fKey), _GFX_KEY_OFFSET(key));
			uint64_t size =
				_GFX_KEY_SIZE(fKey) - (offset - _GFX_KEY_OFFSET(fKey));

			if (size >= _GFX_KEY_SIZE(key))
			{
				_GFX_KEY_OFFSET(key) = offset; // Set the key's offset value.
				break;
			}
		}

		if (node != NULL)
			break;
	}

	// Allocate a new memory block.
	if (block == NULL)
	{
		block = _gfx_alloc_mem_block(alloc, type, _GFX_KEY_SIZE(key));
		if (block == NULL) return 0;

		// There's 1 free node, the entire block, just pick it :)
		// We're at the beginning, so it always aligns, set offset of 0.
		node = block->nodes.free.root;
		_GFX_KEY_OFFSET(key) = 0;
	}

	// Claim the memory.
	// i.e. output the allocation data.
	gfx_list_insert_before(&block->nodes.list, &mem->node.list, &node->list);
	mem->node.free = 0;
	mem->block = block;
	mem->size = _GFX_KEY_SIZE(key);
	mem->offset = _GFX_KEY_OFFSET(key);
	mem->vk.memory = block->vk.memory;

	// Now fix the free tree and link the allocation in it...
	// So we aligned the claimed memory, this means there could be some waste
	// to the left of it, however we just ignore it and consider it unusable.
	// However to the right of the memory we might still have a big free block.
	const uint64_t* cKey = gfx_tree_key(&block->nodes.free, node);

	uint64_t rOffset =
		_GFX_KEY_OFFSET(key) + _GFX_KEY_SIZE(key);
	uint64_t rSize =
		_GFX_KEY_SIZE(cKey) - (rOffset - _GFX_KEY_OFFSET(cKey));

	// The waste we created to the left is at most (alignment - 1) in size.
	// Similarly, if memory to the right is smaller than alignment, we skip it as well.
	// Bit of an arbitrary heuristic, but hey we don't like small nodes :)
	if (rSize < reqs.alignment)
	{
		// Not preserving any memory, erase claimed node.
		gfx_list_erase(&block->nodes.list, &node->list);
		gfx_tree_erase(&block->nodes.free, node);

		// Move block to allocd list if fully allocated now.
		if (block->nodes.free.root == NULL)
		{
			gfx_list_erase(&alloc->free, &block->list);
			gfx_list_insert_after(&alloc->allocd, &block->list, NULL);
		}
	}
	else
	{
		// We want to preserve memory to the right,
		// so just update the node's key in the free tree.
		uint64_t rKey[2] = { rSize, rOffset };
		gfx_tree_update(&block->nodes.free, node, rKey);
	}

	return 1;
}

/****************************/
void _gfx_free(_GFXAllocator* alloc, _GFXMemAlloc* mem)
{
	assert(alloc != NULL);
	assert(mem != NULL);

	_GFXMemBlock* block = mem->block;
	_GFXMemNode* left = (_GFXMemNode*)mem->node.list.prev;
	_GFXMemNode* right = (_GFXMemNode*)mem->node.list.next;

	// First the weird case that this allocation is the only memory node.
	// Just free the memory block.
	if (left == NULL && right == NULL)
	{
		_gfx_free_mem_block(alloc, block);
		return;
	}

	// Now we regard the allocation as free and coalesce it with its neighbours.
	// We may have created some wasted space during allocation, make sure to
	// reclaim those areas as free space as well.
	// This is why we first calculate the range of space we can reclaim.
	uint64_t lBound =
		(left == NULL) ? 0 :
		(left->free) ?
			_GFX_KEY_OFFSET((const uint64_t*)gfx_tree_key(&block->nodes.free, left)) :
			((_GFXMemAlloc*)left)->offset +
			((_GFXMemAlloc*)left)->size;

	uint64_t rBound =
		(right == NULL) ? block->size :
		(right->free) ?
			_GFX_KEY_OFFSET((const uint64_t*)gfx_tree_key(&block->nodes.free, right)) +
			_GFX_KEY_SIZE((const uint64_t*)gfx_tree_key(&block->nodes.free, right)) :
			((_GFXMemAlloc*)right)->offset;

	// Now modify the list and free tree to reflect the claimed space.
	uint64_t key[2] = { rBound - lBound, lBound };
	int lFree = (left != NULL) && left->free;
	int rFree = (right != NULL) && right->free;

	if (lFree || rFree)
	{
		// If a free neighbour exists, just unlink the allocation.
		// We are going to expand one of its neighbours.
		gfx_list_erase(&block->nodes.list, &mem->node.list);

		// If both are free, erase the right one.
		if (lFree && rFree)
		{
			gfx_list_erase(&block->nodes.list, &right->list);
			gfx_tree_erase(&block->nodes.free, right);
		}

		// If more than one node remains in the list,
		// expand a neighbour so it covers the new free space.
		// If only one remains, just free the entire memory block.
		if (block->nodes.list.head != block->nodes.list.tail)
			gfx_tree_update(&block->nodes.free, lFree ? left : right, key);
		else
			_gfx_free_mem_block(alloc, block);
	}
	else
	{
		int full = (block->nodes.free.root == NULL);

		// We know no free neighbour exists AND at least one neighbour exists,
		// if no neighbour were to exist at all we exit early at the top.
		// So just insert a new free node.
		_GFXMemNode* node = gfx_tree_insert(
			&block->nodes.free, sizeof(_GFXMemNode), NULL, key);

		if (node == NULL)
		{
			// Ah well crud..
			gfx_log_warn(
				"Could not insert a new free node whilst freeing an allocation "
				"from a Vulkan memory object, potentially lost %llu bytes.",
				(unsigned long long)_GFX_KEY_SIZE(key));
		}
		else
		{
			// Yey we have a node, link it in..
			node->free = 1;
			gfx_list_insert_after(&block->nodes.list, &node->list, &mem->node.list);

			if (full)
			{
				// If the block was full, move it to the free list now :)
				// Make sure we append it to the list to avoid swapping the
				// same block over and over again.
				gfx_list_erase(&alloc->allocd, &block->list);
				gfx_list_insert_after(&alloc->free, &block->list, NULL);
			}
		}

		// Unlink the allocation from the list.
		// Do this on failure of node insertion as well, as the allocation
		// must always be invalidated as a result of calling this function.
		gfx_list_erase(&block->nodes.list, &mem->node.list);
	}
}
