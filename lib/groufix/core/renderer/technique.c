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


// Get Vulkan descriptor type.
#define _GFX_GET_VK_DESCRIPTOR_TYPE(type, dynamic) \
	((type) == _GFX_SHADER_BUFFER_UNIFORM ? (dynamic ? \
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : \
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) : \
	(type) == _GFX_SHADER_BUFFER_STORAGE ? (dynamic ? \
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : \
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) : \
	(type) == _GFX_SHADER_BUFFER_UNIFORM_TEXEL ? \
		VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER : \
	(type) == _GFX_SHADER_BUFFER_STORAGE_TEXEL ? \
		VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : \
	(type) == _GFX_SHADER_IMAGE_AND_SAMPLER ? \
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : \
	(type) == _GFX_SHADER_IMAGE_SAMPLED ? \
		VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : \
	(type) == _GFX_SHADER_IMAGE_STORAGE ? \
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : \
	(type) == _GFX_SHADER_SAMPLER ? \
		VK_DESCRIPTOR_TYPE_SAMPLER : \
	(type) == _GFX_SHADER_ATTACHMENT_INPUT ? \
		VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT : \
		0) /* Should not happen. */


/****************************
 * Technique constant element definition.
 */
typedef struct _GFXConstantElem
{
	uint32_t    stage; // Shader stage index.
	uint32_t    id;
	size_t      size;
	GFXConstant value;

} _GFXConstantElem;


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
 * Compares two shader resources, ignoring the location/set/id and binding.
 * @return Non-zero if equal.
 */
static inline bool _gfx_cmp_resources(const _GFXShaderResource* l,
                                      const _GFXShaderResource* r)
{
	// Do not count attachment inputs.
	const bool isImage =
		l->type == _GFX_SHADER_IMAGE_AND_SAMPLER ||
		l->type == _GFX_SHADER_IMAGE_SAMPLED ||
		l->type == _GFX_SHADER_IMAGE_STORAGE;

	return
		l->count == r->count &&
		l->size == r->size &&
		l->type == r->type &&
		(!isImage || l->viewType == r->viewType);
}

/****************************
 * Finds a _GFXSamplerElem in a vector, optionally inserts it at
 * its correct sorted position.
 * @param vec Assumed to be sorted and store _GFXSamplerElem.
 * @return Index of the (new) element, SIZE_MAX on failure.
 */
static size_t _gfx_find_sampler_elem(GFXVec* vec,
                                     size_t set, size_t binding, size_t index,
                                     bool insert)
{
	// Binary search to its position.
	size_t l = 0;
	size_t r = vec->size;

	while (l < r)
	{
		const size_t p = (l + r) >> 1;
		_GFXSamplerElem* e = gfx_vec_at(vec, p);

		const bool lesser = e->set < set ||
			(e->set == set &&
			(e->sampler.binding < binding ||
				(e->sampler.binding == binding &&
				e->sampler.index < index)));

		const bool greater = e->set > set ||
			(e->set == set &&
			(e->sampler.binding > binding ||
				(e->sampler.binding == binding &&
				e->sampler.index > index)));

		if (lesser) l = p + 1;
		else if (greater) r = p;
		else return p;
	}

	// Insert anew.
	if (insert && gfx_vec_insert(vec, 1, NULL, l))
	{
		_GFXSamplerElem* e = gfx_vec_at(vec, l);
		e->set = set;
		e->sampler.binding = binding;
		e->sampler.index = index;

		return l;
	}

	return SIZE_MAX;
}

/****************************
 * Finds a _GFXBindingElem in a vector, optionally inserts it at
 * its correct sorted position.
 * @param vec Assumed to be sorted and store _GFXBindingElem.
 * @return Non-zero if it contains the (new) element.
 */
