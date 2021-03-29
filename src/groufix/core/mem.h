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
 * Vulkan memory object (e.g. VRAM memory block).
 */
typedef struct _GFXMemBlock
{
	GFXListNode list; // Base-type.

	uint32_t type; // Vulkan memory type index.
	uint64_t size;


	// Related memory nodes.
	struct
	{
		GFXTree free; // Stores { uint64_t, uint64_t } : _GFXMemNode.
		GFXList list; // References _GFXMemNode | _GFXMemAlloc.

	} nodes;


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
 * Allocated memory node.
 */
typedef struct _GFXMemAlloc
{
	_GFXMemNode   node; // Base-type.
	_GFXMemBlock* block;

	uint64_t size;
	uint64_t offset;


	// Vulkan fields.
	struct
	{
		VkDeviceMemory memory; // Redundant for memory locality.

	} vk;

} _GFXMemAlloc;


/**
 * Vulkan memory allocator definition.
 */
typedef struct _GFXAllocator
{
	_GFXContext* context;

	GFXList free;   // References _GFXMemBlock.
	GFXList allocd; // References _GFXMemBlock.


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
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL.
 * @param reqs  Must be valid (size > 0, align = a power of two, bits != 0).
 * @param flags Must have at least 1 bit set.
 * @return Non-zero on success.
 *
 * Not thread-safe at all.
 */
int _gfx_alloc(_GFXAllocator* alloc, _GFXMemAlloc* mem,
               VkMemoryRequirements reqs, VkMemoryPropertyFlags flags);

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


#endif
