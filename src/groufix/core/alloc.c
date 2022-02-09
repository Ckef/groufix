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


// Maximum size for a heap to be considered 'small' (1 GiB).
// If a heap is 'small', blocks will be the size of the heap divided by 8.
#define _GFX_MAX_SMALL_HEAP_SIZE (1024ull * 1024 * 1024)

// Preferred memory block size of a 'large' heap (256 MiB).
#define _GFX_DEF_LARGE_HEAP_BLOCK_SIZE (256ull * 1024 * 1024)


// Get the size and offset of a key (as an lvalue) +
// Get the strictest alignment (i.e. the least significant bit) of a key,
// returns all 1's on no alignment, this so it always compares as stricter.
#define _GFX_KEY_SIZE(key) \
	((VkDeviceSize*)(key))[0]

#define _GFX_KEY_OFFSET(key) \
	((VkDeviceSize*)(key))[1]

#define _GFX_KEY_ALIGN(key) \
	(_GFX_KEY_OFFSET(key) == 0 ? ~((VkDeviceSize)0) : \
	_GFX_KEY_OFFSET(key) & (~_GFX_KEY_OFFSET(key) + 1))


// Get Vulkan memory property flag bits as a readable string.
#define _GFX_GET_VK_TYPE_FLAG_STRING(alloc, type, flag) \
	((flag & (alloc)->vk.properties.memoryTypes[type].propertyFlags) ? \
		"        "#flag"\n" : "")

#define _GFX_GET_VK_TYPE_EMPTY_STRING(alloc, type) \
	((alloc)->vk.properties.memoryTypes[type].propertyFlags == 0 ? \
		"None." : "")

#define _GFX_GET_VK_TYPE_DEVICE_LOCAL_STRING(alloc, type) \
	_GFX_GET_VK_TYPE_FLAG_STRING(alloc, type, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)

#define _GFX_GET_VK_TYPE_HOST_VISIBLE_STRING(alloc, type) \
	_GFX_GET_VK_TYPE_FLAG_STRING(alloc, type, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)

#define _GFX_GET_VK_TYPE_HOST_COHERENT_STRING(alloc, type) \
	_GFX_GET_VK_TYPE_FLAG_STRING(alloc, type, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)

#define _GFX_GET_VK_TYPE_HOST_CACHED_STRING(alloc, type) \
	_GFX_GET_VK_TYPE_FLAG_STRING(alloc, type, VK_MEMORY_PROPERTY_HOST_CACHED_BIT)

#define _GFX_GET_VK_TYPE_LAZILY_ALLOCATED_STRING(alloc, type) \
	_GFX_GET_VK_TYPE_FLAG_STRING(alloc, type, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)

#define _GFX_GET_VK_TYPE_PROTECTED_STRING(alloc, type) \
	_GFX_GET_VK_TYPE_FLAG_STRING(alloc, type, VK_MEMORY_PROPERTY_PROTECTED_BIT)


// Gets suitable memory types (auto log when none found) assigned to two lvalue.
#define _GFX_GET_MEM_TYPES(lreq, lopt, alloc, required, optimal, types, action) \
	do { \
		lreq = _gfx_get_mem_type(alloc, required, types); \
		lopt = _gfx_get_mem_type(alloc, optimal, types); \
		if (lreq == UINT32_MAX && lopt == UINT32_MAX) { \
			gfx_log_error( \
				"Could not find suitable Vulkan memory type for allocation."); \
			action; \
		} \
	} while (0)


/****************************
 * Search tree key comparison function, l and r are of type VkDeviceSize[2].
 * First element is the size, second is the offset.
 * Orders on size first, then strictest alignment (i.e. LSB of offset).
 */
static int _gfx_allocator_cmp(const void* l, const void* r)
{
	const VkDeviceSize* kL = l;
	const VkDeviceSize* kR = r;

	return
		(_GFX_KEY_SIZE(kL) < _GFX_KEY_SIZE(kR)) ? -1 :
		(_GFX_KEY_SIZE(kL) > _GFX_KEY_SIZE(kR)) ?  1 :
		(_GFX_KEY_ALIGN(kL) < _GFX_KEY_ALIGN(kR)) ? -1 :
		(_GFX_KEY_ALIGN(kL) > _GFX_KEY_ALIGN(kR)) ?  1 : 0;
}

/****************************
 * Find a memory type that includes all the given memory property flags.
 * @param types Supported (i.e. required) memory type bits to choose from.
 * @return UINT32_MAX if none found.
 */
static uint32_t _gfx_get_mem_type(_GFXAllocator* alloc,
                                  VkMemoryPropertyFlags flags,
                                  uint32_t types)
{
	assert(alloc != NULL);
	assert(types != 0);

	// We search in order, Vulkan orders subsets before supersets,
	// so we don't have to check for the least flags :)
	const VkPhysicalDeviceMemoryProperties* pdmp = &alloc->vk.properties;

	for (uint32_t t = 0; t < pdmp->memoryTypeCount; ++t)
	{
		// Not a supported type.
		if (!(((uint32_t)1 << t) & types))
			continue;

		// Does not include all required flags.
		if ((pdmp->memoryTypes[t].propertyFlags & flags) != flags)
			continue;

		return t;
	}

	return UINT32_MAX;
}

/****************************
 * Allocates and initializes a new Vulkan memory 'block' to be subdivided.
 * @param minSize Use to force a minimum allocation (beyond default block sizes).
 * @param maxSize Use to force a maximum allocation.
 * @return NULL on failure.
 *
 * If the resulting size of the allocated block is equal to minSize,
 * the free root node _WILL NOT_ be inserted. To force this behaviour, i.e.
 * allocate a block of an exact size, set minSize == maxSize.
 *
 * To allocate Vulkan 'dedicated' memory, a buffer _OR_ image can be passed,
 * these will be passed to Vulkan if and only if minSize == maxSIze.
 */
static _GFXMemBlock* _gfx_alloc_mem_block(_GFXAllocator* alloc, uint32_t type,
                                          VkDeviceSize minSize, VkDeviceSize maxSize,
                                          VkBuffer buffer, VkImage image)
{
	assert(alloc != NULL);
	assert(minSize <= maxSize);
	assert(buffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE);

	_GFXContext* context = alloc->context;

	// Firstly, check against Vulkan's allocation limit & take the lock.
	// We have to lock such that two concurrent allocations both fail properly
	// if the limit only allows one more allocation.
	_gfx_mutex_lock(&context->allocLock);

	if (atomic_load(&context->allocs) >= context->maxAllocs)
	{
		gfx_log_error(
			"Cannot allocate %"PRIu64" bytes because physical device limit "
			"of %"PRIu32" memory allocations has been reached.",
			minSize, context->maxAllocs);

		_gfx_mutex_unlock(&context->allocLock);
		return NULL;
	}

	// Increase the count & unlock.
	// On failure of allocation, we just decrease the count back down.
	// This might prevent other allocations when near the limit,
	// but honestly at that point something else is horribly wrong...
	atomic_fetch_add(&context->allocs, 1);

	_gfx_mutex_unlock(&context->allocLock);

	// Validate that we have enough memory.
	const VkPhysicalDeviceMemoryProperties* pdmp =
		&alloc->vk.properties;
	const VkDeviceSize heapSize =
		pdmp->memoryHeaps[pdmp->memoryTypes[type].heapIndex].size;

	if (minSize > heapSize)
	{
		gfx_log_error(
			"Memory heap of %"PRIu64" bytes is too small to allocate "
			"%"PRIu64" bytes from.",
			heapSize, minSize);

		goto fail_decrease;
	}

	// Calculate block size in Vulkan units.
	// If it is a 'small' heap, we allocate the heap's size divided by 8.
	const VkDeviceSize prefBlockSize =
		(heapSize <= _GFX_MAX_SMALL_HEAP_SIZE) ?
		heapSize / 8 :
		_GFX_DEF_LARGE_HEAP_BLOCK_SIZE;

	VkDeviceSize blockSize =
		GFX_CLAMP(prefBlockSize, minSize, maxSize);

	// Check whether we want to allocate Vulkan dedicated memory.
	const int dedicated =
		(minSize == maxSize) &&
		(buffer != VK_NULL_HANDLE || image != VK_NULL_HANDLE);

	VkMemoryDedicatedAllocateInfo mdai = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
		.pNext = NULL,
		.image = image,
		.buffer = buffer
	};

	// Allocate block & init.
	_GFXMemBlock* block = malloc(sizeof(_GFXMemBlock));
	if (block == NULL)
		goto clean;

	if (!_gfx_mutex_init(&block->map.lock))
		goto clean;

	// Allocate the actual Vulkan memory object.
	// If the allocation failed, we try again at 1/2, 1/4 and 1/8 of the size.
	// During the process we keep track of the actual allocated size.
	for (unsigned int i = 0; 1; ++i) // Note: condition is always true!
	{
		VkMemoryAllocateInfo mai = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,

			.pNext           = dedicated ? &mdai : NULL,
			.allocationSize  = blockSize,
			.memoryTypeIndex = type
		};

		VkResult result = context->vk.AllocateMemory(
			context->vk.device, &mai, NULL, &block->vk.memory);

		if (result != VK_ERROR_OUT_OF_DEVICE_MEMORY)
		{
			// Success?
			_GFX_VK_CHECK(result, goto clean_lock);
			break;
		}

		// Try smaller size class.
		// Smallest size class we go is 1/8 (i.e. we divided by two 3 times).
		// Can't go smaller than the minimum requested either..
		if (i >= 3 || blockSize <= minSize)
		{
			_GFX_VK_CHECK(result, {});
			goto clean_lock;
		}

		blockSize /= 2;
		blockSize = GFX_MAX(minSize, blockSize);
	}

	// At this point we have memory!
	// Initialize the block and the list of nodes & free tree.
	const VkDeviceSize key[2] = { blockSize, 0 };
	block->type = type;
	block->size = blockSize;

	block->map.refs = 0;
	block->map.ptr = NULL;

	gfx_list_init(&block->nodes.list);
	gfx_tree_init(&block->nodes.free, sizeof(key),
		// Take the largest alignment of the key and element types.
		GFX_MAX(_Alignof(VkDeviceSize[2]), _Alignof(_GFXMemNode)),
		_gfx_allocator_cmp);

	// If an exact size, link the block into the allocd list.
	// As there is no free root node, it will be regarded as full.
	if (blockSize == minSize)
		gfx_list_insert_after(&alloc->allocd, &block->list, NULL);
	else
	{
		// If not an exact size however (!), insert a free root node.
		_GFXMemNode* node = gfx_tree_insert(
			&block->nodes.free, sizeof(_GFXMemNode), NULL, key);

		// Ah well..
		if (node == NULL)
			goto clean_memory;

		node->free = 1;
		gfx_list_insert_after(&block->nodes.list, &node->list, NULL);

		// And link the block in the free list instead.
		gfx_list_insert_after(&alloc->free, &block->list, NULL);
	}

	// Woop woop.
	gfx_log_debug(
		"New Vulkan memory object allocated:\n"
		"    Memory block size: %"PRIu64" bytes%s.\n"
		"    Prefer block size: %"PRIu64" bytes.\n"
		"    Memory heap size: %"PRIu64" bytes.\n"
		"    Memory heap flags: %s\n%s%s%s%s%s%s",
		blockSize,
		dedicated ? " (dedicated)" : "",
		prefBlockSize,
		heapSize,
		_GFX_GET_VK_TYPE_EMPTY_STRING(alloc, type),
		_GFX_GET_VK_TYPE_DEVICE_LOCAL_STRING(alloc, type),
		_GFX_GET_VK_TYPE_HOST_VISIBLE_STRING(alloc, type),
		_GFX_GET_VK_TYPE_HOST_COHERENT_STRING(alloc, type),
		_GFX_GET_VK_TYPE_HOST_CACHED_STRING(alloc, type),
		_GFX_GET_VK_TYPE_LAZILY_ALLOCATED_STRING(alloc, type),
		_GFX_GET_VK_TYPE_PROTECTED_STRING(alloc, type));

	return block;


	// Cleanup on failure.
