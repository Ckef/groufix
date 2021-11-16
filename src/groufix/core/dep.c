/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/sync.h"
#include <assert.h>
#include <stdlib.h>


/****************************/
GFX_API GFXDependency* gfx_create_dep(GFXDevice* device)
{
	// Allocate a new dependency object.
	GFXDependency* dep = malloc(sizeof(GFXDependency));
	if (dep == NULL) goto clean;

	// Get context associated with the device.
	_GFX_GET_CONTEXT(dep->context, device, goto clean);

	// Initialize things,
	// we get all queue family indices for ownership transfers.
	if (!_gfx_mutex_init(&dep->lock))
		goto clean;

	_GFXQueue graphics, compute, transfer;
	_gfx_pick_queue(dep->context, &graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	_gfx_pick_queue(dep->context, &compute, VK_QUEUE_COMPUTE_BIT, 0);
	_gfx_pick_queue(dep->context, &transfer, VK_QUEUE_TRANSFER_BIT, 0);

	dep->graphics = graphics.family;
	dep->compute = compute.family;
	dep->transfer = transfer.family;

	gfx_vec_init(&dep->syncs, sizeof(_GFXSync));

	return dep;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not create a new dependency object.");
	free(dep);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_dep(GFXDependency* dep)
{
	if (dep == NULL)
		return;

	gfx_vec_clear(&dep->syncs);
	_gfx_mutex_clear(&dep->lock);

	free(dep);
}

/****************************/
int _gfx_deps_prepare(size_t numDeps, const GFXDepArg* deps,
                      size_t numRefs, const GFXReference* refs,
                      _GFXInjection* injection)
{
	assert(numDeps > 0);
	assert(deps != NULL);
	assert(numRefs == 0 || refs != NULL);
	assert(injection != NULL);

	return 0;
}

/****************************/
void _gfx_deps_record_wait(VkCommandBuffer cmd, const _GFXInjection* injection)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(injection != NULL);
}

/****************************/
void _gfx_deps_record_sig(VkCommandBuffer cmd, const _GFXInjection* injection)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(injection != NULL);
}

/****************************/
void _gfx_deps_abort(_GFXInjection* injection)
{
	assert(injection != NULL);
}

/****************************/
void _gfx_deps_finish(_GFXInjection* injection)
{
	assert(injection != NULL);
}
