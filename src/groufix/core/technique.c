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


/****************************
 * Compares two shader resources, ignoring the location and/or set and binding.
 * @return Non-zero if equal.
 */
static inline int _gfx_cmp_resources(const _GFXShaderResource* l,
                                     const _GFXShaderResource* r)
{
	// Do not count attachment inputs.
	const int isImage =
		l->type == _GFX_SHADER_IMAGE_AND_SAMPLER ||
		l->type == _GFX_SHADER_IMAGE_SAMPLED ||
		l->type == _GFX_SHADER_IMAGE_STORAGE;

	return
		l->count == r->count &&
		l->type == r->type &&
		(!isImage || l->viewType == r->viewType);
}

/****************************
 * Finds a _GFXBindingElem in a vector, optionally inserts it at
 * its correct sorted position.
 * @param vec Assumed to be sorted and store _GFXBindingElem.
 * @return Non-zero if it contains the (new) element.
 */
static int _gfx_find_binding_elem(GFXVec* vec, size_t set, size_t binding,
                                  int insert)
{
	// Binary search to its position.
	size_t l = 0;
	size_t r = vec->size;

	while (l < r)
	{
		const size_t p = (l + r) >> 1;
		_GFXBindingElem* e = gfx_vec_at(vec, p);

		const int lesser = e->set < set ||
			(e->set == set && e->binding < binding);
		const int greater = e->set > set ||
			(e->set == set && e->binding > binding);

		if (lesser) l = p + 1;
		else if (greater) r = p;
		else return 1;
	}

	if (insert)
	{
		// Insert anew.
		_GFXBindingElem elem = { .set = set, .binding = binding };
		return gfx_vec_insert(vec, 1, &elem, l);
	}

	return 0;
}

/****************************
 * Retrieves a shader resource from a technique by set/binding number.
 * Unknown what shader will be referenced, technique is assumed to be validated.
 * @return NULL if not present.
 */
static _GFXShaderResource* _gfx_tech_get_resource(GFXTechnique* technique,
                                                  size_t set, size_t binding)
{
	// Loop over all shaders in order (for locality).
	// Then do a binary search for the resource with the given set/binding.
	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		if (technique->shaders[s] != NULL)
		{
			GFXShader* shader = technique->shaders[s];
			size_t l = shader->reflect.locations;
			size_t r = shader->reflect.locations + shader->reflect.bindings;

			while (l < r)
			{
				const size_t p = (l + r) >> 1;
				_GFXShaderResource* res = shader->reflect.resources + p;

				const int lesser = res->set < set ||
					(res->set == set && res->binding < binding);
				const int greater = res->set > set ||
					(res->set == set && res->binding > binding);

				if (lesser) l = p + 1;
				else if (greater) r = p;
				else return shader->reflect.resources + p;
			}
		}

	return NULL;
}

/****************************/
void _gfx_tech_get_set_size(GFXTechnique* technique,
                            size_t set, size_t* numBindings, size_t* numEntries)
{
	assert(technique != NULL);
	assert(technique->layout != NULL); // Must be locked.
	assert(set < technique->numSets);
	assert(numBindings != NULL);
	assert(numEntries != NULL);

	*numBindings = 0;
	*numEntries = 0;

	// Loop over all shaders in order (for locality).
	// Then do a binary search for the right-most resource with the given set.
	// Keep track of this right-most index for the next loop.
	size_t rMost[_GFX_NUM_SHADER_STAGES];

	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		if (technique->shaders[s] != NULL)
		{
			GFXShader* shader = technique->shaders[s];
			size_t l = shader->reflect.locations;
			size_t r = shader->reflect.locations + shader->reflect.bindings;
			rMost[s] = 0; // 0 = include none.

			while (l < r)
			{
				const size_t p = (l + r) >> 1;
				_GFXShaderResource* res = shader->reflect.resources + p;

				if (res->set > set) r = p;
				else l = p + 1;
			}

			// No resource with lesser or equal set.
			if (r == shader->reflect.locations)
				continue;

			// No resource with equal set.
			_GFXShaderResource* rRes = shader->reflect.resources + (r-1);
			if (rRes->set != set)
				continue;

			rMost[s] = r;

			// We want to count empty bindings too,
			// so we can set numBindings to the maximum binding we can find.
			*numBindings =
				GFX_MAX(*numBindings, (size_t)(rRes->binding + 1));
		}

	// We have the number of bindings, but not yet the number of entries.
	// An entry being an actual descriptor within a binding.
	// For this we loop over all shaders again, then loop from the right-most
	// resource to the left and check if we've already counted it.
	// If not, check if is immutable, if not, add its descriptor count.
	unsigned char counted[*numBindings > 0 ? *numBindings : 1];
	memset(counted, 0, *numBindings);

	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		if (technique->shaders[s] != NULL)
		{
			GFXShader* shader = technique->shaders[s];
			for (size_t i = rMost[s]; i > shader->reflect.locations; --i)
			{
				// Stop if we passed the left-most element.
				_GFXShaderResource* res = shader->reflect.resources + (i-1);
				if (res->set != set) break;
				if (counted[res->binding]) continue;

				int isImmutable = _gfx_find_binding_elem(
					&technique->immutable, set, res->binding, 0);

				// Note that we do not need to check the actual resource
				// itself, gfx_tech_samplers will do that for us.
				if (!isImmutable) *numEntries += res->count;
				counted[res->binding] = 1;
			}
		}
}