clean_memory:
	gfx_list_clear(&block->nodes.list);
	gfx_tree_clear(&block->nodes.free);
	context->vk.FreeMemory(context->vk.device, block->vk.memory, NULL);
clean_lock:
	_gfx_mutex_clear(&block->map.lock);
clean:
	gfx_log_error(
		"Could not allocate a new %sVulkan memory object of %"PRIu64" bytes.",
		dedicated ? "(dedicated) " : "",
		blockSize);

	free(block);

	// Decrease allocation count on failure.
fail_decrease:
	atomic_fetch_sub(&context->allocs, 1);

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

	// Free the Vulkan memory and decrease the allocation count afterwards.
	context->vk.FreeMemory(context->vk.device, block->vk.memory, NULL);
	atomic_fetch_sub(&context->allocs, 1);

	// Unlink from the allocator and free all remaining block things.
	gfx_list_erase(
		(block->nodes.free.root == NULL) ? &alloc->allocd : &alloc->free,
		&block->list);

	gfx_list_clear(&block->nodes.list);
	gfx_tree_clear(&block->nodes.free);
	_gfx_mutex_clear(&block->map.lock);

	gfx_log_debug(
		"Freed Vulkan memory object:\n"
		"    Memory block size: %"PRIu64" bytes.\n"
		"    Memory heap flags: %s\n%s%s%s%s%s%s",
		block->size,
		_GFX_GET_VK_TYPE_EMPTY_STRING(alloc, block->type),
		_GFX_GET_VK_TYPE_DEVICE_LOCAL_STRING(alloc, block->type),
		_GFX_GET_VK_TYPE_HOST_VISIBLE_STRING(alloc, block->type),
		_GFX_GET_VK_TYPE_HOST_COHERENT_STRING(alloc, block->type),
		_GFX_GET_VK_TYPE_HOST_CACHED_STRING(alloc, block->type),
		_GFX_GET_VK_TYPE_LAZILY_ALLOCATED_STRING(alloc, block->type),
		_GFX_GET_VK_TYPE_PROTECTED_STRING(alloc, block->type));

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

	VkPhysicalDeviceProperties pdp;
	_groufix.vk.GetPhysicalDeviceProperties(device->vk.device, &pdp);

	alloc->granularity = pdp.limits.bufferImageGranularity;

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
int _gfx_alloc(_GFXAllocator* alloc, _GFXMemAlloc* mem, int linear,
               VkMemoryPropertyFlags required, VkMemoryPropertyFlags optimal,
               VkMemoryRequirements reqs)
{
	assert(alloc != NULL);
	assert(mem != NULL);
	assert(reqs.size > 0);
	assert(GFX_IS_POWER_OF_TWO(reqs.alignment));
	assert(reqs.memoryTypeBits != 0);

	// Normalize linear to 0 or 1 for easy calculation.
	linear = linear ? 1 : 0;

	// Alignment of 0 means 1.
	reqs.alignment = (reqs.alignment > 0) ? reqs.alignment : 1;

	// Get memory type index.
	uint32_t tReq, tOpt;
	_GFX_GET_MEM_TYPES(
		tReq, tOpt, alloc, required, optimal, reqs.memoryTypeBits,
		return 0);

	// Construct a search key:
	// The key of the memory block will store two uint64_t's:
	// the first being the size, the second being the offset.
	// Alignment is computed from offset to compare, so we can insert alignment.
	// When found, we override this alignment with the resulting offset,
	// at that point we will use the key's values for consistency.
	VkDeviceSize key[2] = { reqs.size, reqs.alignment };

	// Find a free memory block with enough space.
	// Start with a defined memory type.
	// Note that if neither types are defined we already returned.
	uint32_t type = (tOpt == UINT32_MAX) ? tReq : tOpt;
	_GFXMemBlock* block;
	_GFXMemNode* node = NULL;

	// Goto here to try with another type :)
try_search:

	for (
		block = (_GFXMemBlock*)alloc->free.head;
		block != NULL;
		block = (_GFXMemBlock*)block->list.next)
	{
		if (block->type != type)
			continue;

		// Search for free space.
		// If there are no nodes with an exact size match, we need the least
		// strict alignment of the next size class, the tree does this for us
		// by searching for a right match.
		// If there are exact size matches, we want the least strict alignment
		// that is >= than the key's alignment, so again, right match.
		// Lastly, for granularity constraints we need to search all exact
		// size/alignment duplicates, luckily right match will return the
		// left-most duplicate, so gfx_tree_succ will cover them all.
		for (
			node = gfx_tree_search(&block->nodes.free, key, GFX_TREE_MATCH_RIGHT);
			node != NULL;
			node = gfx_tree_succ(&block->nodes.free, node))
		{
			const VkDeviceSize* fKey = gfx_tree_key(&block->nodes.free, node);

			// Check if granularity constraints apply.
			_GFXMemAlloc* left = (_GFXMemAlloc*)node->list.prev;
			_GFXMemAlloc* right = (_GFXMemAlloc*)node->list.next;

			// If neighbors exist, they must be an allocation.
			const int lGran = (left != NULL && left->linear != linear);
			const int rGran = (right != NULL && right->linear != linear);

			// Get the alignment we want, if left granularity applies,
			// we use the largest of the asked alignment and the granularity.
			// We can do this because granularity must be a power of two.
			// This is necessary because a free block directly starts at the
			// end of a claimed block, so we need to align up.
			// Otherwise we still need to align up because we can encounter
			// less strict alignments when the node's size is larger.
			const VkDeviceSize align = lGran ?
				GFX_MAX(alloc->granularity, reqs.alignment) : reqs.alignment;
			const VkDeviceSize offset =
				GFX_ALIGN_UP(_GFX_KEY_OFFSET(fKey), align);

			VkDeviceSize waste =
				offset - _GFX_KEY_OFFSET(fKey);

			// If right granularity applies, we want to align down.
			// This is necessary because a free block also directly ends at
			// the start of a claimed block.
			if (rGran) waste +=
				right->offset - GFX_ALIGN_DOWN(right->offset, alloc->granularity);

			// Check if we didn't waste all space and
			// we have enough for the asked size.
			if (
				_GFX_KEY_SIZE(fKey) > waste &&
				_GFX_KEY_SIZE(fKey) - waste >= reqs.size)
			{
				_GFX_KEY_OFFSET(key) = offset; // Set the key's offset value.
				break;
			}
		}

		if (node != NULL)
			break;
	}

	if (block == NULL)
	{
		// Uh oh the search failed, try to allocate a new memory block.
		block = _gfx_alloc_mem_block(
			alloc, type, reqs.size,
			GFX_MAX(reqs.size, _GFX_DEF_LARGE_HEAP_BLOCK_SIZE),
			VK_NULL_HANDLE, VK_NULL_HANDLE);

		// Well this is not going well..
		if (block == NULL)
		{
			// If we tried the optimal memory type, and there is a defined
			// required type as fallback, try to search/allocate using that.
			// This is a bit much logging going on, however this is quite
			// an extraordinary situation, so we would surely want to
			// know about it.
			// Announce we're still trying through a warning.
			if (type == tOpt && (tReq != UINT32_MAX && tReq != tOpt))
			{
				gfx_log_warn(
					"Allocation failed, will try to fallback to another "
					"available memory heap:\n"
					"    Memory heap flags (failed): %s\n%s%s%s%s%s%s",
					_GFX_GET_VK_TYPE_EMPTY_STRING(alloc, type),
					_GFX_GET_VK_TYPE_DEVICE_LOCAL_STRING(alloc, type),
					_GFX_GET_VK_TYPE_HOST_VISIBLE_STRING(alloc, type),
					_GFX_GET_VK_TYPE_HOST_COHERENT_STRING(alloc, type),
					_GFX_GET_VK_TYPE_HOST_CACHED_STRING(alloc, type),
					_GFX_GET_VK_TYPE_LAZILY_ALLOCATED_STRING(alloc, type),
					_GFX_GET_VK_TYPE_PROTECTED_STRING(alloc, type));

				type = tReq;
				goto try_search;
			}

			return 0;
		}

		// There's 1 free node, the entire block, just pick it :)
		// We're at the beginning, so it always aligns, set offset of 0.
		node = block->nodes.free.root;
		_GFX_KEY_OFFSET(key) = 0;
	}

	// Claim the memory.
	// i.e. output the allocation data.
	*mem = (_GFXMemAlloc){
		.node   = { .free = 0 },
		.block  = block,
		.size   = _GFX_KEY_SIZE(key),
		.offset = _GFX_KEY_OFFSET(key),
		.flags  = alloc->vk.properties.memoryTypes[block->type].propertyFlags,
		.linear = linear,
		.vk     = { .memory = block->vk.memory }
	};

	gfx_list_insert_before(
		&block->nodes.list, &mem->node.list,
		(node == NULL) ? NULL : &node->list);

	// Now fix the free tree...
	// If there was no free root node to begin with, we're done!
	if (node == NULL)
		return 1;

	// So we aligned the claimed memory, this means there could be some waste
	// to the left of it, however we just ignore it and consider it unusable.
	// However to the right of the memory we might still have a big free block.
	const VkDeviceSize* cKey = gfx_tree_key(&block->nodes.free, node);

	const VkDeviceSize rOffset =
		_GFX_KEY_OFFSET(key) + _GFX_KEY_SIZE(key);
	const VkDeviceSize rSize =
		_GFX_KEY_SIZE(cKey) - (rOffset - _GFX_KEY_OFFSET(cKey));

	// The waste we created to the left is at most (alignment - 1) in size,
	// ignoring granularity. Similarly, if memory to the right is smaller
	// than the waste, we skip it as well.
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
		const VkDeviceSize rKey[2] = { rSize, rOffset };
		gfx_tree_update(&block->nodes.free, node, rKey);
	}

	return 1;
}

