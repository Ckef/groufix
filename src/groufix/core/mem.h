/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_MEM_H
#define _GFX_CORE_MEM_H

#include "groufix/containers/tree.h"
#include "groufix/core.h"


/**
 * Vulkan memory object (e.g. VRAM memory block).
 */
typedef struct _GFXMemBlock
{
	struct _GFXMemBlock* next;
	struct _GFXMemBlock* prev;

	uint32_t type; // Vulkan memory type index.
	uint64_t size;
	GFXTree  free; // Stores { uint64_t, uint64_t } : _GFXMemNode.


	// Vulkan fields.
	struct
	{
		VkDeviceMemory memory;

	} vk;

} _GFXMemBlock;


/**
 * Memory node, allocated or free.
 */
typedef struct _GFXMemNode
{
	// Neighbours in actual memory.
	struct _GFXMemNode* left;
	struct _GFXMemNode* right;

	int free; // isa _GFXMemAlloc if zero, isa search tree node if non-zero.

} _GFXMemNode;


/**
 * Allocated memory node.
 */
typedef struct _GFXMemAlloc
{
	_GFXMemNode   node;
	_GFXMemBlock* block;

	uint64_t size;
	uint64_t offset;


	// Vulkan fields.
	struct
	{
		VkDeviceMemory memory; // May be redundant (for memory locality).

	} vk;

} _GFXMemAlloc;


/**
 * Vulkan memory allocator definition.
 */
typedef struct _GFXAllocator
{
	_GFXContext* context;

	_GFXMemBlock* free;
	_GFXMemBlock* allocd;


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
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL.
 * @param reqs  Must be valid (size > 0, align = a power of two, bits != 0).
 * @param flags Must have at least 1 bit set.
 * @return Non-zero on success.
 */
int _gfx_allocator_alloc(_GFXAllocator* alloc, _GFXMemAlloc* mem,
                         VkMemoryRequirements reqs, VkMemoryPropertyFlags flags);

/**
 * Free some Vulkan memory.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL, must be allocated from alloc.
 */
void _gfx_allocator_free(_GFXAllocator* alloc, _GFXMemAlloc* mem);


#endif
