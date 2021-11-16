/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_SYNC_H
#define _GFX_CORE_SYNC_H

#include "groufix/containers/vec.h"
#include "groufix/core.h"


/**
 * Synchronization (metadata) object.
 */
typedef struct _GFXSync
{
	GFXReference  ref;
	GFXRange      range;
	unsigned long tag; // So we can recycle.


	// Stage in the object's lifecycle.
	enum
	{
		_GFX_SYNC_UNUSED,
		_GFX_SYNC_PREPARE,
		_GFX_SYNC_PENDING,
		_GFX_SYNC_USED

	} stage;


	// Vulkan fields.
	struct
	{
		VkSemaphore signaled;

		// Barrier metadata.
		VkAccessFlags srcAccess;
		VkAccessFlags dstAccess;
		VkImageLayout oldLayout;
		VkImageLayout newLayout;
		uint32_t      srcFamily;
		uint32_t      dstFamily;

	} vk;

} _GFXSync;


/**
 * Internal dependency object.
 */
struct GFXDependency
{
	_GFXContext* context;
	GFXVec       syncs; // Stores _GFXSync.
	_GFXMutex    lock;

	// Vulkan family indices.
	uint32_t graphics;
	uint32_t compute;
	uint32_t transfer;
};


/****************************
 * Injection into operations/dependencies.
 ****************************/

/**
 * Dependency injection metadata.
 * References sync objects of a set of dependencies.
 */
typedef struct _GFXInjection
{
	size_t numWaits;
	void*  waits; // Stores { GFXDependency*, size_t }.

	size_t numSigs;
	void*  sigs; // Stores { GFXDependency*, size_t }.


	// To be passed to Vulkan.
	size_t       numWaitSems;
	VkSemaphore* waitSems;

	size_t       numSigSems;
	VkSemaphore* sigSems;

} _GFXInjection;


/**
 * TODO: Somehow generate or pass a tag for recycling.
 * Prepares all data for a dependency injection.
 * @param numDeps   Number of given dependency arguments, must be > 0.
 * @param deps      Given dependency arguments, cannot be NULL.
 * @param numRefs   Number of reference involved in this operation.
 * @param refs      References involved in this operation.
 * @param injection Output injection metadata, cannot be NULL.
 * @return Zero on failure.
 *
 * Either _gfx_deps_abort() or _gfx_deps_finish() must be called
 * to appropriately cleanup and free the injection metadata!
 */
int _gfx_deps_prepare(size_t numDeps, const GFXDepArg* deps,
                      size_t numRefs, const GFXReference* refs,
                      _GFXInjection* injection);

/**
 * Record all 'wait' barriers into a Vulkan command buffer.
 * @param cmd       Cannot be VK_NULL_HANDLE and must be in the recording state.
 * @param injection Must be the output of a call to _gfx_deps_prepare.
 */
void _gfx_deps_record_wait(VkCommandBuffer cmd, const _GFXInjection* injection);

/**
 * Record all 'signal' barriers into a Vulkan command buffer.
 * @param cmd       Cannot be VK_NULL_HANDLE and must be in the recording state.
 * @param injection Must be the output of a call to _gfx_deps_prepare.
 */
void _gfx_deps_record_sig(VkCommandBuffer cmd, const _GFXInjection* injection);

/**
 * Aborts a dependency injection, freeing all data.
 * After this call, the injection object is invalid!
 */
void _gfx_deps_abort(_GFXInjection* injection);

/**
 * Finalizes a dependency injection, after this call all signal commands
 * are visible for future wait commands and all wait commands are finalized.
 * After this call, the injection object is invalid!
 */
void _gfx_deps_finish(_GFXInjection* injection);


#endif
