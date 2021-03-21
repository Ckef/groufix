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


// Check whether a value is a power of two (0 counts).
#define _GFX_IS_POWER_OF_TWO(x) \
	(((x) & (x - 1)) == 0)

// Get the strictest alignment (i.e. the least significant bit) of an offset.
#define _GFX_GET_ALIGN(offset) \
	(offset == 0 ? UINT64_MAX : (offset) & (~(offset) + 1))

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
		(kL[0] < kR[0]) ? -1 : (kL[0] > kR[0]) ? 1 :
		(_GFX_GET_ALIGN(kL[1]) < _GFX_GET_ALIGN(kR[1])) ? -1 :
		(_GFX_GET_ALIGN(kL[1]) > _GFX_GET_ALIGN(kR[1])) ? 1 : 0;
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
 * Unlinks and relinks a memory block back into the allocator, possible cases:
 * - The block contains no Vulkan memory object, only unlink.
 * - The block contains no free memory nodes, stick it in the allocated block list.
 * - The block does contain free nodes, stick it in the free block list.
 *
 * A side-effect is that this will always put the block at the front of the list,
 * making it the first to be searched when allocating memory.
 */
static void _gfx_relink_mem_block(_GFXAllocator* alloc, _GFXMemBlock* block)
{
	assert(alloc != NULL);
	assert(block != NULL);

	// Unlink from the allocator.
	if (alloc->free == block)
		alloc->free = block->next;
	if (alloc->allocd == block)
		alloc->allocd = block->next;

	if (block->next != NULL)
		block->next->prev = block->prev;
	if (block->prev != NULL)
		block->prev->next = block->next;

	// Relink into the allocator if we still have memory.
	if (block->vk.memory != VK_NULL_HANDLE)
	{
		_GFXMemBlock** list =
			(block->free.root == NULL) ? &alloc->allocd : &alloc->free;

		if (*list != NULL)
			(*list)->prev = block;

		block->next = *list;
		block->prev = NULL;
		*list = block;
	}
}

/****************************
 * Frees a memory block, also freeing the Vulkan memory object.
 */
static void _gfx_free_mem_block(_GFXAllocator* alloc, _GFXMemBlock* block)
{
	assert(alloc != NULL);
	assert(block != NULL);

	_GFXContext* context = alloc->context;

	gfx_tree_clear(&block->free);
	context->vk.FreeMemory(context->vk.device, block->vk.memory, NULL);

	// Unlink from the allocator.
	block->vk.memory = VK_NULL_HANDLE; // Necessary to not relink.
	_gfx_relink_mem_block(alloc, block);

	free(block);
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
	_GFXMemNode data = { .left = NULL, .right = NULL, .free = 1 };

	block->type = type;
	block->size = blockSize;
	gfx_tree_init(&block->free, sizeof(key), _gfx_allocator_cmp);

	if (gfx_tree_insert(&block->free, sizeof(_GFXMemNode), &data, key) == NULL)
		goto clean_block;

	// Finally link the block into the allocator.
	block->next = NULL;
	block->prev = NULL;
	_gfx_relink_mem_block(alloc, block);

	gfx_log_debug(
		"New Vulkan memory object allocated:\n"
		"    Memory block size: %llu bytes.\n"
		"    Preferred block size: %llu bytes.\n",
		(unsigned long long)blockSize,
		(unsigned long long)prefBlockSize);

	return block;


	// Cleanup on failure.
clean_block:
	gfx_tree_clear(&block->free);
	context->vk.FreeMemory(context->vk.device, block->vk.memory, NULL);
clean:
	free(block);

error:
	gfx_log_error(
		"Could not allocate a new Vulkan memory object of %llu bytes.",
		(unsigned long long)blockSize);

	return NULL;
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

	// Free all memory.
	while (alloc->free != NULL)
		_gfx_free_mem_block(alloc, alloc->free);

	while (alloc->allocd != NULL)
		_gfx_free_mem_block(alloc, alloc->allocd);
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

	// Alignment of 0 means 1.
	reqs.alignment = (reqs.alignment > 0) ? reqs.alignment : 1;

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
	// When found, we override this alignment with the resulting offset.
	uint64_t key[2] = { reqs.size, reqs.alignment };

	// Find a free memory block with enough space.
	_GFXMemBlock* block;
	_GFXMemNode* node = NULL;

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
		// over all successor nodes until we find a match.
		for (
			node = gfx_tree_search(&block->free, key, GFX_TREE_MATCH_RIGHT);
			node != NULL;
			node = gfx_tree_succ(&block->free, node))
		{
			// fKey[0] is size.
			// fKey[1] is offset.
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
		if (block == NULL) return 0;

		// There's 1 free node, the entire block, just pick it :)
		// We're at the beginning, so it always aligns, set offset of 0.
		node = block->free.root;
		key[1] = 0;
	}

	// Claim the memory.
	// i.e. output the allocation data.
	mem->node.free = 0;
	mem->block = block;
	mem->size = key[0];
	mem->offset = key[1];
	mem->vk.memory = block->vk.memory;

	// Now fix the free tree and link the allocation in it...
	// So we aligned the claimed memory, this means there could be some waste
	// to the left of it, however we just ignore it and consider it unusable.
	// However to the right of the memory we might still have a big free block.
	const uint64_t* nKey = gfx_tree_key(&block->free, node);
	uint64_t rOffset = key[1] + key[0];
	uint64_t rSize = nKey[0] - (rOffset - nKey[1]);

	// The waste we created to the left is at most (alignment - 1) in size.
	// Similarly, if memory to the right is smaller than alignment, we skip it as well.
	// Bit of an arbitrary heuristic, but hey we don't like small nodes :)
	if (rSize < reqs.alignment)
	{
		// Replace node with the allocation.
		mem->node.left = node->left;
		mem->node.right = node->right;

		if (node->left != NULL)
			node->left->right = &mem->node;
		if (node->right != NULL)
			node->right->left = &mem->node;

		// Truly erase :)
		gfx_tree_erase(&block->free, node);

		// Relink the block when it is now fully allocated.
		if (block->free.root == NULL)
			_gfx_relink_mem_block(alloc, block);
	}
	else
	{
		// We want to preserve memory to the right,
		// so just insert the allocation between the nodes.
		mem->node.left = node->left;
		mem->node.right = node;

		if (node->left != NULL)
			node->left->right = &mem->node;

		node->left = &mem->node;

		// And update the node we're allocating from to its new size/offset.
		uint64_t rKey[2] = { rSize, rOffset };
		gfx_tree_update(&block->free, node, rKey);
	}

	return 1;
}

/****************************/
void _gfx_allocator_free(_GFXAllocator* alloc, _GFXMemAlloc* mem)
{
	assert(alloc != NULL);
	assert(mem != NULL);

	// First the weird case that this allocation is the only memory node.
	// Just free the memory block.
	if (mem->node.left == NULL && mem->node.right == NULL)
		_gfx_free_mem_block(alloc, mem->block);

	// TODO: Implement the rest.
}
