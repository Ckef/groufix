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

	uint64_t size;
	GFXTree  free; // Stores uint64_t : _GFXMemFree.


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

	int free; // isa _GFXMemAlloc if zero, isa _GFXMemFree if non-zero.

} _GFXMemNode;


/**
 * Free memory node (isa search tree node).
 */
typedef struct _GFXMemFree
{
	_GFXMemNode node;

	// Size is stored as key in the search tree.
	uint64_t offset;

} _GFXMemFree;


/**
 * Allocated memory node.
 */
typedef struct _GFXMemAlloc
{
	_GFXMemNode   node;
	_GFXMemBlock* block;

	uint64_t offset;
	uint64_t size;


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

} _GFXAllocator;


/****************************
 * Vulkan memory management.
 ****************************/

/**
 * Initializes an allocator.
 * @param alloc   Cannot be NULL.
 * @param context Cannot be NULL.
 */
void _gfx_allocator_init(_GFXAllocator* alloc, _GFXContext* context);

/**
 * Clears an allocator, freeing all allocations.
 * @param alloc Cannot be NULL.
 */
void _gfx_allocator_clear(_GFXAllocator* alloc);

/**
 * Allocate some vulkan memory.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL.
 * @param size  Must be > 0.
 * @return Non-zero on success.
 */
int _gfx_allocator_alloc(_GFXAllocator* alloc, _GFXMemAlloc* mem,
                         uint64_t size, uint64_t align);

/**
 * Free some vulkan memory.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL.
 */
void _gfx_allocator_free(_GFXAllocator* alloc, _GFXMemAlloc* mem);


#endif
