/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include "shaderc/shaderc.h"
#include "spirv_cross_c.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


// SPIR-V word size compatibility.
static_assert(sizeof(uint32_t) == 4, "SPIR-V words must be 4 bytes.");


#define _GFX_GET_LANGUAGE_STRING(language) \
	((language) == GFX_GLSL ? "glsl" : \
	(language) == GFX_HLSL ? "hlsl" : "*")

#define _GFX_GET_STAGE_STRING(stage) \
	((stage) == GFX_STAGE_VERTEX ? \
		"vertex" : \
	(stage) == GFX_STAGE_TESS_CONTROL ? \
		"tessellation control" : \
	(stage) == GFX_STAGE_TESS_EVALUATION ? \
		"tessellation evaluation" : \
	(stage) == GFX_STAGE_GEOMETRY ? \
		"geometry" : \
	(stage) == GFX_STAGE_FRAGMENT ? \
		"fragment" : \
	(stage) == GFX_STAGE_COMPUTE ? \
		"compute" : "unknown")

#define _GFX_GET_SHADERC_LANGUAGE(language) \
	((language) == GFX_GLSL ? \
		shaderc_source_language_glsl : \
	(language) == GFX_HLSL ? \
		shaderc_source_language_hlsl : \
		shaderc_source_language_glsl)

#define _GFX_GET_SHADERC_KIND(stage) \
	((stage) == GFX_STAGE_VERTEX ? \
		shaderc_vertex_shader : \
	(stage) == GFX_STAGE_TESS_CONTROL ? \
		shaderc_tess_control_shader : \
	(stage) == GFX_STAGE_TESS_EVALUATION ? \
		shaderc_tess_evaluation_shader : \
	(stage) == GFX_STAGE_GEOMETRY ? \
		shaderc_geometry_shader : \
	(stage) == GFX_STAGE_FRAGMENT ? \
		shaderc_fragment_shader : \
	(stage) == GFX_STAGE_COMPUTE ? \
		shaderc_compute_shader : \
		shaderc_glsl_infer_from_source)


#define _GFX_GET_RESOURCES(type, list, size) \
	do { \
		result = spvc_resources_get_resource_list_for_type( \
			resources, type, &list, &size); \
		if (result != SPVC_SUCCESS) \
			goto clean; \
	} while (0)

#define _GFX_RESOURCES_REFLECT(type, list, size) \
	do { \
		for (size_t i = 0; i < size; ++i) \
			_gfx_reflect_resource( \
				shader, compiler, type, list + i, rList + (rInd++)); \
	} while (0)

