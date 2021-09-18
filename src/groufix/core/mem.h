/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_MEM_H
#define _GFX_CORE_MEM_H

#include "groufix/containers/list.h"
#include "groufix/containers/tree.h"
#include "groufix/core.h"


/**
 * Memory block (i.e. Vulkan memory object to be subdivided).
 */
typedef struct _GFXMemBlock
{
	GFXListNode  list; // Base-type.
	uint32_t     type; // Vulkan memory type index.
	VkDeviceSize size;


	// Related memory nodes.
	struct
	{
		GFXTree free; // Stores { VkDeviceSize, VkDeviceSize } : _GFXMemNode.
		GFXList list; // References _GFXMemNode | _GFXMemAlloc.

	} nodes;


	// Mapped memory pointer.
	struct
	{
		unsigned long refs;
		void*         ptr; // NULL if not mapped.
		_GFXMutex     lock;

	} map;


	// Vulkan fields.
	struct
	{
		VkDeviceMemory memory;

	} vk;

} _GFXMemBlock;


/**
 * Memory node, linked to neighbours in actual memory.
 */
typedef struct _GFXMemNode
{
	GFXListNode list; // Base-type.

	int free; // isa _GFXMemAlloc if zero, isa search tree node if non-zero.

} _GFXMemNode;


/**
 * Allocated memory node (contains everything necessary for use).
 */
typedef struct _GFXMemAlloc
{
	_GFXMemNode   node; // Base-type.
	_GFXMemBlock* block;

	VkDeviceSize  size;
	VkDeviceSize  offset;

	VkMemoryPropertyFlags flags; // Actual used flags.

	// For granularity constraints.
	int linear;


	// Vulkan fields.
	struct
	{
		VkDeviceMemory memory; // Redundant for locality.

	} vk;

} _GFXMemAlloc;


/**
 * Vulkan memory allocator definition.
 */
typedef struct _GFXAllocator
{
	_GFXDevice*  device; // For the allocation limit.
	_GFXContext* context;

	GFXList free;   // References _GFXMemBlock.
	GFXList allocd; // References _GFXMemBlock.

	// Constant, queried once.
	VkDeviceSize granularity;


	// Vulkan fields.
	struct
	{
		VkPhysicalDeviceMemoryProperties properties; // Queried once.

	} vk;

} _GFXAllocator;


/****************************
 * Vulkan memory management.
 ****************************/

/**
 * Initializes an allocator.
 * @param alloc  Cannot be NULL.
 * @param device Cannot be NULL.
 *
 * _gfx_device_init_context must have returned successfully at least once
 * for the given device.
 */
void _gfx_allocator_init(_GFXAllocator* alloc, _GFXDevice* device);

/**
 * Clears an allocator, freeing all allocations.
 * @param alloc Cannot be NULL.
 */
void _gfx_allocator_clear(_GFXAllocator* alloc);

/**
 * Allocate some Vulkan memory.
 * The object pointed to by mem cannot be moved or copied!
 * @param alloc    Cannot be NULL.
 * @param mem      Cannot be NULL.
 * @param linear   Non-zero for a linear resource, 0 for a non-linear one.
 * @param required Required flags, if they cannot be satisfied, it will fail.
 * @param optimal  Optimal, i.e. preferred flags.
 * @param reqs     Must be valid (size > 0, align = a power of two, bits != 0).
 * @return Non-zero on success.
 *
 * Not thread-safe at all.
 */
int _gfx_alloc(_GFXAllocator* alloc, _GFXMemAlloc* mem, int linear,
               VkMemoryPropertyFlags required, VkMemoryPropertyFlags optimal,
               VkMemoryRequirements reqs);

/**
 * TODO: Add image and buffer arguments so we can do true 'Vulkan'-dedication?
 * Allocate some dedicated Vulkan memory,
 * meaning it will not be sub-allocated from a larger memory block.
 * @see _gfx_alloc.
 */
int _gfx_allocd(_GFXAllocator* alloc, _GFXMemAlloc* mem,
                VkMemoryPropertyFlags required, VkMemoryPropertyFlags optimal,
                VkMemoryRequirements reqs);

/**
 * Free some Vulkan memory.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL, must be allocated from alloc.
 *
 * Not thread-safe at all.
 * The content of mem is invalidated after this call.
 * Silently warns when not able to modify the free structure appropriately.
 */
void _gfx_free(_GFXAllocator* alloc, _GFXMemAlloc* mem);

/**
 * Maps some Vulkan memory to a host virtual address pointer, this can be
 * called multiple times, the actual memory object is reference counted.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL.
 * @return NULL on failure.
 *
 * This function is reentrant!
 * The given object must be allocated with VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT.
 */
void* _gfx_map(_GFXAllocator* alloc, _GFXMemAlloc* mem);

/**
 * Unmaps Vulkan memory, invalidating a mapped pointer.
 * Must be called exactly once for every successful call to _gfx_map.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL.
 *
 * This function is reentrant!
 */
void _gfx_unmap(_GFXAllocator* alloc, _GFXMemAlloc* mem);


#endif
