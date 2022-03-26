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


// Pushes an lvalue to a hash key being built.
#define _GFX_KEY_PUSH(value) \
	do { \
		if (_gfx_hash_builder_push( \
			&builder, sizeof(value), &(value)) == NULL) \
		{ \
			goto clean_builder; \
		} \
	} while (0)


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
		// If this returns 0, we do not use any update entries,
		// even though binding->count might be > 0!
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

	// At this point we have a fully initialized set, except for its cache key.
	// We first build a key with the empty values, meaning we just push NULL
	// handles and alike a bunch of times.
	// This at least means we're not accidentally hashing valid handles.
	// Note that the key is entirely built in accordance with _gfx_pool_get.
	_GFXHashBuilder builder;
	if (!_gfx_hash_builder(&builder))
		goto clean;

	_GFX_KEY_PUSH(aset->setLayout);

	for (size_t b = 0; b < numBindings; ++b)
	{
		_GFXSetBinding* binding = &aset->bindings[b];
		if (binding->entries == NULL) continue;

		// Firstly, set the key index :)
		binding->keyIndex = _gfx_hash_builder_index(&builder);

		// Then, push update info to the key.
		// Note we just push null values (i.e. empty).
		for (size_t e = 0; e < binding->count; ++e)
		{
			if (_GFX_DESCRIPTOR_IS_BUFFER(binding->type))
			{
				const _GFXBuffer* nullBuffer = NULL;
				const VkDeviceSize nullSize = 0;
				_GFX_KEY_PUSH(nullBuffer);
				_GFX_KEY_PUSH(nullSize);
				_GFX_KEY_PUSH(nullSize);
			}

			else if (_GFX_DESCRIPTOR_IS_VIEW(binding->type))
			{
				// TODO: Come up with key for buffer view.
			}

			else
			{
				// Else it's an image and/or sampler.
				const _GFXCacheElem* nullSampler = NULL;
				const VkImageLayout nullLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				_GFX_KEY_PUSH(nullSampler);
				// TODO: Come up with key for image view.
				_GFX_KEY_PUSH(nullLayout);
			}
		}
	}

	// Claim the key!
	aset->key = _gfx_hash_builder_get(&builder);

	// And finally, before finishing up, set all initial resources, groups,
	// views and samplers. Let individual resources overwrite groups.
	gfx_set_samplers(aset, numSamplers, samplers);
	gfx_set_views(aset, numViews, views);
	gfx_set_groups(aset, numGroups, groups);
	gfx_set_resources(aset, numResources, resources);

	// Link the set into the renderer.
	// Modifying the renderer, lock!
	_gfx_mutex_lock(&renderer->lock);
	gfx_list_insert_after(&renderer->sets, &aset->list, NULL);
	_gfx_mutex_unlock(&renderer->lock);

	return aset;


	// Cleanup on failure.
clean_builder:
	free(_gfx_hash_builder_get(&builder));
clean:
	free(aset);
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
