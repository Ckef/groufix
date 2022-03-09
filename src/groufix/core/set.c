/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


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
	assert(renderer->pFrame.vk.done == VK_NULL_HANDLE);
	assert(technique != NULL);
	assert(technique->renderer == renderer);
	assert(set < technique->numSets);
	assert(numResources == 0 || resources != NULL);
	assert(numGroups == 0 || groups != NULL);
	assert(numViews == 0 || views != NULL);
	assert(numSamplers == 0 || samplers != NULL);

	// TODO: Implement.

	return NULL;
}

/****************************/
GFX_API void gfx_erase_set(GFXSet* set)
{
	assert(set != NULL);
	assert(set->renderer->pFrame.vk.done == VK_NULL_HANDLE);

	// TODO: Implement.
}

/****************************/
GFX_API int gfx_set_resources(GFXSet* set,
                              size_t numResources, const GFXSetResource* resources)
{
	assert(set != NULL);
	assert(set->renderer->pFrame.vk.done == VK_NULL_HANDLE);
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
	assert(set->renderer->pFrame.vk.done == VK_NULL_HANDLE);
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
	assert(set->renderer->pFrame.vk.done == VK_NULL_HANDLE);
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
	assert(set->renderer->pFrame.vk.done == VK_NULL_HANDLE);
	assert(numSamplers > 0);
	assert(samplers != NULL);

	// TODO: Implement.

	return 0;
}
