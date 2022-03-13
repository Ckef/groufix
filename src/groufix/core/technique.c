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

	// TODO: Validate shader contexts.
	// TODO: Validate shader input.
	// TODO: Compute a compacted array of shaders to use below.

	// Count the number of descriptor set layouts to store.
	// Do this by looping over the resources of all shaders in lockstep.
	size_t numSets = 0;
	uint32_t curSet = UINT32_MAX;
	size_t curBind[numShaders > 0 ? numShaders : 1];

	for (size_t s = 0; s < numShaders; ++s)
		curBind[s] = 0,
		// Make sure the first set is counted.
		numSets |= shaders[s]->reflect.bindings > 0 ? 1 : 0,
		// Get lowest first set.
		curSet = shaders[s]->reflect.bindings > 0 ? GFX_MIN(curSet,
			shaders[s]->reflect.resources[
				shaders[s]->reflect.locations].set) : curSet;

	while (1)
	{
		// For the current set, for each shader:
		// loop over its resources until we hit current set + 1.
		// Take the lowest next set as next to explore.
		int loop = 0;
		uint32_t nextSet = UINT32_MAX;

		for (size_t s = 0; s < numShaders; ++s)
		{
			size_t ind = shaders[s]->reflect.locations + curBind[s];
			while (
				curBind[s] < shaders[s]->reflect.bindings &&
				shaders[s]->reflect.resources[ind].set <= curSet)
			{
				++ind;
				++curBind[s];
			}

			if (curBind[s] < shaders[s]->reflect.bindings)
				loop = 1,
				nextSet = GFX_MIN(nextSet,
					shaders[s]->reflect.resources[ind].set);
		}

		if (!loop) break;

		// Loop to next set to explore.
		curSet = nextSet;
		++numSets;
	}

	// Ok we know how many set layouts to create.
	// Allocate a new technique.
	// We allocate set layouts at the tail of the technique,
	// make sure to adhere to its alignment requirements!
	const size_t structSize = GFX_ALIGN_UP(
		sizeof(GFXTechnique) + sizeof(GFXShader*) * _GFX_NUM_SHADER_STAGES,
		_Alignof(_GFXCacheElem*));

	GFXTechnique* tech = malloc(
		structSize +
		sizeof(_GFXCacheElem*) * numSets);

	if (tech == NULL)
		goto clean;

	// Initialize the technique.
	tech->renderer = renderer;
	tech->numSets = numSets;
	tech->setLayouts = (_GFXCacheElem**)((char*)tech + structSize);
	tech->layout = NULL;

	for (size_t l = 0; l < numSets; ++l)
		tech->setLayouts[l] = NULL;

	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		tech->shaders[s] = NULL;

	gfx_vec_init(&tech->samplers, sizeof(_GFXSamplerElem));
	gfx_vec_init(&tech->immutable, sizeof(_GFXBindingElem));
	gfx_vec_init(&tech->dynamic, sizeof(_GFXBindingElem));

	// TODO: Get pushSize/pushStages.
	// TODO: Fill the shaders array.

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