static bool _gfx_find_binding_elem(GFXVec* vec, size_t set, size_t binding,
                                   bool insert)
{
	// Binary search to its position.
	size_t l = 0;
	size_t r = vec->size;

	while (l < r)
	{
		const size_t p = (l + r) >> 1;
		_GFXBindingElem* e = gfx_vec_at(vec, p);

		const bool lesser = e->set < set ||
			(e->set == set && e->binding < binding);
		const bool greater = e->set > set ||
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

				const bool lesser = res->set < set ||
					(res->set == set && res->binding < binding);
				const bool greater = res->set > set ||
					(res->set == set && res->binding > binding);

				if (lesser) l = p + 1;
				else if (greater) r = p;
				else return shader->reflect.resources + p;
			}
		}

	return NULL;
}

/****************************/
void _gfx_tech_get_constants(GFXTechnique* technique,
                             VkSpecializationInfo* infos,
                             VkSpecializationMapEntry* entries)
{
	assert(technique != NULL);
	assert(technique->layout != NULL); // Must be locked.
	assert(infos != NULL);
	assert(technique->constants.size == 0 || entries != NULL);

	// Init info structs to empty.
	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		infos[s] = (VkSpecializationInfo){
			.mapEntryCount = 0,
			.pMapEntries = NULL,
			.dataSize = 0,
			.pData = NULL
		};

	// No constants, done.
	if (technique->constants.size == 0) return;

	// Loop over all constants, count & output them;
	// They are already sorted correctly.
	uint32_t currStage = UINT32_MAX;
	uint32_t currOffset = 0;

	for (size_t c = 0; c < technique->constants.size; ++c)
	{
		_GFXConstantElem* elem = gfx_vec_at(&technique->constants, c);
		infos[elem->stage].mapEntryCount += 1;
		infos[elem->stage].dataSize += sizeof(_GFXConstantElem);

		// If we hit a new stage, set the map entry & data pointers.
		if (elem->stage != currStage)
		{
			currStage = elem->stage;
			currOffset = 0;

			infos[elem->stage].pMapEntries = entries + c;
			infos[elem->stage].pData =
				// Point to the first value of the stage.
				(char*)elem + offsetof(_GFXConstantElem, value);

			// Also chop off the first bit of the first constant elem.
			infos[elem->stage].dataSize -= offsetof(_GFXConstantElem, value);
		}

		// Output the map entry.
		entries[c] = (VkSpecializationMapEntry){
			.constantID = elem->id,
			.offset = currOffset,
			.size = elem->size
		};

		// Increase offset for next entry.
		currOffset += (uint32_t)sizeof(_GFXConstantElem);
	}
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
	// If not, check if it is immutable, if not, add its descriptor count.
	unsigned char counted[GFX_MAX(1, *numBindings)];
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

				const bool isImmutable = _gfx_find_binding_elem(
					&technique->immutable, set, res->binding, 0);

				// Note that we also check if the resource contains more
				// than just an immutable sampler.
				if (!isImmutable || res->type != _GFX_SHADER_SAMPLER)
					*numEntries += res->count;

				counted[res->binding] = 1;
			}
		}
}

/****************************/
bool _gfx_tech_get_set_binding(GFXTechnique* technique,
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
		out->size = 0;
		return 0;
	}

	// Note that gfx_tech_samplers and gfx_tech_dynamic already checked
	// resource compatibility, we can assume they are correct.
	const bool isImmutable =
		_gfx_find_binding_elem(&technique->immutable, set, binding, 0);
	const bool isDynamic =
		_gfx_find_binding_elem(&technique->dynamic, set, binding, 0);

	out->type = _GFX_GET_VK_DESCRIPTOR_TYPE(res->type, isDynamic);
	out->viewType = res->viewType;
	out->count = res->count;
	out->size = res->size;

	// Just as above, check if it contains more than an immutable sampler.
	return !isImmutable || res->type != _GFX_SHADER_SAMPLER;
}

