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


#define _GFX_GET_VK_FILTER(filter) \
	(((filter) == GFX_FILTER_NEAREST) ? VK_FILTER_NEAREST : \
	((filter) == GFX_FILTER_LINEAR) ? VK_FILTER_LINEAR : \
	VK_FILTER_NEAREST)

#define _GFX_GET_VK_MIPMAP_MODE(filter) \
	(((filter) == GFX_FILTER_NEAREST) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : \
	((filter) == GFX_FILTER_LINEAR) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : \
	VK_SAMPLER_MIPMAP_MODE_NEAREST)

#define _GFX_GET_VK_REDUCTION_MODE(mode) \
	((mode) == GFX_FILTER_MODE_AVERAGE ? \
		VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE : \
	(mode) == GFX_FILTER_MODE_MIN ? \
		VK_SAMPLER_REDUCTION_MODE_MIN : \
	(mode) == GFX_FILTER_MODE_MAX ? \
		VK_SAMPLER_REDUCTION_MODE_MAX : \
		VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE)

#define _GFX_GET_VK_ADDRESS_MODE(wrap) \
	((wrap) == GFX_WRAP_REPEAT ? \
		VK_SAMPLER_ADDRESS_MODE_REPEAT : \
	(wrap) == GFX_WRAP_REPEAT_MIRROR ? \
		VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : \
	(wrap) == GFX_WRAP_CLAMP_TO_EDGE ? \
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : \
	(wrap) == GFX_WRAP_CLAMP_TO_EDGE_MIRROR ? \
		VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : \
	(wrap) == GFX_WRAP_CLAMP_TO_BORDER ? \
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : \
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)

#define _GFX_GET_VK_COMPARE_OP(op) \
	(((op) == GFX_CMP_NEVER) ?  VK_COMPARE_OP_NEVER : \
	((op) == GFX_CMP_LESS) ? VK_COMPARE_OP_LESS : \
	((op) == GFX_CMP_LESS_EQUAL) ? VK_COMPARE_OP_LESS_OR_EQUAL : \
	((op) == GFX_CMP_GREATER) ? VK_COMPARE_OP_GREATER : \
	((op) == GFX_CMP_GREATER_EQUAL) ? VK_COMPARE_OP_GREATER_OR_EQUAL : \
	((op) == GFX_CMP_EQUAL) ? VK_COMPARE_OP_EQUAL : \
	((op) == GFX_CMP_NOT_EQUAL) ? VK_COMPARE_OP_NOT_EQUAL : \
	((op) == GFX_CMP_ALWAYS) ? VK_COMPARE_OP_ALWAYS : \
	VK_COMPARE_OP_ALWAYS)


/****************************/
_GFXCacheElem* _gfx_get_sampler(GFXRenderer* renderer,
                                const GFXSampler* sampler)
{
	assert(renderer != NULL);

	// Define some defaults.
	VkSamplerReductionModeCreateInfo srmci = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
		.pNext = NULL,
		.reductionMode = VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE
	};

	VkSamplerCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,

		.pNext            = NULL,
		.flags            = 0,
		.magFilter        = VK_FILTER_NEAREST,
		.minFilter        = VK_FILTER_NEAREST,
		.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.mipLodBias       = 0.0f,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy    = 1.0f,
		.compareEnable    = VK_FALSE,
		.compareOp        = VK_COMPARE_OP_ALWAYS,
		.minLod           = 0.0f,
		.maxLod           = 1.0f,
		.borderColor      = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,

		.unnormalizedCoordinates = VK_FALSE
	};

	// Set given sampler values.
	if (sampler != NULL)
	{
		// Filter out reduction mode, anisotropy, compare and unnormalized
		// coordinates if they are not enabled.
		// This makes it so when disabled, key values in the cache will be
		// equivalent (!).
		if (sampler->mode != GFX_FILTER_MODE_AVERAGE)
		{
			srmci.pNext = &srmci;
			srmci.reductionMode = _GFX_GET_VK_REDUCTION_MODE(sampler->mode);
		}

		if (sampler->flags & GFX_SAMPLER_ANISOTROPY)
		{
			sci.anisotropyEnable = VK_TRUE;
			sci.maxAnisotropy = sampler->maxAnisotropy;
		}

		if (sampler->flags & GFX_SAMPLER_COMPARE)
		{
			sci.compareEnable = VK_TRUE;
			sci.compareOp = _GFX_GET_VK_COMPARE_OP(sampler->cmp);
		}

		if (sampler->flags & GFX_SAMPLER_UNNORMALIZED)
			sci.unnormalizedCoordinates = VK_TRUE;

		sci.magFilter     = _GFX_GET_VK_FILTER(sampler->magFilter);
		sci.minFilter     = _GFX_GET_VK_FILTER(sampler->minFilter);
		sci.mipmapMode    = _GFX_GET_VK_MIPMAP_MODE(sampler->mipFilter);
		sci.addressModeU  = _GFX_GET_VK_ADDRESS_MODE(sampler->wrapU);
		sci.addressModeV  = _GFX_GET_VK_ADDRESS_MODE(sampler->wrapV);
		sci.addressModeW  = _GFX_GET_VK_ADDRESS_MODE(sampler->wrapW);
		sci.mipLodBias    = sampler->mipLodBias;
		sci.minLod        = sampler->minLod;
		sci.maxLod        = sampler->maxLod;
	}

	// Create an actual sampler object.
	return _gfx_cache_warmup(&renderer->cache, &sci.sType, NULL);
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
		}
	}

	// Link the set into the renderer.
	gfx_list_insert_after(&renderer->sets, &aset->list, NULL);

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

	// Unlink itself from the renderer.
	gfx_list_erase(&set->renderer->sets, &set->list);

	// Recycle all matching descriptor sets.
	if (atomic_load(&set->used))
		_gfx_pool_recycle(&set->renderer->pool, set->key);

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
