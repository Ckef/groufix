/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_DEPS_H
#define GFX_CORE_DEPS_H

#include "groufix/core/device.h"
#include "groufix/def.h"


/**
 * Synchronization target.
 */
typedef enum GFXSyncTarget
{
	GFX_SYNC_VERTEX_READ   = 0x000001,
	GFX_SYNC_INDEX_READ    = 0x000002,
	GFX_SYNC_UNIFORM_READ  = 0x000004,
	GFX_SYNC_INDIRECT_READ = 0x000008,
	GFX_SYNC_SAMPLED_READ  = 0x000010,

	GFX_SYNC_STORAGE_READ     = 0x000020,
	GFX_SYNC_STORAGE_WRITE    = 0x000040,
	GFX_SYNC_ATTACHMENT_READ  = 0x000080,
	GFX_SYNC_ATTACHMENT_WRITE = 0x000100,

	GFX_SYNC_TRANSFER_READ       = 0x000200,
	GFX_SYNC_TRANSFER_WRITE      = 0x000400,
	GFX_SYNC_TRANSFER_FAST_READ  = 0x000800,
	GFX_SYNC_TRANSFER_FAST_WRITE = 0x001000

} GFXSyncTarget;


/**
 * Dependency (synchronization) object definition.
 */
typedef struct GFXDependency GFXDependency;


/**
 * Creates a dependency object.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @return NULL on failure.
 */
GFX_API GFXDependency* gfx_create_dep(GFXDevice* device);

/**
 * Destroys a dependency object.
 */
GFX_API void gfx_destroy_dep(GFXDependency* dep);


#endif