/****************************/
GFX_API GFXTechnique* gfx_renderer_add_tech(GFXRenderer* renderer,
                                            size_t numShaders, GFXShader** shaders)
{
	assert(renderer != NULL);
	assert(numShaders > 0);
	assert(shaders != NULL);

	// Get the array of shaders to use.
	// Use the last shader of each stage.
	GFXShader* shads[_GFX_NUM_SHADER_STAGES];
	bool compute = 0;
	bool nonCompute = 0;

	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		shads[s] = NULL; // Init all to empty.

	for (size_t s = 0; s < numShaders; ++s)
	{
		// Validate context while we're at it.
		if (shaders[s]->context != renderer->cache.context)
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
		else nonCompute = 1;
	}

	// No compute or only compute.
	if (compute && nonCompute)
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

	// Also keep track of max #sets and #bindings.
	// We need to create empty set layouts for missing set numbers AND
	// we want to count empty bindings too, so just get the maximum of all :)
	size_t maxSets = 0;
	size_t maxBindings = 0;

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

		// Keep track of max #sets and #bindings.
		maxSets =
			GFX_MAX(maxSets, (size_t)(cur->set + 1));
		maxBindings =
			GFX_MAX(maxBindings, (size_t)(cur->binding + 1));
	}

	// Allocate a new technique.
	GFXTechnique* tech = malloc(
		sizeof(GFXTechnique) +
		sizeof(_GFXCacheElem*) * maxSets);

	if (tech == NULL)
		goto error;

	// Initialize the technique.
	tech->renderer = renderer;
	tech->numSets = maxSets;
	tech->maxBindings = maxBindings;
	tech->pushSize = 0;
	tech->pushStages = 0;
	tech->layout = NULL;
	tech->vk.layout = VK_NULL_HANDLE;
	memcpy(tech->shaders, shads, sizeof(shads));

	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		if (shads[s] != NULL && shads[s]->reflect.push > 0)
			tech->pushSize = GFX_MAX(tech->pushSize, shads[s]->reflect.push),
			tech->pushStages |= shads[s]->stage;

	for (size_t l = 0; l < tech->numSets; ++l)
		tech->setLayouts[l] = NULL;

	gfx_vec_init(&tech->constants, sizeof(_GFXConstantElem));
	gfx_vec_init(&tech->samplers, sizeof(_GFXSamplerElem));
	gfx_vec_init(&tech->immutable, sizeof(_GFXBindingElem));
	gfx_vec_init(&tech->dynamic, sizeof(_GFXBindingElem));

	// Link the technique into the renderer.
	// Modifying the renderer, lock!
	_gfx_mutex_lock(&renderer->lock);
	gfx_list_insert_after(&renderer->techniques, &tech->list, NULL);
	_gfx_mutex_unlock(&renderer->lock);

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

	GFXRenderer* renderer = technique->renderer;

	// Unlink itself from the renderer.
	// Modifying the renderer, lock!
	_gfx_mutex_lock(&renderer->lock);
	gfx_list_erase(&renderer->techniques, &technique->list);
	_gfx_mutex_unlock(&renderer->lock);

	// Destroy itself.
	gfx_vec_clear(&technique->constants);
	gfx_vec_clear(&technique->samplers);
	gfx_vec_clear(&technique->immutable);
	gfx_vec_clear(&technique->dynamic);

	free(technique);
}

/****************************/
GFX_API GFXRenderer* gfx_tech_get_renderer(GFXTechnique* technique)
{
	assert(technique != NULL);

	return technique->renderer;
}

/****************************/
GFX_API GFXShader* gfx_tech_get_shader(GFXTechnique* technique, GFXShaderStage stage)
{
	assert(technique != NULL);
	assert(stage != GFX_STAGE_ANY);
	assert(GFX_IS_POWER_OF_TWO(stage)); // Only 1 stage can be set.

	return technique->shaders[_GFX_GET_SHADER_STAGE_INDEX(stage)];
}

/****************************/
GFX_API size_t gfx_tech_get_num_sets(GFXTechnique* technique)
{
	assert(technique != NULL);

	return technique->numSets;
}

