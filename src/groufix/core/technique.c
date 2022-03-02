/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


// #shaders a technique can hold.
#define _GFX_NUM_SHADER_STAGES 6

// Get index from shader stage.
#define _GFX_GET_SHADER_STAGE_INDEX(stage) \
	(((stage) == GFX_STAGE_VERTEX) ? 0 : \
	((stage) == GFX_STAGE_TESS_CONTROL) ? 1 : \
	((stage) == GFX_STAGE_TESS_EVALUATION) ? 2 : \
	((stage) == GFX_STAGE_GEOMETRY) ? 3 : \
	((stage) == GFX_STAGE_FRAGMENT) ? 4 : \
	((stage) == GFX_STAGE_COMPUTE) ? 5 : _GFX_NUM_SHADER_STAGES)


/****************************
 * Technique immutable sampler element definition.
 */
typedef struct _GFXSamplerElem
{
	size_t     set;
	GFXSampler sampler;

} _GFXSamplerElem;



/****************************
 * Technique dynamic element definition.
 */
typedef struct _GFXDynamicElem
{
	size_t set;
	size_t binding;

} _GFXDynamicElem;


/****************************/
GFX_API GFXTechnique* gfx_renderer_add_tech(GFXRenderer* renderer,
                                            size_t numShaders, GFXShader** shaders)
{
	assert(renderer != NULL);
	assert(renderer->pFrame.vk.done == VK_NULL_HANDLE);
	assert(numShaders > 0);
	assert(shaders != NULL);

	// TODO: Implement.

	return NULL;
}

/****************************/
GFX_API void gfx_erase_tech(GFXTechnique* technique)
{
	assert(technique != NULL);
	assert(technique->renderer->pFrame.vk.done == VK_NULL_HANDLE);

	// TODO: Implement.
}

/****************************/
GFX_API void gfx_tech_set_samplers(GFXTechnique* technique, size_t set,
                                   size_t numSamplers, const GFXSampler* samplers)
{
	assert(technique != NULL);
	assert(technique->renderer->pFrame.vk.done == VK_NULL_HANDLE);
	assert(numSamplers > 0);
	assert(samplers != NULL);

	// Skip if already built.
	if (technique->layout != NULL)
		return;

	// TODO: Implement.
}

/****************************/
GFX_API void gfx_tech_set_dynamic(GFXTechnique* technique, size_t set,
                                  size_t binding)
{
	assert(technique != NULL);
	assert(technique->renderer->pFrame.vk.done == VK_NULL_HANDLE);

	// Skip if already built.
	if (technique->layout != NULL)
		return;

	// TODO: Implement.
}