/****************************/
int _gfx_allocd(_GFXAllocator* alloc, _GFXMemAlloc* mem,
                VkMemoryPropertyFlags required, VkMemoryPropertyFlags optimal,
                VkMemoryRequirements reqs,
                VkBuffer buffer, VkImage image)
{
	assert(alloc != NULL);
	assert(mem != NULL);
	assert(reqs.size > 0);
	assert(reqs.memoryTypeBits != 0);
	assert(buffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE);

	// Get memory type index.
	uint32_t tReq, tOpt;
	_GFX_GET_MEM_TYPES(
		tReq, tOpt, alloc, required, optimal, reqs.memoryTypeBits,
		return 0);

	// Allocate a memory block.
	// No free root node is inserted by setting minSize == maxSize.
	_GFXMemBlock* block = NULL;

	// First try the optimal memory type.
	if (tOpt != UINT32_MAX)
		block = _gfx_alloc_mem_block(
			alloc, tOpt, reqs.size, reqs.size, buffer, image);

	// If we failed and there is a defined required type as fallback, try it.
	// Again this is a bit much logging, but again, it is a situation we
	// probably want to know about, to optimize our memory usage.
	// Still, warn that we're still trying.
	if (block == NULL && (tReq != UINT32_MAX && tReq != tOpt))
	{
		if (tOpt != UINT32_MAX) gfx_log_warn(
			"Dedicated allocation failed, will try to fallback to another "
			"available memory heap:\n"
			"    Memory heap flags (failed): %s\n%s%s%s%s%s%s",
			_GFX_GET_VK_TYPE_EMPTY_STRING(alloc, tOpt),
			_GFX_GET_VK_TYPE_DEVICE_LOCAL_STRING(alloc, tOpt),
			_GFX_GET_VK_TYPE_HOST_VISIBLE_STRING(alloc, tOpt),
			_GFX_GET_VK_TYPE_HOST_COHERENT_STRING(alloc, tOpt),
			_GFX_GET_VK_TYPE_HOST_CACHED_STRING(alloc, tOpt),
			_GFX_GET_VK_TYPE_LAZILY_ALLOCATED_STRING(alloc, tOpt),
			_GFX_GET_VK_TYPE_PROTECTED_STRING(alloc, tOpt));

		block = _gfx_alloc_mem_block(
			alloc, tReq, reqs.size, reqs.size, buffer, image);
	}

	if (block == NULL)
		return 0;

	// Claim memory,
	// i.e. output the allocation data.
	*mem = (_GFXMemAlloc){
		.node   = { .free = 0 },
		.block  = block,
		.size   = reqs.size,
		.offset = 0,
		.flags  = alloc->vk.properties.memoryTypes[block->type].propertyFlags,
		.linear = 0,
		.vk     = { .memory = block->vk.memory }
	};

	gfx_list_insert_before(&block->nodes.list, &mem->node.list, NULL);

	return 1;
}