/****************************/
GFX_API size_t gfx_tech_get_max_bindings(GFXTechnique* technique)
{
	assert(technique != NULL);

	return technique->maxBindings;
}

/****************************/
GFX_API GFXShaderResourceType gfx_tech_get_resource_type(GFXTechnique* technique,
                                                         size_t set, size_t binding)
{
	assert(technique != NULL);

	_GFXShaderResource* res =
		_gfx_tech_get_resource(technique, set, binding);

	if (res == NULL)
		return GFX_RESOURCE_UNKNOWN;

	switch (res->type)
	{
	case _GFX_SHADER_BUFFER_UNIFORM:
		return GFX_RESOURCE_BUFFER_UNIFORM;
	case _GFX_SHADER_BUFFER_STORAGE:
		return GFX_RESOURCE_BUFFER_STORAGE;
	case _GFX_SHADER_BUFFER_UNIFORM_TEXEL:
		return GFX_RESOURCE_BUFFER_UNIFORM_TEXEL;
	case _GFX_SHADER_BUFFER_STORAGE_TEXEL:
		return GFX_RESOURCE_BUFFER_STORAGE_TEXEL;
	case _GFX_SHADER_IMAGE_AND_SAMPLER:
		return GFX_RESOURCE_IMAGE_AND_SAMPLER;
	case _GFX_SHADER_IMAGE_SAMPLED:
		return GFX_RESOURCE_IMAGE_SAMPLED;
	case _GFX_SHADER_IMAGE_STORAGE:
		return GFX_RESOURCE_IMAGE_STORAGE;
	case _GFX_SHADER_ATTACHMENT_INPUT:
		return GFX_RESOURCE_IMAGE_ATTACHMENT;
	case _GFX_SHADER_SAMPLER:
		return GFX_RESOURCE_SAMPLER;

	default:
		return GFX_RESOURCE_UNKNOWN;
	}
}

/****************************/
GFX_API size_t gfx_tech_get_binding_size(GFXTechnique* technique,
                                         size_t set, size_t binding)
{
	assert(technique != NULL);

	_GFXShaderResource* res =
		_gfx_tech_get_resource(technique, set, binding);

	return (res == NULL) ? 0 : res->count;
}

/****************************/
GFX_API size_t gfx_tech_get_binding_block_size(GFXTechnique* technique,
                                               size_t set, size_t binding)
{
	assert(technique != NULL);

	_GFXShaderResource* res =
		_gfx_tech_get_resource(technique, set, binding);

	return (res == NULL) ? 0 : res->size;
}

/****************************/
GFX_API uint32_t gfx_tech_get_push_size(GFXTechnique* technique)
{
	assert(technique != NULL);

	return technique->pushSize;
}

/****************************/
GFX_API GFXShaderStage gfx_tech_get_push_stages(GFXTechnique* technique)
{
	assert(technique != NULL);

	return technique->pushStages;
}

/****************************/
GFX_API bool gfx_tech_constant(GFXTechnique* technique,
                               uint32_t id, GFXShaderStage stage,
                               size_t size, GFXConstant value)
{
	assert(technique != NULL);
	assert(stage != 0);
	assert(
		size == sizeof(value.i32) ||
		size == sizeof(value.u32) ||
		size == sizeof(value.f));

	// Skip if already locked.
	if (technique->layout != NULL)
		return 0;

	// Keep track of success.
	bool success = 1;

	// Loop over all shader stages, set constants.
	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		// Cheat a little by fabricating each bit-flag from stage index.
		if ((stage & ((uint32_t)1 << s)) && technique->shaders[s] != NULL)
		{
			GFXShader* shader = technique->shaders[s];

			// Check if this shader even has the constant ID.
			size_t i;
			for (i = shader->reflect.constants; i > 0; --i)
			{
				_GFXShaderResource* res =
					shader->reflect.resources +
					shader->reflect.locations +
					shader->reflect.bindings + (i-1);

				if (res->id == id) break;
			}

			// Silently skip it if not.
			if (i == 0) continue;

			// If it does, insert/update the specialization constant.
			// Do another binary search to find it, then insert if not found.
			size_t l = 0;
			size_t r = technique->constants.size;

			while (l < r)
			{
				const size_t p = (l + r) >> 1;
				_GFXConstantElem* e = gfx_vec_at(&technique->constants, p);

				const bool lesser =
					e->stage < s || (e->stage == s && e->id < id);
				const bool greater =
					e->stage > s || (e->stage == s && e->id > id);

				if (lesser) l = p + 1;
				else if (greater) r = p;
				else {
					// If found, just update.
					e->size = size;
					e->value = value;
					break;
				}
			}

			// Insert if not found.
			if (l == r)
			{
				_GFXConstantElem elem = {
					.stage = (uint32_t)s,
					.id = id,
					.size = size,
					.value = value
				};

				if (!gfx_vec_insert(&technique->constants, 1, &elem, l))
					success = 0;
			}
		}

	return success;
}

