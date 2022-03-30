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
#include <string.h>


// Type checkers for Vulkan update info.
#define _GFX_DESCRIPTOR_IS_BUFFER(type) \
	((type) == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || \
	(type) == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)

#define _GFX_DESCRIPTOR_IS_IMAGE(type) \
	((type) == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || \
	(type) == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || \
	(type) == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)

#define _GFX_DESCRIPTOR_IS_SAMPLER(type) \
	((type) == VK_DESCRIPTOR_TYPE_SAMPLER || \
	(type) == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)

#define _GFX_DESCRIPTOR_IS_VIEW(type) \
	((type) == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)


// Type checkers for groufix update info.
#define _GFX_BINDING_IS_BUFFER(type) \
	(_GFX_DESCRIPTOR_IS_BUFFER(type) || _GFX_DESCRIPTOR_IS_VIEW(type))

#define _GFX_BINDING_IS_IMAGE(type) \
	(_GFX_DESCRIPTOR_IS_IMAGE(type))

#define _GFX_BINDING_IS_SAMPLER(type) \
	(_GFX_DESCRIPTOR_IS_SAMPLER(type))


/****************************
 * Mirrors _GFXHashKey, but containing one _GFXCacheElem* and one GFXSet*.
 */
typedef union _GFXSetKey
{
	_GFXHashKey hash;

	struct {
		size_t len;
		char bytes[sizeof(_GFXCacheElem*) + sizeof(GFXSet*)];
	};

} _GFXSetKey;


/****************************
 * Recycles all possible matching descriptor sets that a set has queried
 * from the renderer's pool. Thread-safe outside recording!
 */
static void _gfx_set_recycle(GFXSet* set)
{
	// Only recycle if the set has been used & reset used flag.
	if (atomic_exchange(&set->used, 0))
	{
		GFXRenderer* renderer = set->renderer;

		// Create a set key.
		_GFXSetKey key;
		key.len = sizeof(key.bytes);
		memcpy(key.bytes, &set->setLayout, sizeof(_GFXCacheElem*));
		memcpy(key.bytes + sizeof(_GFXCacheElem*), &set, sizeof(GFXSet*));

		// Recycle all matching descriptor sets, this is explicitly NOT
		// thread-safe, so we use the renderer's lock!
		// This should be a rare path to go down, given dynamic offsets or
		// alike are always prefered to updating sets.
		// So aggressive locking is fine.
		_gfx_mutex_lock(&renderer->lock);
		_gfx_pool_recycle(&renderer->pool, &key.hash);
		_gfx_mutex_unlock(&renderer->lock);
	}
}

