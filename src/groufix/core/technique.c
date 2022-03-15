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
 * Technique binding element (immutable/dynamic) definition.
 */
typedef struct _GFXBindingElem
{
	size_t set;
	size_t binding;

} _GFXBindingElem;


/****************************/
GFX_API GFXTechnique* gfx_renderer_add_tech(GFXRenderer* renderer,
                                            size_t numShaders, GFXShader** shaders)
{
	assert(renderer != NULL);
	assert(!renderer->recording);
	assert(numShaders > 0);
	assert(shaders != NULL);

	// Get the array of shaders to use.
	// Use the last shader of each stage.
	GFXShader* shads[_GFX_NUM_SHADER_STAGES];
	int compute = 0;

	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		shads[s] = NULL; // Init all to empty.

	for (size_t s = 0; s < numShaders; ++s)
	{
		// Validate context while we're at it.
		if (shaders[s]->context != renderer->allocator.context)
		{
			gfx_log_error(
				"All shaders of a technique must be built on the same "
				"logical Vulkan device as its renderer.");

			return NULL;
		}

		// Must yield a valid index for all shaders (!).
		shads[_GFX_GET_SHADER_STAGE_INDEX(shaders[s]->stage)] = shaders[s];
		if (shaders[s]->stage == GFX_STAGE_COMPUTE) compute = 1;
	}

	// No compute or only compute.
	if (compute) for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		if (
			s != _GFX_GET_SHADER_STAGE_INDEX(GFX_STAGE_COMPUTE) &&
			shads[s] != NULL)
		{
			gfx_log_error(
				"A technique cannot have a compute shader in combination "
				"with shaders of a different stage.");

			return NULL;
		}

	// We need the number of descriptor set layouts to store.
	// Luckily we actually need to create empty sets for missing set numbers.
	// Plus shader resources are sorted, so we just get the maximum of all :)
	// Also get the push constant size/stages while we're at it.
	uint32_t maxSet = 0;
	uint32_t pushSize = 0;
	GFXShaderStage pushStages = 0;

	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		if (shads[s] != NULL)
		{
			if (shads[s]->reflect.bindings > 0)
				maxSet = GFX_MAX(maxSet, shads[s]->reflect.resources[
					shads[s]->reflect.locations +
					shads[s]->reflect.bindings - 1].set);

			if (shads[s]->reflect.push > 0)
				pushSize = GFX_MAX(pushSize, shads[s]->reflect.push),
				pushStages |= shads[s]->stage;
		}

	// Allocate a new technique.
	// We allocate set layouts at the tail of the technique,
	// make sure to adhere to its alignment requirements!
	const size_t structSize = GFX_ALIGN_UP(
		sizeof(GFXTechnique) + sizeof(GFXShader*) * _GFX_NUM_SHADER_STAGES,
		_Alignof(_GFXCacheElem*));

	GFXTechnique* tech = malloc(
		structSize +
		sizeof(_GFXCacheElem*) * maxSet);

	if (tech == NULL)
		goto clean;

	// Initialize the technique.
	tech->renderer = renderer;
	tech->numSets = (size_t)maxSet;
	tech->setLayouts = (_GFXCacheElem**)((char*)tech + structSize);
	tech->layout = NULL;
	tech->pushSize = pushSize;
	tech->pushStages = pushStages;
	memcpy(tech->shaders, shads, sizeof(shads));

	for (size_t l = 0; l < tech->numSets; ++l)
		tech->setLayouts[l] = NULL;

	gfx_vec_init(&tech->samplers, sizeof(_GFXSamplerElem));
	gfx_vec_init(&tech->immutable, sizeof(_GFXBindingElem));
	gfx_vec_init(&tech->dynamic, sizeof(_GFXBindingElem));

	// Link the technique into the renderer.
	gfx_list_insert_after(&renderer->techniques, &tech->list, NULL);

	return tech;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not add a new technique to a renderer.");
	free(tech);

	return NULL;
}

/****************************/
GFX_API void gfx_erase_tech(GFXTechnique* technique)
{
	assert(technique != NULL);
	assert(!technique->renderer->recording);

	// Unlink itself from the renderer.
	gfx_list_erase(&technique->renderer->techniques, &technique->list);

	// Destroy itself.
	gfx_vec_clear(&technique->samplers);
	gfx_vec_clear(&technique->immutable);
	gfx_vec_clear(&technique->dynamic);

	free(technique);
}

/****************************/
GFX_API size_t gfx_tech_get_num_sets(GFXTechnique* technique)
{
	assert(technique != NULL);

	return technique->numSets;
}

/****************************/
GFX_API int gfx_tech_samplers(GFXTechnique* technique, size_t set,
                              size_t numSamplers, const GFXSampler* samplers)
{
	assert(technique != NULL);
	assert(!technique->renderer->recording);
	assert(set < technique->numSets);
	assert(numSamplers > 0);
	assert(samplers != NULL);

	// Skip if already locked.
	if (technique->layout != NULL)
		return 0;

	// TODO: Implement.

	return 0;
}

/****************************/
GFX_API int gfx_tech_dynamic(GFXTechnique* technique, size_t set,
                             size_t binding)
{
	assert(technique != NULL);
	assert(!technique->renderer->recording);
	assert(set < technique->numSets);

	// Skip if already locked.
	if (technique->layout != NULL)
		return 0;

	// TODO: Implement.

	return 0;
}

/****************************/
GFX_API int gfx_tech_lock(GFXTechnique* technique)
{
	assert(technique != NULL);
	assert(!technique->renderer->recording);

	// Already locked.
	if (technique->layout != NULL)
		return 1;

	// TODO: Implement.

	return 0;
}