/****************************/
GFX_API bool gfx_tech_samplers(GFXTechnique* technique,
                               size_t set,
                               size_t numSamplers, const GFXSampler* samplers)
{
	assert(technique != NULL);
	assert(set < technique->numSets);
	assert(numSamplers > 0);
	assert(samplers != NULL);

	// Skip if already locked.
	if (technique->layout != NULL)
		return 0;

	// Keep track of success, not gonna bother removing
	// previous samplers if one failed to insert.
	bool success = 1;

	for (size_t s = 0; s < numSamplers; ++s)
	{
		// Check if we can set a sampler to this resource.
		_GFXShaderResource* res =
			_gfx_tech_get_resource(technique, set, samplers[s].binding);

		if (res == NULL ||
			(res->type != _GFX_SHADER_IMAGE_AND_SAMPLER &&
			res->type != _GFX_SHADER_SAMPLER) ||
			// Check if the index exists while we're at it :)
			samplers[s].index >= res->count)
		{
			// Skip it if not.
			gfx_log_warn(
				"Could not set sampler of descriptor resource "
				"(set=%"GFX_PRIs", binding=%"GFX_PRIs", index=%"GFX_PRIs") "
				"of a technique, not a sampler.",
				set, samplers[s].binding, samplers[s].index);

			success = 0;
			continue;
		}

		// Insert the sampler element.
		size_t ind = _gfx_find_sampler_elem(
			&technique->samplers,
			set, samplers[s].binding, samplers[s].index, 1);

		if (ind == SIZE_MAX)
		{
			// Just continue to the next on failure.
			success = 0;
			continue;
		}

		// Set sampler values.
		_GFXSamplerElem* elem = gfx_vec_at(&technique->samplers, ind);
		elem->sampler = samplers[s];

		// And insert a binding element to make it immutable.
		if (!_gfx_find_binding_elem(
			&technique->immutable, set, samplers[s].binding, 1))
		{
			// Erase the sampler altogether on failure.
			gfx_vec_erase(&technique->samplers, 1, ind);
			success = 0;
			continue;
		}
	}

	return success;
}

/****************************/
GFX_API bool gfx_tech_immutable(GFXTechnique* technique,
                                size_t set, size_t binding)
{
	assert(technique != NULL);
	assert(set < technique->numSets);

	// Skip if already locked.
	if (technique->layout != NULL)
		return 0;

	// Check if we can make this resoure immutable.
	_GFXShaderResource* res =
		_gfx_tech_get_resource(technique, set, binding);

	if (res == NULL ||
		(res->type != _GFX_SHADER_IMAGE_AND_SAMPLER &&
		res->type != _GFX_SHADER_SAMPLER))
	{
		// Nop.
		gfx_log_warn(
			"Could not set an immutable descriptor resource "
			"(set=%"GFX_PRIs", binding=%"GFX_PRIs") of a technique, "
			"not a sampler.",
			set, binding);

		return 0;
	}

	// Insert the binding element.
	return _gfx_find_binding_elem(&technique->immutable, set, binding, 1);
}