/****************************/
int _gfx_tech_get_set_binding(GFXTechnique* technique,
                              size_t set, size_t binding, _GFXSetBinding* out)
{
	assert(technique != NULL);
	assert(technique->layout != NULL); // Must be locked.
	assert(set < technique->numSets);
	assert(out != NULL);

	_GFXShaderResource* res = _gfx_tech_get_resource(technique, set, binding);
	if (res == NULL)
	{
		// Empty.
		out->count = 0;
		return 0;
	}

	// Note that gfx_tech_samplers and gfx_tech_dynamic already checked
	// resource compatibility, we can assume they are correct.
	int isImmutable =
		_gfx_find_binding_elem(&technique->immutable, set, binding, 0);
	int isDynamic =
		_gfx_find_binding_elem(&technique->dynamic, set, binding, 0);

	switch (res->type)
	{
	case _GFX_SHADER_BUFFER_UNIFORM:
		out->type = isDynamic ?
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC :
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		break;

	case _GFX_SHADER_BUFFER_STORAGE:
		out->type = isDynamic ?
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC :
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		break;

	case _GFX_SHADER_BUFFER_UNIFORM_TEXEL:
		out->type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		break;

	case _GFX_SHADER_BUFFER_STORAGE_TEXEL:
		out->type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
		break;

	case _GFX_SHADER_IMAGE_AND_SAMPLER:
		out->type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		break;

	case _GFX_SHADER_IMAGE_SAMPLED:
		out->type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		break;

	case _GFX_SHADER_IMAGE_STORAGE:
		out->type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		break;

	case _GFX_SHADER_SAMPLER:
		out->type = VK_DESCRIPTOR_TYPE_SAMPLER;
		break;

	case _GFX_SHADER_ATTACHMENT_INPUT:
		out->type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		break;

	default:
		// Can never be vert/frag io.
		break;
	}

	out->viewType = res->viewType;
	out->count = res->count;

	return !isImmutable;
}

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

		// And if they contain a valid SPIR-V module.
		if (shaders[s]->vk.module == VK_NULL_HANDLE)
		{
			gfx_log_error(
				"All shaders of a technique must contain "
				"valid SPIR-V bytecode.");

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

	// Now that we know the shaders we are going to use,
	// validate all shaders that they are compatible with each other,
	// i.e. all bindings must be equal in all shaders.
	// This is super specific code, but we really want to ALWAYS do this check,
	// if we did not check here, sets would have to check, and we essentially
	// have a stale lingering technique that cannot be used...
	size_t valPos[_GFX_NUM_SHADER_STAGES];
	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s) valPos[s] = 0;

	while (1)
	{
		_GFXShaderResource* cur = NULL;

		// Get resource with lowest set/binding at this iteration.
		for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
			if (shads[s] != NULL && valPos[s] < shads[s]->reflect.bindings)
			{
				_GFXShaderResource* res =
					shads[s]->reflect.resources +
					shads[s]->reflect.locations + valPos[s];

				if (
					cur == NULL || res->set < cur->set ||
					(res->set == cur->set && res->binding < cur->binding))
				{
					cur = res;
				}
			}

		// Done, valid!
		if (cur == NULL) break;

		// Check if all other matching resources of the iteration
		// are compatible (and go to the next resource within that shader).
		for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
			if (shads[s] != NULL && valPos[s] < shads[s]->reflect.bindings)
			{
				_GFXShaderResource* res =
					shads[s]->reflect.resources +
					shads[s]->reflect.locations + valPos[s];

				if (res->set != cur->set || res->binding != cur->binding)
					continue;

				if (!_gfx_cmp_resources(res, cur))
				{
					gfx_log_error(
						"Shaders have incompatible descriptor resources "
						"(set=%"PRIu32", binding=%"PRIu32"), could not "
						"add a new technique to a renderer.",
						res->set, res->binding);

					return NULL;
				}

				// If matched, go to next.
				++valPos[s];
			}
	}

	// We need the number of descriptor set layouts to store.
	// Luckily we need to create empty set layouts for missing set numbers.
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
		sizeof(_GFXCacheElem*) * (maxSet + 1));

	if (tech == NULL)
		goto error;

	// Initialize the technique.
	tech->renderer = renderer;
	tech->numSets = (size_t)(maxSet + 1);
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


	// Error on failure.