/****************************/
void _gfx_free(_GFXAllocator* alloc, _GFXMemAlloc* mem)
{
	assert(alloc != NULL);
	assert(mem != NULL);

	_GFXMemBlock* block = mem->block;

	// Ok we have to deal with the list of memory nodes and the free tree..
	// First the case that this allocation is the only memory node.
	// Just free the memory block.
	_GFXMemNode* left = (_GFXMemNode*)mem->node.list.prev;
	_GFXMemNode* right = (_GFXMemNode*)mem->node.list.next;

	if (left == NULL && right == NULL)
	{
		_gfx_free_mem_block(alloc, block);
		return;
	}

	// Now we regard the allocation as free and coalesce it with its neighbours.
	// We may have created some wasted space during allocation, make sure to
	// reclaim those areas as free space as well.
	// This is why we first calculate the range of space we can reclaim.
	const VkDeviceSize lBound =
		(left == NULL) ? 0 :
		(left->free) ?
			_GFX_KEY_OFFSET(gfx_tree_key(&block->nodes.free, left)) :
			((_GFXMemAlloc*)left)->offset +
			((_GFXMemAlloc*)left)->size;

	const VkDeviceSize rBound =
		(right == NULL) ? block->size :
		(right->free) ?
			_GFX_KEY_OFFSET(gfx_tree_key(&block->nodes.free, right)) +
			_GFX_KEY_SIZE(gfx_tree_key(&block->nodes.free, right)) :
			((_GFXMemAlloc*)right)->offset;

	// Now modify the list and free tree to reflect the claimed space.
	const VkDeviceSize key[2] = { rBound - lBound, lBound };
	const int lFree = (left != NULL) && left->free;
	const int rFree = (right != NULL) && right->free;

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
		const int full = (block->nodes.free.root == NULL);

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
				"from a Vulkan memory object, potentially lost %"PRIu64" bytes.",
				_GFX_KEY_SIZE(key));
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