/****************************/
_GFXPoolElem* _gfx_set_get(GFXSet* set, _GFXPoolSub* sub)
{
	assert(set != NULL);
	assert(sub != NULL);

	// Create a set key.
	_GFXSetKey key;
	key.len = sizeof(key.bytes);
	memcpy(key.bytes, &set->setLayout, sizeof(_GFXCacheElem*));
	memcpy(key.bytes + sizeof(_GFXCacheElem*), &set, sizeof(GFXSet*));

	// Get the first set entry.
	const size_t structSize = GFX_ALIGN_UP(
		sizeof(GFXSet) + sizeof(_GFXSetBinding) * set->numBindings,
		_Alignof(_GFXSetEntry));

	const _GFXSetEntry* first =
		(const _GFXSetEntry*)((char*)set + structSize);

	// Get the descriptor set.
	_GFXPoolElem* elem = _gfx_pool_get(
		&set->renderer->pool, sub,
		set->setLayout, &key.hash, &first->vk.update);

	// Make sure to set the used flag on success.
	if (elem != NULL) atomic_store(&set->used, 1);
	return elem;
}

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
	const size_t structSize = GFX_ALIGN_UP(
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
		// If this returns 0, we do not use any update entries,
		// even though binding->count might be > 0!
		if (_gfx_tech_get_set_binding(technique, set, b, binding))
			entries = binding->count;

		binding->entries = entries > 0 ? entryPtr : NULL;
		entryPtr += entries;

		// Initialize entries to empty.
		for (size_t e = 0; e < entries; ++e)
		{
			_GFXSetEntry* entry = &binding->entries[e];
			entry->ref = GFX_REF_NULL;
			entry->viewType = GFX_VIEW_2D;
			entry->sampler = NULL;
			entry->vk.format = VK_FORMAT_UNDEFINED;

			// Set range, leave undefined if only a sampler.
			if (_GFX_BINDING_IS_BUFFER(binding->type))
				entry->range = (GFXRange){
					.offset = 0,
					.size = 0
				};

			else if (_GFX_BINDING_IS_IMAGE(binding->type))
				entry->range = (GFXRange){
					// Specify all aspect flags, will be filtered later on.
					.aspect = GFX_IMAGE_COLOR | GFX_IMAGE_DEPTH | GFX_IMAGE_STENCIL,
					.mipmap = 0,
					.numMipmaps = 0,
					.layer = 0,
					.numLayers = 0
				};

			// Set update info.
			if (_GFX_DESCRIPTOR_IS_BUFFER(binding->type))
				entry->vk.update.buffer = (VkDescriptorBufferInfo){
					.buffer = VK_NULL_HANDLE,
					.offset = 0,
					.range = 0
				};

			else if(_GFX_DESCRIPTOR_IS_VIEW(binding->type))
				entry->vk.update.view = VK_NULL_HANDLE;

			else
				// Else it's an image and/or sampler.
				entry->vk.update.image = (VkDescriptorImageInfo){
					.sampler = VK_NULL_HANDLE,
					.imageView = VK_NULL_HANDLE,
					.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED
				};
		}
	}

	// And finally, before finishing up, set all initial resources, groups,
	// views and samplers. Let individual resources and views overwrite groups.
	gfx_set_groups(aset, numGroups, groups);
	gfx_set_resources(aset, numResources, resources);
	gfx_set_samplers(aset, numSamplers, samplers);
	gfx_set_views(aset, numViews, views);

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

	// Unlink itself from the renderer.
	// Modifying the renderer, lock!
	_gfx_mutex_lock(&renderer->lock);
	gfx_list_erase(&renderer->sets, &set->list);
	_gfx_mutex_unlock(&renderer->lock);

	// And recycle all matching descriptor sets,
	// none of the resources may be referenced anymore!
	_gfx_set_recycle(set);

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

	// Keep track of success, much like the technique,
	// we skip over failures.
	int success = 1;

	for (size_t r = 0; r < numResources; ++r)
	{
		const GFXSetResource* res = &resources[r];

		// Check if the resource exists.
		if (
			res->binding >= set->numBindings ||
			res->index >= set->bindings[res->binding].count)
		{
			// Skip it if not.
			gfx_log_warn(
				"Could not set descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") "
				"of a set, does not exist.",
				res->binding, res->index);

			success = 0;
			continue;
		}

		_GFXSetBinding* binding = &set->bindings[res->binding];
		_GFXSetEntry* entry = &binding->entries[res->index];

		// Check if the types match.
		if (
			GFX_REF_IS_NULL(res->ref) ||
			(GFX_REF_IS_BUFFER(res->ref) && !_GFX_BINDING_IS_BUFFER(binding->type)) ||
			(GFX_REF_IS_IMAGE(res->ref) && !_GFX_BINDING_IS_IMAGE(binding->type)))
		{
			gfx_log_warn(
				"Could not set descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") "
				"of a set, incompatible resource type.",
				res->binding, res->index);

			success = 0;
			continue;
		}

		// Check if it is even a different reference.
		// For this we want to unpack the reference, as we want to check the
		// underlying resource, not the top-level reference.
		_GFXUnpackRef cur = _gfx_ref_unpack(entry->ref);
		_GFXUnpackRef new = _gfx_ref_unpack(res->ref);

		// If equal (including offsets), just skip it, not a failure.
		if (_GFX_UNPACK_REF_IS_EQUAL(cur, new) && cur.value == new.value)
			continue;

		// Set the new reference & update.
		entry->ref = res->ref;

		// TODO: Update.
	}

	return success;
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