#define _GFX_SET_SHADERC_LIMIT(shc, vk) \
	do { \
		shaderc_compile_options_set_limit(options, \
			shaderc_limit_##shc, (int)pdp.limits.vk); \
	} while (0)


/****************************
 * Default shaderc include error.
 */
const static shaderc_include_result _gfx_shaderc_def_error = {
	.source_name = "",
	.source_name_length = 0,
	.content =
		"unknown source or failed allocation for #include directive.",

	.content_length = 59 // (!).
};


/****************************
 * Callback for SPIRV-Cross errors.
 */
static void _gfx_spirv_cross_error(void* userData, const char* error)
{
	// Just log it as a groufix error.
	gfx_log_error("SPIRV-Cross: %s", error);
}

/****************************
 * Resolve callback for the shaderc includer.
 */
static shaderc_include_result* _gfx_shaderc_resolve(void* ptr, const char* req,
                                                    int type, const char* src,
                                                    size_t depth)
{
	const GFXIncluder* inc = ptr;

	// Allocate new source name so we can return it.
	const size_t sourceLen = strlen(req);
	if (sourceLen == 0)
		return (shaderc_include_result*)&_gfx_shaderc_def_error;

	char* sourceName = malloc(sourceLen + 1);
	if (sourceName == NULL)
		return (shaderc_include_result*)&_gfx_shaderc_def_error;

	strcpy(sourceName, req);

	// Allocate a shaderc result (on error: default error!).
	shaderc_include_result* result = malloc(sizeof(shaderc_include_result));
	if (result == NULL)
	{
		free(sourceName);
		return (shaderc_include_result*)&_gfx_shaderc_def_error;
	}

	// Set it to an error state so we can goto on error!
	result->source_name = "";
	result->source_name_length = 0;

	// Resolve the requested source.
	result->content =
		"could not resolve #include directive.";

	const GFXReader* str = gfx_io_resolve(inc, req);
	if (str == NULL) goto clean_name;

	// Allocate content buffer.
	result->content =
		"could not read data from stream to load #include directive.";

	long long len = gfx_io_len(str);
	if (len <= 0) goto clean_stream;

	char* content = malloc((size_t)len);
	if (content == NULL) goto clean_stream;

	// Read content.
	len = gfx_io_read(str, content, (size_t)len);
	if (len <= 0) goto clean_content;

	// Release the stream & output.
	gfx_io_release(inc, str);

	result->source_name = sourceName;
	result->source_name_length = sourceLen;
	result->content = content;
	result->content_length = (size_t)len;

	return result;


	// Cleanup on error.
clean_content:
	free(content);
clean_stream:
	gfx_io_release(inc, str);
clean_name:
	free(sourceName);

	result->content_length = strlen(result->content); // (!).
	return result;
}

/****************************
 * Release callback for the shaderc includer.
 */
static void _gfx_shaderc_release(void* ptr, shaderc_include_result* result)
{
	// Nothing to do if it was the default error.
	if (result != (shaderc_include_result*)&_gfx_shaderc_def_error)
	{
		if (result->source_name_length > 0)
		{
			free((char*)result->source_name);
			free((char*)result->content);
		}

		free(result);
	}
}

/****************************
 * Initializes a reflected shader resource at the given address,
 * then insertion-sorts it backwards into the shader->reflect.resources array.
 * Make sure to reflect vert/frag io resources first!
 */
static void _gfx_reflect_resource(GFXShader* shader, spvc_compiler compiler,
                                  spvc_resource_type type,
                                  const spvc_reflected_resource* in,
                                  _GFXShaderResource* out)
{
	// Get all metadata.
	const spvc_type hType =
		spvc_compiler_get_type_handle(compiler, in->type_id);
	const unsigned int numDims =
		spvc_type_get_num_array_dimensions(hType);

	const spvc_type hBaseType =
		spvc_compiler_get_type_handle(compiler, in->base_type_id);
	const spvc_basetype baseType =
		spvc_type_get_basetype(hBaseType);

	const bool hasLocation =
		type == SPVC_RESOURCE_TYPE_STAGE_INPUT ||
		type == SPVC_RESOURCE_TYPE_STAGE_OUTPUT;

	const bool isImage =
		baseType == SPVC_BASETYPE_IMAGE ||
		baseType == SPVC_BASETYPE_SAMPLED_IMAGE;

	const SpvDim imageDim = isImage ?
		spvc_type_get_image_dimension(hBaseType) : 0;
	const spvc_bool arrayed = isImage ?
		spvc_type_get_image_arrayed(hBaseType) : SPVC_FALSE;

	if (hasLocation)
	{
		// Get location of vertex input or fragment output.
		// Leave binding undefined.
		out->location = spvc_compiler_get_decoration(
			compiler, in->id, SpvDecorationLocation);
	}
	else
	{
		// Get binding & set of descriptor binding.
		out->set = spvc_compiler_get_decoration(
			compiler, in->id, SpvDecorationDescriptorSet);
		out->binding = spvc_compiler_get_decoration(
			compiler, in->id, SpvDecorationBinding);
	}

	// Get array size.
	// Multiply all array dimensions together.
	// If one of them is 0, so should count, as it is unsized.
	out->count = (numDims == 0) ? 1 :
		spvc_type_get_array_dimension(hType, 0);

	for (unsigned int d = 1; d < numDims; ++d)
		out->count *= spvc_type_get_array_dimension(hType, d);

	// Get struct size.
	out->size = 0;

	if (baseType == SPVC_BASETYPE_STRUCT)
	{
		size_t structSize;
		spvc_result result = spvc_compiler_get_declared_struct_size(
			compiler, hBaseType, &structSize);

		if (result == SPVC_SUCCESS) out->size = structSize;
	}

	// Get view type.
	switch (imageDim)
	{
	case SpvDim1D:
		out->viewType = arrayed ? GFX_VIEW_1D_ARRAY : GFX_VIEW_1D;
		break;

	case SpvDim2D:
	case SpvDimRect:
		out->viewType = arrayed ? GFX_VIEW_2D_ARRAY : GFX_VIEW_2D;
		break;

	case SpvDimCube:
		out->viewType = arrayed ? GFX_VIEW_CUBE_ARRAY : GFX_VIEW_CUBE;
		break;

	case SpvDim3D:
		out->viewType = GFX_VIEW_3D;
		break;

	default:
		break;
	}

	// Resource type.
	switch (type)
	{
	case SPVC_RESOURCE_TYPE_STAGE_INPUT:
		out->type = _GFX_SHADER_VERTEX_INPUT;
		break;

	case SPVC_RESOURCE_TYPE_STAGE_OUTPUT:
		out->type = _GFX_SHADER_FRAGMENT_OUTPUT;
		break;

	case SPVC_RESOURCE_TYPE_SUBPASS_INPUT:
		out->type = _GFX_SHADER_ATTACHMENT_INPUT;
		break;

	case SPVC_RESOURCE_TYPE_UNIFORM_BUFFER:
		out->type = _GFX_SHADER_BUFFER_UNIFORM;
		break;

	case SPVC_RESOURCE_TYPE_STORAGE_BUFFER:
		out->type = _GFX_SHADER_BUFFER_STORAGE;
		break;

	case SPVC_RESOURCE_TYPE_STORAGE_IMAGE:
		out->type = imageDim == SpvDimBuffer ?
			_GFX_SHADER_BUFFER_STORAGE_TEXEL :
			_GFX_SHADER_IMAGE_STORAGE;
		break;

	case SPVC_RESOURCE_TYPE_SAMPLED_IMAGE:
		out->type = _GFX_SHADER_IMAGE_AND_SAMPLER;
		break;

	case SPVC_RESOURCE_TYPE_SEPARATE_IMAGE:
		out->type = imageDim == SpvDimBuffer ?
			_GFX_SHADER_BUFFER_UNIFORM_TEXEL :
			_GFX_SHADER_IMAGE_SAMPLED;
		break;

	case SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS:
		out->type = _GFX_SHADER_SAMPLER;
		break;

	default:
		break;
	}

	// Now we have all information, it needs to be insertion-sorted
	// backwards into the shader->reflect.resources array.
	_GFXShaderResource t = *out;

	while (out != shader->reflect.resources)
	{
		_GFXShaderResource* bef = out-1;

		// Check if the previous position needs to go to the right.
		// NOTE: We assume all vert/frag io resources are inserted before
		// any others!!
		const bool greater = hasLocation ?
			// Compare locations if it is a vert/frag io.
			bef->location > t.location :
			// Check if it is not a vert/frag io AND compare set/binding.
			bef->type != _GFX_SHADER_VERTEX_INPUT &&
			bef->type != _GFX_SHADER_FRAGMENT_OUTPUT &&
				(bef->set > t.set ||
					(bef->set == t.set && bef->binding > t.binding));

		if (!greater) break;

		*out = *bef;
		out = bef;
	}

	*out = t;
}

/****************************
 * Performs reflection and creates metadata for a shader.
 * shader->reflect.* must be 0/NULL, no prior reflection must be performed.
 * @param shader Cannot be NULL.
 * @return Zero on failure.
 *
 * Reflection data is not cleaned on failure!
 */
static bool _gfx_shader_reflect(GFXShader* shader,
                                size_t size, const uint32_t* code)
{
	assert(shader != NULL);
	assert(shader->reflect.push == 0);
	assert(shader->reflect.locations == 0);
	assert(shader->reflect.sets == 0);
	assert(shader->reflect.bindings == 0);
	assert(shader->reflect.constants == 0);
	assert(shader->reflect.resources == NULL);

	// Create SPIR-V context.
	spvc_context context = NULL;
	spvc_result result = spvc_context_create(&context);

	if (result != SPVC_SUCCESS)
		goto error;

	// Set error callback.
	spvc_context_set_error_callback(context, _gfx_spirv_cross_error, NULL);

	// Parse SPIR-V!
	// Size is rounded down.
	spvc_parsed_ir ir = NULL;
	result = spvc_context_parse_spirv(context,
		(const SpvId*)code, size / sizeof(uint32_t), &ir);

	if (result != SPVC_SUCCESS)
		goto clean;

	// Create compiler and give it the data.
	spvc_compiler compiler = NULL;
	result = spvc_context_create_compiler(context,
		SPVC_BACKEND_NONE, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler);

	if (result != SPVC_SUCCESS)
		goto clean;

	// Get shader resources.
	spvc_resources resources = NULL;
	result = spvc_compiler_create_shader_resources(compiler, &resources);

	if (result != SPVC_SUCCESS)
		goto clean;

	// Get all resource types we care about.
	const spvc_reflected_resource* inps = NULL;
	const spvc_reflected_resource* outs = NULL;
	const spvc_reflected_resource* pushs = NULL;
	const spvc_reflected_resource* subs = NULL;
	const spvc_reflected_resource* ubos = NULL;
	const spvc_reflected_resource* sbos = NULL;
	const spvc_reflected_resource* imgs = NULL;
	const spvc_reflected_resource* simgs = NULL;
	const spvc_reflected_resource* sepimgs = NULL;
	const spvc_reflected_resource* samps = NULL;

	size_t numInps = 0, numOuts = 0,
		numPushs = 0, numSubs = 0,
		numUbos = 0, numSbos = 0,
		numImgs = 0, numSimgs = 0, numSepimgs = 0, numSamps = 0;

	if (shader->stage == GFX_STAGE_VERTEX)
		_GFX_GET_RESOURCES(SPVC_RESOURCE_TYPE_STAGE_INPUT, inps, numInps);

	else if (shader->stage == GFX_STAGE_FRAGMENT)
		_GFX_GET_RESOURCES(SPVC_RESOURCE_TYPE_STAGE_OUTPUT, outs, numOuts);

	_GFX_GET_RESOURCES(SPVC_RESOURCE_TYPE_PUSH_CONSTANT, pushs, numPushs);
	_GFX_GET_RESOURCES(SPVC_RESOURCE_TYPE_SUBPASS_INPUT, subs, numSubs);
	_GFX_GET_RESOURCES(SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, ubos, numUbos);
	_GFX_GET_RESOURCES(SPVC_RESOURCE_TYPE_STORAGE_BUFFER, sbos, numSbos);
	_GFX_GET_RESOURCES(SPVC_RESOURCE_TYPE_STORAGE_IMAGE, imgs, numImgs);
	_GFX_GET_RESOURCES(SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, simgs, numSimgs);
	_GFX_GET_RESOURCES(SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, sepimgs, numSepimgs);
	_GFX_GET_RESOURCES(SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, samps, numSamps);

	// Get specialization constants.
	const spvc_specialization_constant* consts = NULL;
	size_t numConsts = 0;

	result = spvc_compiler_get_specialization_constants(
		compiler, &consts, &numConsts);

	if (result != SPVC_SUCCESS)
		goto clean;

	// Count the number of resources and allocate them.
	// We allocate one big block for all of them, and insertion-sort them
	// into place.
	shader->reflect.locations = numInps + numOuts;
	shader->reflect.bindings = numSubs + numUbos + numSbos +
		numImgs + numSimgs + numSepimgs + numSamps;
	shader->reflect.constants = numConsts;

	// If no locations, bindings or constants, don't allocate any memory.
	if (
		shader->reflect.locations > 0 ||
		shader->reflect.bindings > 0 ||
		shader->reflect.constants > 0)
	{
		_GFXShaderResource* rList = malloc(
			sizeof(_GFXShaderResource) *
				(shader->reflect.locations +
				shader->reflect.bindings +
				shader->reflect.constants));

		if (rList == NULL)
			goto clean;

		// We keep track of the position to insert at.
		// _gfx_reflect_resource will insertion-sort them into place.
		shader->reflect.resources = rList;
		size_t rInd = 0;

		// Make sure to reflect vert/frag io resources first!
		_GFX_RESOURCES_REFLECT(SPVC_RESOURCE_TYPE_STAGE_INPUT, inps, numInps);
		_GFX_RESOURCES_REFLECT(SPVC_RESOURCE_TYPE_STAGE_OUTPUT, outs, numOuts);

		_GFX_RESOURCES_REFLECT(SPVC_RESOURCE_TYPE_SUBPASS_INPUT, subs, numSubs);
		_GFX_RESOURCES_REFLECT(SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, ubos, numUbos);
		_GFX_RESOURCES_REFLECT(SPVC_RESOURCE_TYPE_STORAGE_BUFFER, sbos, numSbos);
		_GFX_RESOURCES_REFLECT(SPVC_RESOURCE_TYPE_STORAGE_IMAGE, imgs, numImgs);
		_GFX_RESOURCES_REFLECT(SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, simgs, numSimgs);
		_GFX_RESOURCES_REFLECT(SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, sepimgs, numSepimgs);
		_GFX_RESOURCES_REFLECT(SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, samps, numSamps);

		// And just stick in the specialization constants at the end.
		for (size_t i = 0; i < numConsts; ++i)
		{
			_GFXShaderResource* out = rList + (rInd++);
			out->id = consts[i].constant_id;
			out->count = 1;
			out->type = _GFX_SHADER_CONSTANT;
		}

		// Count number of used descriptor sets (which should be sorted!).
		uint32_t curSet = UINT32_MAX;
		for (size_t b = 0; b < shader->reflect.bindings; ++b)
		{
			_GFXShaderResource* res = rList + shader->reflect.locations + b;
			if (curSet == UINT32_MAX || res->set > curSet)
			{
				++shader->reflect.sets;
				curSet = res->set;
			}
		}
	}

	// Get push constant block size.
	for (size_t i = 0; i < numPushs; ++i)
	{
		const spvc_type hBaseType = spvc_compiler_get_type_handle(
			compiler, pushs[i].base_type_id);

		size_t pushSize = 0;
		result = spvc_compiler_get_declared_struct_size(
			compiler, hBaseType, &pushSize);

		if (result != SPVC_SUCCESS)
			goto clean;

		shader->reflect.push += (uint32_t)pushSize;
	}

	// Destroy all resources.
	// The context owns all memory allocations, no need to free others.
	spvc_context_destroy(context);

	return 1;


	// Cleanup on failure.
clean:
	spvc_context_destroy(context);
error:
	gfx_log_error(
		"Reflection on %s shader failed.",
		_GFX_GET_STAGE_STRING(shader->stage));

	return 0;
}

/****************************
 * Creates a new shader module & metadata to actually use.
 * shader->vk.module must be NULL, no prior shader module must be created.
 * @param shader Cannot be NULL.
 * @param size   Must be a multiple of sizeof(uint32_t).
 * @return Zero on failure.
 */
static bool _gfx_shader_build(GFXShader* shader,
                              size_t size, const uint32_t* code)
{
	assert(shader != NULL);
	assert(shader->vk.module == VK_NULL_HANDLE);
	assert(size % sizeof(uint32_t) == 0);

	_GFXContext* context = shader->context;

	// First perform reflection.
	if (!_gfx_shader_reflect(shader, size, code))
		goto clean_reflect;

	// Then create the Vulkan shader module.
	VkShaderModuleCreateInfo smci = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,

		.pNext    = NULL,
		.flags    = 0,
		.codeSize = size,
		.pCode    = code
	};

	_GFX_VK_CHECK(
		context->vk.CreateShaderModule(
			context->vk.device, &smci, NULL, &shader->vk.module),
		{
			// Explicitly set module so we can call compile() or load() again.
			shader->vk.module = VK_NULL_HANDLE;
			goto clean_reflect;
		});

	// Victory log!
	gfx_log_debug(
		"Successfully loaded %s shader:\n"
		"    Input size: %"GFX_PRIs" words (%"GFX_PRIs" bytes).\n"
		"    Push constants size: %"PRIu32" bytes.\n"
		"    #input/output locations: %"GFX_PRIs".\n"
		"    #descriptor sets: %"GFX_PRIs".\n"
		"    #descriptor bindings: %"GFX_PRIs".\n"
		"    #specialization constants: %"GFX_PRIs".\n",
		_GFX_GET_STAGE_STRING(shader->stage),
		size / sizeof(uint32_t), size,
		shader->reflect.push,
		shader->reflect.locations,
		shader->reflect.sets,
		shader->reflect.bindings,
		shader->reflect.constants);

	return 1;


	// Cleanup on failure.
clean_reflect:
	free(shader->reflect.resources);

	shader->reflect.push = 0;
	shader->reflect.locations = 0;
	shader->reflect.sets = 0;
	shader->reflect.bindings = 0;
	shader->reflect.constants = 0;
	shader->reflect.resources = NULL;

	return 0;
}

