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
	GFXReference  ref; // May be GFX_REF_NULL to flag this object for reuse.
	GFXRange      range;


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

	// Vulkan family indices.
	uint32_t graphics;
	uint32_t compute;
	uint32_t transfer;
};


#endif
