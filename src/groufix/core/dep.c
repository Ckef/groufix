/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
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
int _gfx_deps_catch(VkCommandBuffer cmd,
                    size_t numInjs, const GFXInject* injs,
                    _GFXInjection* injection)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.refs != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.masks != NULL);

	// Initialize the injection output.
	injection->out.numWaits = 0;
	injection->out.waits = NULL;
	injection->out.numSigs = 0;
	injection->out.sigs = NULL;

	// Ok so during a catch, we loop over all injections and filter out the
	// wait commands. For each wait command, we match against all pending
	// sychronization objects and 'catch' them with a potential barrier.
	for (size_t i = 0; i < numInjs; ++i)
	{
		if (
			injs[i].type != GFX_DEP_WAIT &&
			injs[i].type != GFX_DEP_WAIT_RANGE)
		{
			continue;
		}

		// If the wait command AND the injection metadata specify references,
		// filter the wait commands against that, ignore on mismatch.
		// Keep track of the reference's index in the injection metadata,
		// so we can get its access mask with respect to the operation.
		_GFXUnpackRef unp = _gfx_ref_unpack(injs[i].ref);
		size_t injRef = SIZE_MAX;

		if (!GFX_REF_IS_NULL(injs[i].ref) && injection->inp.numRefs > 0)
		{
			for (injRef = 0; injRef < injection->inp.numRefs; ++injRef)
				if (_GFX_UNPACK_REF_IS_EQUAL(injection->inp.refs[injRef], unp))
					break;

			if (injRef >= injection->inp.numRefs)
			{
				gfx_log_warn(
					"Dependency injection (wait) command ignored, "
					"given underlying resource not used by operation.");

				continue;
			}
		}

		// Compute the resources & their range to match against.
		// TODO: do.

		// Now the bit where we match against all synchronization objects.
		// We lock for each command individually.
		_gfx_mutex_lock(&injs[i].dep->lock);

		for (size_t s = 0; s < injs[i].dep->syncs.size; ++s)
		{
			_GFXSync* sync = gfx_vec_at(&injs[i].dep->syncs, s);

			if (
				sync->stage != _GFX_SYNC_PENDING ||
				sync->vk.dstFamily != injection->inp.family)
			{
				continue;
			}

			// TODO: Continue implementing...
		}

		_gfx_mutex_unlock(&injs[i].dep->lock);
	}

	return 1;
}

/****************************/
int _gfx_deps_prepare(VkCommandBuffer cmd,
                      size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection)
{
	assert(cmd != VK_NULL_HANDLE);
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.refs != NULL);
	assert(injection->inp.numRefs == 0 || injection->inp.masks != NULL);

	// During a prepare, we again loop over all injections and filter out the
	// signal commands. For each signal command we find the resources it is
	// supposed to signal, claim a new synchronization object and 'prepare'
	// them with a potential barrier.
	for (size_t i = 0; i < numInjs; ++i)
	{
		if (
			injs[i].type != GFX_DEP_SIGNAL &&
			injs[i].type != GFX_DEP_SIGNAL_RANGE)
		{
			continue;
		}

		// TODO: Continue implementing...
	}

	return 1;
}

/****************************/
void _gfx_deps_abort(size_t numInjs, const GFXInject* injs,
                     _GFXInjection* injection)
{
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
}

/****************************/
void _gfx_deps_finish(size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection)
{
	assert(numInjs == 0 || injs != NULL);
	assert(injection != NULL);
}