/****************************
 * Generates a unique-ish 'ID' for the shader to use as hashable cache handles.
 */
static inline uintptr_t _gfx_shader_handle(_GFXContext* context)
{
	uintptr_t handle = atomic_fetch_add_explicit(
		&context->limits.shaders, 1, memory_order_relaxed);

	if (handle == UINTPTR_MAX) gfx_log_warn(
		"Shader handle generation reached maximum (%"PRIuPTR") and "
		"overflowed; may cause new shaders to be swapped with old shaders.",
		UINTPTR_MAX);

	return handle;
}

/****************************/
GFX_API GFXShader* gfx_create_shader(GFXShaderStage stage, GFXDevice* device)
{
	assert(stage != GFX_STAGE_ANY);
	assert(GFX_IS_POWER_OF_TWO(stage)); // Only 1 stage can be set.

	// Allocate a new shader.
	GFXShader* shader = malloc(sizeof(GFXShader));
	if (shader == NULL) goto clean;

	// Get context associated with the device.
	// We need the device to set the compiler's target environment.
	_GFX_GET_DEVICE(shader->device, device);
	_GFX_GET_CONTEXT(shader->context, device, goto clean);

	shader->handle = _gfx_shader_handle(shader->context);
	shader->stage = stage;
	shader->vk.module = VK_NULL_HANDLE;

	shader->reflect.push = 0;
	shader->reflect.locations = 0;
	shader->reflect.sets = 0;
	shader->reflect.bindings = 0;
	shader->reflect.constants = 0;
	shader->reflect.resources = NULL;

	return shader;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not create a new shader.");
	free(shader);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_shader(GFXShader* shader)
{
	if (shader == NULL)
		return;

	_GFXContext* context = shader->context;

	// Free reflection metadata.
	free(shader->reflect.resources);

	// Destroy the shader module.
	context->vk.DestroyShaderModule(
		context->vk.device, shader->vk.module, NULL);

	free(shader);
}

/****************************/
GFX_API GFXDevice* gfx_shader_get_device(GFXShader* shader)
{
	if (shader == NULL)
		return NULL;

	return (GFXDevice*)shader->device;
}

/****************************/
GFX_API bool gfx_shader_compile(GFXShader* shader, GFXShaderLanguage language,
                                bool optimize,
                                const GFXReader* src, const GFXIncluder* inc,
                                const GFXWriter* out, const GFXWriter* err)
{
	assert(shader != NULL);
	assert(src != NULL);

	_GFXDevice* device = shader->device;

	// Already has a shader module.
	if (shader->vk.module != VK_NULL_HANDLE)
		return 0;

	// Read source.
	const void* source;
	long long len = gfx_io_raw_init(&source, src);

	if (len <= 0)
	{
		gfx_log_error(
			"Could not read source from stream to compile %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		return 0;
	}

	// Create compiler and compile options.
	// We create new resources for every shader,
	// this presumably makes it pretty much thread-safe.
	shaderc_compiler_t compiler =
		shaderc_compiler_initialize();
	shaderc_compile_options_t options =
		shaderc_compile_options_initialize();

	if (compiler == NULL || options == NULL)
	{
		gfx_log_error(
			"Could not initialize resources to compile %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		goto clean_compiler;
	}

	// Set source language.
	shaderc_compile_options_set_source_language(
		options, _GFX_GET_SHADERC_LANGUAGE(language));

	// Set target environment.
	// Omits patch version (Shaderc doesn't understand it).
	shaderc_compile_options_set_target_env(
		options, shaderc_target_env_vulkan,
		VK_MAKE_API_VERSION(0,
			VK_API_VERSION_MAJOR(device->api),
			VK_API_VERSION_MINOR(device->api), 0));

#if !defined (NDEBUG)
	// If in debug mode, generate debug info :)
	shaderc_compile_options_set_generate_debug_info(
		options);
#endif

	// Set includer callbacks if an includer is given.
	if (inc != NULL)
		shaderc_compile_options_set_include_callbacks(
			options,
			_gfx_shaderc_resolve,
			_gfx_shaderc_release,
			(GFXIncluder*)inc);

	// Add all these options only if we compile for this specific platform.
	// This will enable optimization for the target API and GPU limits.
	if (optimize)
	{
		// Optimization level and target environment.
		shaderc_compile_options_set_optimization_level(
			options, shaderc_optimization_level_performance);

		// GPU limits.
		VkPhysicalDeviceProperties pdp;
		_groufix.vk.GetPhysicalDeviceProperties(device->vk.device, &pdp);

		_GFX_SET_SHADERC_LIMIT(max_clip_distances, maxClipDistances);
		_GFX_SET_SHADERC_LIMIT(max_cull_distances, maxCullDistances);
		_GFX_SET_SHADERC_LIMIT(max_viewports, maxViewports);

		_GFX_SET_SHADERC_LIMIT(max_combined_clip_and_cull_distances,
			maxCombinedClipAndCullDistances);
		_GFX_SET_SHADERC_LIMIT(max_vertex_output_components,
			maxVertexOutputComponents);
		_GFX_SET_SHADERC_LIMIT(max_tess_control_total_output_components,
			maxTessellationControlTotalOutputComponents);
		_GFX_SET_SHADERC_LIMIT(max_tess_evaluation_input_components,
			maxTessellationEvaluationInputComponents);
		_GFX_SET_SHADERC_LIMIT(max_tess_evaluation_output_components,
			maxTessellationEvaluationOutputComponents);
		_GFX_SET_SHADERC_LIMIT(max_tess_gen_level,
			maxTessellationGenerationLevel);
		_GFX_SET_SHADERC_LIMIT(max_geometry_input_components,
			maxGeometryInputComponents);
		_GFX_SET_SHADERC_LIMIT(max_geometry_output_components,
			maxGeometryOutputComponents);
		_GFX_SET_SHADERC_LIMIT(max_geometry_output_vertices,
			maxGeometryOutputVertices);
		_GFX_SET_SHADERC_LIMIT(max_geometry_total_output_components,
			maxGeometryTotalOutputComponents);
		_GFX_SET_SHADERC_LIMIT(max_fragment_input_components,
			maxFragmentInputComponents);
		_GFX_SET_SHADERC_LIMIT(max_compute_work_group_count_x,
			maxComputeWorkGroupCount[0]);
		_GFX_SET_SHADERC_LIMIT(max_compute_work_group_count_y,
			maxComputeWorkGroupCount[1]);
		_GFX_SET_SHADERC_LIMIT(max_compute_work_group_count_z,
			maxComputeWorkGroupCount[2]);
		_GFX_SET_SHADERC_LIMIT(max_compute_work_group_size_x,
			maxComputeWorkGroupSize[0]);
		_GFX_SET_SHADERC_LIMIT(max_compute_work_group_size_y,
			maxComputeWorkGroupSize[1]);
		_GFX_SET_SHADERC_LIMIT(max_compute_work_group_size_z,
			maxComputeWorkGroupSize[2]);
	}

	// Compile the shader.
	shaderc_compilation_result_t result = shaderc_compile_into_spv(
		compiler, (const char*)source, (size_t)len,
		_GFX_GET_SHADERC_KIND(shader->stage),
		_GFX_GET_LANGUAGE_STRING(language),
		"main",
		options);

	shaderc_compilation_status status =
		shaderc_result_get_compilation_status(result);

	// Something went wrong.
	// We explicitly log shader errors/warnings AND stream out.
	if (status != shaderc_compilation_status_success)
	{
		const char* msg = shaderc_result_get_error_message(result);
		if (err != NULL) gfx_io_write(err, msg, strlen(msg));

		gfx_log_error(
			"Could not compile %s shader:\n%s",
			_GFX_GET_STAGE_STRING(shader->stage), msg);

		goto clean_result;
	}

	// We have no errors, but maybe warnings.
	const size_t warnings = shaderc_result_get_num_warnings(result);

	// Stream warnings out.
	if (err != NULL && warnings > 0)
	{
		const char* msg = shaderc_result_get_error_message(result);
		gfx_io_write(err, msg, strlen(msg));
	}

	// Get bytecode and its length / word size.
	// Round the size to a multiple of 4 just in case it isn't.
	const size_t size = shaderc_result_get_length(result);
	const char* bytes = shaderc_result_get_bytes(result);
	const size_t wordSize = (size / sizeof(uint32_t)) * sizeof(uint32_t);

	// Compilation victory log!
	gfx_log_debug(
		"Successfully compiled %s shader:\n"
		"    Output size: %"GFX_PRIs" words (%"GFX_PRIs" bytes).\n"
		"    #warnings: %"GFX_PRIs".\n%s%s",
		_GFX_GET_STAGE_STRING(shader->stage),
		size / sizeof(uint32_t), size,
		warnings,
		warnings > 0 ? "\n" : "",
		warnings > 0 ? shaderc_result_get_error_message(result) : "");

	// Then, stream out the resulting SPIR-V bytecode.
	if (out != NULL && gfx_io_write(out, bytes, size) > 0)
		gfx_log_info(
			"Written SPIR-V to stream (%"GFX_PRIs" bytes).",
			size);

	// Lastly, attempt to build the shader module.
	if (!_gfx_shader_build(shader, wordSize, (const uint32_t*)bytes))
	{
		gfx_log_error(
			"Failed to load compiled %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		goto clean_result;
	}

	// Get rid of the resources and return.
	shaderc_result_release(result);
	shaderc_compiler_release(compiler);
	shaderc_compile_options_release(options);

	gfx_io_raw_clear(&source, src);

	return 1;


	// Cleanup on failure.
clean_result:
	shaderc_result_release(result);
clean_compiler:
	shaderc_compiler_release(compiler);
	shaderc_compile_options_release(options);

	gfx_io_raw_clear(&source, src);

	return 0;
}

/****************************/
GFX_API bool gfx_shader_load(GFXShader* shader, const GFXReader* src)
{
	assert(shader != NULL);
	assert(src != NULL);

	// Already has a shader module.
	if (shader->vk.module != VK_NULL_HANDLE)
		return 0;

	// Read source.
	const void* source;
	long long len = gfx_io_raw_init(&source, src);

	if (len <= 0)
	{
		gfx_log_error(
			"Could not read source from stream to load %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		return 0;
	}

	// Attempt to build the shader module.
	// Round the size to a multiple of 4 just in case it isn't.
	const size_t wordSize =
		((size_t)len / sizeof(uint32_t)) * sizeof(uint32_t);

	bool built = _gfx_shader_build(shader, wordSize, source);
	if (!built)
		gfx_log_error(
			"Failed to load %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

	gfx_io_raw_clear(&source, src);

	return built;
}