/****************************/
void* _gfx_map(_GFXAllocator* alloc, _GFXMemAlloc* mem)
{
	assert(alloc != NULL);
	assert(mem != NULL);
	assert((mem->flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);

	void* ptr;
	_GFXMemBlock* block = mem->block;

	// Ok so we are going to map entire memory blocks, this way we can
	// map any allocation in any memory block concurrently, because in reality
	// there is only 1 mapping, ever.
	// Lock access to the mapping so checking refs and the actual mapping of
	// the Vulkan memory object are one atomic operation.
	_gfx_mutex_lock(&block->map.lock);

	// If the block is not mapped yet, map it.
	// refs must be >= 0 because of requirements of _gfx_unmap (!).
	if (block->map.refs == 0)
	{
		void* vkPtr;
		_GFXContext* context = alloc->context;

		_GFX_VK_CHECK(
			context->vk.MapMemory(
				context->vk.device, block->vk.memory, 0, VK_WHOLE_SIZE, 0,
				&vkPtr),
			goto unlock);

		block->map.ptr = vkPtr;
	}

	// Increase reference count.
	++block->map.refs;

unlock:
	// Read resulting pointer just before unlock :)
	ptr = (block->map.ptr == NULL) ? NULL :
		(void*)((char*)block->map.ptr + mem->offset);

	_gfx_mutex_unlock(&block->map.lock);

	return ptr;
}

/****************************/
void _gfx_unmap(_GFXAllocator* alloc, _GFXMemAlloc* mem)
{
	assert(alloc != NULL);
	assert(mem != NULL);

	_GFXMemBlock* block = mem->block;

	// Obviously we lock again so dereferencing and unmapping is atomic.
	_gfx_mutex_lock(&block->map.lock);

	// Decrease reference count & unmap when we hit 0.
	// This function is required to be called _exactly_ once (and no more)
	// for every _gfx_map, therefore we can assume refs is > 0.
	if ((--block->map.refs) == 0)
	{
		_GFXContext* context = alloc->context;
		context->vk.UnmapMemory(context->vk.device, block->vk.memory);

		block->map.ptr = NULL;
	}

	_gfx_mutex_unlock(&block->map.lock);
}