/****************************/
GFX_API bool gfx_tech_dynamic(GFXTechnique* technique,
                              size_t set, size_t binding)
{
	assert(technique != NULL);
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
GFX_API bool gfx_tech_lock(GFXTechnique* technique)
{
	assert(technique != NULL);

	GFXRenderer* renderer = technique->renderer;

	// Already locked.
	if (technique->layout != NULL)
		return 1;

	// Create all descriptor set layouts.
	// We do this by looping over all descriptor set layouts we know we must
	// create, while simultaneously looping over all resources in all shaders.
	// We kinda have to do it this difficult way,
	// we need to know which shaders want access to each resource.
	size_t resPos[_GFX_NUM_SHADER_STAGES];
	for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s) resPos[s] = 0;

	// Use a vector for all bindings, samplers & handles of each set,
	// otherwise this gets complicated...
	GFXVec bindings;
	GFXVec samplers;
	GFXVec samplerHandles;
	gfx_vec_init(&bindings, sizeof(VkDescriptorSetLayoutBinding));
	gfx_vec_init(&samplers, sizeof(VkSampler));
	gfx_vec_init(&samplerHandles, sizeof(void*));

	// Loop over all sets.
	for (size_t set = 0; set < technique->numSets; ++set)
	{
		// Loop over all bindings of this set.
		// NOTE: _Always_ true!
		for (size_t binding = 0; 1; ++binding)
		{
			_GFXShaderResource* cur = NULL;
			GFXShaderStage stages = 0;
			bool done = 1;

			// Within all shaders, 'loop' to the relevant resource.
			for (size_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
				if (technique->shaders[s] != NULL)
				{
					GFXShader* shader = technique->shaders[s];
					if (resPos[s] >= shader->reflect.bindings) continue;

					_GFXShaderResource* res =
						shader->reflect.resources +
						shader->reflect.locations + resPos[s];

					if (res->set < set ||
						(res->set == set && res->binding < binding))
					{
						++resPos[s];
						++res;
					}

					if (
						resPos[s] < shader->reflect.bindings &&
						res->set == set)
					{
						done = 0; // Still resources of this set left!
						if (res->binding == binding)
							cur = res,
							stages |= shader->stage;
					}
				}

			// Seen all resources, done for this set!
			if (done) break;

			// If an empty resource, skip it.
			// Do not fret; this does not skip 'unsized' (i.e. variable sized)
			// storage buffers. The _last element_ of this resource would have
			// a count of zero, not the resource itself :)
			if (cur == NULL || cur->count == 0) continue;

			// Push the resource as a binding.
			const bool isDynamic =
				_gfx_find_binding_elem(&technique->dynamic, set, binding, 0);

			VkDescriptorSetLayoutBinding dslb = {
				.binding            = (uint32_t)binding,
				.descriptorType     = _GFX_GET_VK_DESCRIPTOR_TYPE(cur->type, isDynamic),
				.descriptorCount    = (uint32_t)cur->count,
				.stageFlags         = _GFX_GET_VK_SHADER_STAGE(stages),
				.pImmutableSamplers = NULL
			};

			if (!gfx_vec_push(&bindings, 1, &dslb))
				goto reset;
		}

		// Loop over all bindings again to create immutable samplers.
		size_t samOffs[GFX_MAX(1, bindings.size)];
		unsigned char immutable[GFX_MAX(1, bindings.size)];

		for (size_t b = 0; b < bindings.size; ++b)
		{
			VkDescriptorSetLayoutBinding* dslb = gfx_vec_at(&bindings, b);
			samOffs[b] = samplers.size;
			immutable[b] = (unsigned char)_gfx_find_binding_elem(
				&technique->immutable, set, dslb->binding, 0);

			if (!immutable[b]) continue;

			// Welp, create 'm.
			for (size_t i = 0; i < dslb->descriptorCount; ++i)
			{
				const size_t samplerInd = _gfx_find_sampler_elem(
					&technique->samplers, set, dslb->binding, i, 0);

				const GFXSampler* samplerInp =
					(samplerInd == SIZE_MAX) ? NULL :
					&((_GFXSamplerElem*)gfx_vec_at(
						&technique->samplers, samplerInd))->sampler;

				// Use the sampler get func for defaults.
				_GFXCacheElem* sampler =
					_gfx_get_sampler(renderer, samplerInp);

				// Push the sampler and a handle.
				const void* handle = sampler;
				if (
					sampler == NULL ||
					!gfx_vec_push(&samplers, 1, &sampler->vk.sampler) ||
					!gfx_vec_push(&samplerHandles, 1, &handle))
				{
					goto reset;
				}
			}
		}

		// And loop AGAIN to set the immutable sampler pointers!
		for (size_t b = 0; b < bindings.size; ++b)
			if (immutable[b])
			{
				VkDescriptorSetLayoutBinding* dslb = gfx_vec_at(&bindings, b);
				dslb->pImmutableSamplers = gfx_vec_at(&samplers, samOffs[b]);
			}

		// Create the actual descriptor set layout.
		VkDescriptorSetLayoutCreateInfo dslci = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,

			.pNext        = NULL,
			.flags        = 0,
			.bindingCount = (uint32_t)bindings.size,

			.pBindings = bindings.size > 0 ?
				gfx_vec_at(&bindings, 0) : NULL
		};

		const void** handles = gfx_vec_at(&samplerHandles, 0);
		technique->setLayouts[set] =
			_gfx_cache_get(&renderer->cache, &dslci.sType, handles);

		if (technique->setLayouts[set] == NULL)
			goto reset;

		// Keep memory for next set!
		gfx_vec_release(&bindings);
		gfx_vec_release(&samplers);
		gfx_vec_release(&samplerHandles);
	}

	// Clean temporary set memory!
	gfx_vec_clear(&bindings);
	gfx_vec_clear(&samplers);
	gfx_vec_clear(&samplerHandles);

	// Create pipeline layout.
	// We use a scope here so the gotos above are allowed.
	{
		VkDescriptorSetLayout sets[GFX_MAX(1, technique->numSets)];
		const void* handles[GFX_MAX(1, technique->numSets)];

		for (size_t s = 0; s < technique->numSets; ++s)
			sets[s] = technique->setLayouts[s]->vk.setLayout,
			handles[s] = technique->setLayouts[s];

		VkPushConstantRange pcr = {
			.stageFlags = _GFX_GET_VK_SHADER_STAGE(technique->pushStages),
			.offset     = 0,
			.size       = technique->pushSize
		};

		VkPipelineLayoutCreateInfo plci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,

			.pNext                  = NULL,
			.flags                  = 0,
			.setLayoutCount         = (uint32_t)technique->numSets,
			.pSetLayouts            = sets,
			.pushConstantRangeCount = technique->pushSize > 0 ? 1 : 0,
			.pPushConstantRanges    = technique->pushSize > 0 ? &pcr : NULL
		};

		technique->layout =
			_gfx_cache_get(&renderer->cache, &plci.sType, handles);

		if (technique->layout == NULL)
			goto reset;
	}

	// Set `vk.layout` for locality!
	technique->vk.layout = technique->layout->vk.layout;

	// And finally, get rid of the samplers, once we've successfully locked
	// we already created and used all samplers and cannot unlock.
	gfx_vec_clear(&technique->samplers);

	return 1;


	// Reset on failure.
reset:
	technique->layout = NULL;
	gfx_vec_clear(&bindings);
	gfx_vec_clear(&samplers);
	gfx_vec_clear(&samplerHandles);

	for (size_t s = 0; s < technique->numSets; ++s)
		technique->setLayouts[s] = NULL;

	gfx_log_error("Failed to lock technique.");

	return 0;
}