error:
	gfx_log_error("Could not add a new technique to a renderer.");
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

	// Keep track of success, not gonna bother removing
	// previous samplers if one failed to insert.
	int success = 1;

	for (size_t s = 0; s < numSamplers; ++s)
	{
		// Check if we can set a sampler to this resource.
		_GFXShaderResource* res =
			_gfx_tech_get_resource(technique, set, samplers[s].binding);

		if (res == NULL ||
			(res->type != _GFX_SHADER_IMAGE_AND_SAMPLER &&
			res->type != _GFX_SHADER_SAMPLER))
		{
			// Skip it if not.
			gfx_log_warn(
				"Could not set sampler of descriptor resource "
				"(set=%"GFX_PRIs", binding=%"GFX_PRIs") of a technique, "
				"not a sampler.",
				set, samplers[s].binding);

			success = 0;
			continue;
		}

		// Binary search to the samplers position.
		_GFXSamplerElem elem = { .set = set, .sampler = samplers[s] };
		size_t l = 0;
		size_t r = technique->samplers.size;

		while (l < r)
		{
			const size_t p = (l + r) >> 1;
			_GFXSamplerElem* e = gfx_vec_at(&technique->samplers, p);

			const int lesser = e->set < set ||
				(e->set == set &&
				(e->sampler.binding < samplers[s].binding ||
					(e->sampler.binding == samplers[s].binding &&
					e->sampler.index < samplers[s].index)));
			const int greater = e->set > set ||
				(e->set == set &&
				(e->sampler.binding > samplers[s].binding ||
					(e->sampler.binding == samplers[s].binding &&
					e->sampler.index > samplers[s].index)));

			if (lesser) l = p + 1;
			else if (greater) r = p;
			else {
				// Overwrite if found.
				*e = elem;
				break;
			}
		}

		// Insert anew if not found.
		if (l == r)
		{
			if (!gfx_vec_insert(&technique->samplers, 1, &elem, l))
			{
				// Just continue to the next on failure.
				success = 0;
				continue;
			}

			// And insert a binding element to make it immutable.
			if (!_gfx_find_binding_elem(
				&technique->immutable, set, samplers[s].binding, 1))
			{
				// Erase the sampler altogether on failure.
				gfx_vec_erase(&technique->samplers, 1, l);
				success = 0;
				continue;
			}
		}
	}

	return success;
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

	// Check if we can make this resource dynamic.
	_GFXShaderResource* res =
		_gfx_tech_get_resource(technique, set, binding);

	if (res == NULL ||
		(res->type != _GFX_SHADER_BUFFER_UNIFORM &&
		res->type != _GFX_SHADER_BUFFER_STORAGE))
	{
		// Nop.
		gfx_log_warn(
			"Could not set a dynamic descriptor resource "
			"(set=%"GFX_PRIs", binding=%"GFX_PRIs") of a technique, "
			"not a uniform or storage buffer.",
			set, binding);

		return 0;
	}

	// Insert the binding element.
	return _gfx_find_binding_elem(&technique->dynamic, set, binding, 1);
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
