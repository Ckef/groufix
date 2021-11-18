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
 * Dependency injection metadata.
 */
typedef struct _GFXInjection
{
	size_t       numWaits;
	VkSemaphore* waits;

	size_t       numSigs;
	VkSemaphore* sigs;

} _GFXInjection;


/**
 * Synchronization (metadata) object.
 */
typedef struct _GFXSync
{
	GFXReference  ref;
	GFXRange      range;
	unsigned long tag; // So we can recycle, 0 = yet untagged.

	// Claimed by (injections can be async), may be NULL.
	const _GFXInjection* inj;


	// Stage in the object's lifecycle.
	enum
	{
		_GFX_SYNC_UNUSED,
		_GFX_SYNC_PREPARE,
		_GFX_SYNC_PENDING,
		_GFX_SYNC_CATCH,
		_GFX_SYNC_USED

	} stage;


	// Vulkan fields.
	struct
	{
		VkSemaphore signaled; // May be VK_NULL_HANDLE.

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
 * TODO: Somehow generate or pass a tag for recycling.
 * Starts a new dependency injection by catching pending signal commands.
 * The object pointed to by injection cannot be moved or copied!
 * @param cmd       To record barriers to, cannot be VK_NULL_HANDLE.
 * @param family    Vulkan queue family cmd will be submitted to.
 * @param numInjs   Number of given injection commands.
 * @param injs      Given injection commands.
 * @param numRefs   Number of references involved in this operation.
 * @param refs      References involved in this operation.
 * @param injection Output injection metadata, cannot be NULL.
 * @param Zero on failure.
 *
 * Thread-safe with respect to all dependency objects!
 * Either _gfx_deps_abort() or _gfx_deps_finish() must be called with the same
 * arguments to appropriately cleanup and free the injection metadata!
 */
int _gfx_deps_catch(VkCommandBuffer cmd, uint32_t family,
                    size_t numInjs, const GFXInject* injs,
                    size_t numRefs, const GFXReference* refs,
                    _GFXInjection* injection);

/**
 * Injects dependencies by preparing new signal commands.
 * @see _gfx_deps_catch.
 *
 * Thread-safe with respect to all dependency objects!
 * Can only be called after a successful call to _gfx_deps_catch,
 * with the exact same arguments.
 */
int _gfx_deps_prepare(VkCommandBuffer cmd, uint32_t family,
                      size_t numInjs, const GFXInject* injs,
                      size_t numRefs, const GFXReference* refs,
                      _GFXInjection* injection);

/**
 * Aborts a dependency injection, freeing all data.
 * @see _gfx_deps_catch.
 *
 * Thread-safe with respect to all dependency objects!
 * The content of injection is invalidated after this call.
 */
void _gfx_deps_abort(size_t numInjs, const GFXInject* injs,
                     _GFXInjection* injection);

/**
 * Finalizes a dependency injection, all signal commands are made visible for
 * future wait commands and all wait commands are finalized and cleaned up.
 * @see _gfx_deps_catch.
 *
 * Thread-safe with respect to all dependency objects!
 * The content of injection is invalidated after this call.
 */
void _gfx_deps_finish(size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection);


#endif
