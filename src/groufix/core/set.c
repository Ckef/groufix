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
GFX_API GFXSet* gfx_renderer_add_set(GFXRenderer* renderer,
                                     GFXTechnique* technique, size_t set,
                                     size_t numResources, size_t numGroups,
                                     size_t numViews, size_t numSamplers,
                                     const GFXSetResource* resources,
                                     const GFXSetGroup* groups,
                                     const GFXView* views,
                                     const GFXSampler* samplers)
{
	assert(renderer != NULL);
	assert(!renderer->recording);
	assert(technique != NULL);
	assert(technique->renderer == renderer);
	assert(set < technique->numSets);
	assert(numResources == 0 || resources != NULL);
	assert(numGroups == 0 || groups != NULL);
	assert(numViews == 0 || views != NULL);
	assert(numSamplers == 0 || samplers != NULL);

	// First of all, make sure the technique is locked.
	if (!gfx_tech_lock(technique))
		goto error;

	// Get the number of bindings & entries to allocate.
	size_t numBindings;
	size_t numEntries;
	_gfx_tech_get_set_size(technique, set, &numBindings, &numEntries);

	// Allocate a new set.
	size_t structSize = GFX_ALIGN_UP(
		sizeof(GFXSet) + sizeof(_GFXSetBinding) * numBindings,
		_Alignof(_GFXSetEntry));

	GFXSet* aset = malloc(
		structSize +
		sizeof(_GFXSetEntry) * numEntries);

	if (aset == NULL)
		goto error;

	// Initialize the set.
	aset->renderer = renderer;
	aset->setLayout = technique->setLayouts[set];
	aset->key = NULL;
	aset->numAttachs = 0;
	aset->numBindings = numBindings;
	atomic_store(&aset->used, 0);

	// Get all the bindings.
	_GFXSetEntry* entryPtr =
		(_GFXSetEntry*)((char*)aset + structSize);

	for (size_t b = 0; b < numBindings; ++b)
	{
		_GFXSetBinding* binding = &aset->bindings[b];

		size_t entries = 0;
		if (_gfx_tech_get_set_binding(technique, set, b, binding))
			entries = binding->count;

		binding->keyIndex = 0;
		binding->entries = entries > 0 ? entryPtr : NULL;
		entryPtr += entries;

		// Initialize entries to empty.
		for (size_t e = 0; e < entries; ++e)
		{
			_GFXSetEntry* entry = &binding->entries[e];
			entry->ref = GFX_REF_NULL;
			entry->sampler = NULL;
			entry->vk.format = VK_FORMAT_UNDEFINED;
		}
	}

	// Link the set into the renderer.
	// Modifying the renderer, lock!
	_gfx_mutex_lock(&renderer->lock);
	gfx_list_insert_after(&renderer->sets, &aset->list, NULL);
	_gfx_mutex_unlock(&renderer->lock);

	return aset;


	// Error on failure.
error:
	gfx_log_error("Could not add a new set to a renderer.");
	return NULL;
}

/****************************/
GFX_API void gfx_erase_set(GFXSet* set)
{
	assert(set != NULL);
	assert(!set->renderer->recording);

	GFXRenderer* renderer = set->renderer;

	// Modifying the renderer, lock!
	// We need to lock the recycle call too, this should be a rare path
	// to go down, given dynamic offsets or alike are always prefered to
	// updating sets.
	// So aggressive locking is fine.
	_gfx_mutex_lock(&renderer->lock);

	// Unlink itself from the renderer.
	gfx_list_erase(&renderer->sets, &set->list);

	// Recycle all matching descriptor sets.
	if (atomic_load(&set->used))
		_gfx_pool_recycle(&renderer->pool, set->key);

	_gfx_mutex_unlock(&renderer->lock);

	free(set->key);
	free(set);
}

/****************************/
GFX_API int gfx_set_resources(GFXSet* set,
                              size_t numResources, const GFXSetResource* resources)
{
	assert(set != NULL);
	assert(!set->renderer->recording);
	assert(numResources > 0);
	assert(resources != NULL);

	// TODO: Implement.

	return 0;
}

/****************************/
GFX_API int gfx_set_groups(GFXSet* set,
                           size_t numGroups, const GFXSetGroup* groups)
{
	assert(set != NULL);
	assert(!set->renderer->recording);
	assert(numGroups > 0);
	assert(groups != NULL);

	// TODO: Implement.

	return 0;
}

/****************************/
GFX_API int gfx_set_views(GFXSet* set,
                          size_t numViews, const GFXView* views)
{
	assert(set != NULL);
	assert(!set->renderer->recording);
	assert(numViews > 0);
	assert(views != NULL);

	// TODO: Implement.

	return 0;
}

/****************************/
GFX_API int gfx_set_samplers(GFXSet* set,
                             size_t numSamplers, const GFXSampler* samplers)
{
	assert(set != NULL);
	assert(!set->renderer->recording);
	assert(numSamplers > 0);
	assert(samplers != NULL);

	// TODO: Implement.

	return 0;
}
