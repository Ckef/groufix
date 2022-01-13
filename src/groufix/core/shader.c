/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <shaderc/shaderc.h>
#include <spirv_cross_c.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>


#define _GFX_SET_SHADERC_LIMIT(shc, vk) \
	shaderc_compile_options_set_limit(options, \
		shaderc_limit_##shc, (int)pdp.limits.vk)

#define _GFX_GET_LANGUAGE_STRING(language) \
	(((language) == GFX_GLSL) ? "glsl" : \
	((language) == GFX_HLSL) ? "hlsl" : "*")

#define _GFX_GET_STAGE_STRING(stage) \
	(((stage) == GFX_STAGE_VERTEX) ? \
		"vertex" : \
	((stage) == GFX_STAGE_TESS_CONTROL) ? \
		"tessellation control" : \
	((stage) == GFX_STAGE_TESS_EVALUATION) ? \
		"tessellation evaluation" : \
	((stage) == GFX_STAGE_GEOMETRY) ? \
		"geometry" : \
	((stage) == GFX_STAGE_FRAGMENT) ? \
		"fragment" : \
	((stage) == GFX_STAGE_COMPUTE) ? \
		"compute" : "unknown")

#define _GFX_GET_SHADERC_LANGUAGE(language) \
	(((language) == GFX_GLSL) ? \
		shaderc_source_language_glsl : \
	((language) == GFX_HLSL) ? \
		shaderc_source_language_hlsl : \
		shaderc_source_language_glsl)

#define _GFX_GET_SHADERC_KIND(stage) \
	(((stage) == GFX_STAGE_VERTEX) ? \
		shaderc_vertex_shader : \
	((stage) == GFX_STAGE_TESS_CONTROL) ? \
		shaderc_tess_control_shader : \
	((stage) == GFX_STAGE_TESS_EVALUATION) ? \
		shaderc_tess_evaluation_shader : \
	((stage) == GFX_STAGE_GEOMETRY) ? \
		shaderc_geometry_shader : \
	((stage) == GFX_STAGE_FRAGMENT) ? \
		shaderc_fragment_shader : \
	((stage) == GFX_STAGE_COMPUTE) ? \
		shaderc_compute_shader : \
		shaderc_glsl_infer_from_source)


/****************************
 * Callback for SPIRV-Cross errors.
 */
void _gfx_spirv_cross_error(void* userData, const char* error)
{
	// Just log it as a groufix error.
	gfx_log_error("SPIRV-Cross: %s", error);
}

/****************************
 * Performs reflection and creates metadata for a shader.
 * shader->reflect.resources must be NULL, no prior reflection must be performed.
 * @param shader Cannot be NULL.
 * @return Zero on failure.
 */
static int _gfx_shader_reflect(GFXShader* shader,
                               size_t size, const uint32_t* code)
{
	assert(shader != NULL);
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

	// TODO: Get all resource types and store it somewhere.

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
static int _gfx_shader_build(GFXShader* shader,
                             size_t size, const uint32_t* code)
{
	_Static_assert(sizeof(uint32_t) == 4, "SPIR-V words must be 4 bytes.");

	assert(shader != NULL);
	assert(shader->vk.module == VK_NULL_HANDLE);
	assert(size % sizeof(uint32_t) == 0);

	_GFXContext* context = shader->context;

	// First perform reflection.
	if (!_gfx_shader_reflect(shader, size, code))
		return 0;

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
		return 0);

	// Victory log!
	gfx_log_debug(
		"Successfully loaded %s shader:\n"
		"    SPIR-V size: %"GFX_PRIs" words (%"GFX_PRIs" bytes).\n"
		"    Push constants size: %"PRIu32" bytes.\n"
		"    #input/output locations: %"GFX_PRIs".\n"
		"    #descriptor sets: %"GFX_PRIs".\n"
		"    #descriptor bindings: %"GFX_PRIs".\n",
		_GFX_GET_STAGE_STRING(shader->stage),
		size / sizeof(uint32_t), size,
		shader->reflect.push,
		shader->reflect.locations,
		shader->reflect.sets,
		shader->reflect.bindings);

	return 1;
}

/****************************/
GFX_API GFXShader* gfx_create_shader(GFXShaderStage stage, GFXDevice* device)
{
	// Allocate a new shader.
	GFXShader* shader = malloc(sizeof(GFXShader));
	if (shader == NULL) goto clean;

	// Get context associated with the device.
	// We need the device to set the compiler's target environment.
	_GFX_GET_DEVICE(shader->device, device);
	_GFX_GET_CONTEXT(shader->context, device, goto clean);

	shader->stage = stage;
	shader->vk.module = VK_NULL_HANDLE;

	shader->reflect.push = 0;
	shader->reflect.locations = 0;
	shader->reflect.sets = 0;
	shader->reflect.bindings = 0;
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
GFX_API int gfx_shader_compile(GFXShader* shader, GFXShaderLanguage language,
                               int optimize, const GFXReader* src,
                               const GFXWriter* out, const GFXWriter* err)
{
	assert(shader != NULL);
	assert(src != NULL);

	_GFXDevice* device = shader->device;

	// Already has a shader module.
	if (shader->vk.module != VK_NULL_HANDLE)
		return 1;

	// Allocate source buffer.
	long long len = gfx_io_len(src);
	if (len <= 0)
	{
		gfx_log_error(
			"Zero or unknown stream length, cannot compile %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		return 0;
	}

	char* source = malloc((size_t)len);
	if (source == NULL)
	{
		gfx_log_error(
			"Could not allocate source buffer to compile %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		return 0;
	}

	// Read source.
	len = gfx_io_read(src, source, (size_t)len);
	if (len <= 0)
	{
		gfx_log_error(
			"Could not read source from stream to compile %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		goto clean;
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
		compiler, source, (size_t)len,
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

	// Then, attempt to build the shader module.
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

	free(source);

	return 1;


	// Cleanup on failure.
clean_result:
	shaderc_result_release(result);
clean_compiler:
	shaderc_compiler_release(compiler);
	shaderc_compile_options_release(options);
clean:
	free(source);

	return 0;
}

/****************************/
GFX_API int gfx_shader_load(GFXShader* shader, const GFXReader* src)
{
	assert(shader != NULL);
	assert(src != NULL);

	// Already has a shader module.
	if (shader->vk.module != VK_NULL_HANDLE)
		return 1;

	// Allocate source buffer.
	long long len = gfx_io_len(src);
	if (len <= 0)
	{
		gfx_log_error(
			"Zero or unknown stream length, cannot load %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		return 0;
	}

	void* source = malloc((size_t)len);
	if (source == NULL)
	{
		gfx_log_error(
			"Could not allocate source buffer to load %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		return 0;
	}

	// Read source.
	len = gfx_io_read(src, source, (size_t)len);
	if (len <= 0)
	{
		gfx_log_error(
			"Could not read source from stream to load %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		free(source);
		return 0;
	}

	// Attempt to build the shader module.
	// Round the size to a multiple of 4 just in case it isn't.
	const size_t wordSize =
		((size_t)len / sizeof(uint32_t)) * sizeof(uint32_t);

	int ret = _gfx_shader_build(shader, wordSize, source);
	if (ret == 0)
		gfx_log_error(
			"Failed to load %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

	free(source);
	return ret;
}
