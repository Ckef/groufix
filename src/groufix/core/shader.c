/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <shaderc/shaderc.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define _GFX_SET_SHADERC_LIMIT(shc, vk) \
	shaderc_compile_options_set_limit(options, \
		shaderc_limit_##shc, (int)pdp.limits.vk);

#define _GFX_GET_LANGUAGE_STRING(language) \
	((language == GFX_GLSL) ? "glsl" : \
	(language == GFX_HLSL) ? "hlsl" : "*")

#define _GFX_GET_STAGE_STRING(stage) \
	((stage == GFX_SHADER_VERTEX) ? \
		"vertex" : \
	(stage == GFX_SHADER_TESS_CONTROL) ? \
		"tessellation control" : \
	(stage == GFX_SHADER_TESS_EVALUATION) ? \
		"tessellation evaluation" : \
	(stage == GFX_SHADER_GEOMETRY) ? \
		"geometry" : \
	(stage == GFX_SHADER_FRAGMENT) ? \
		"fragment" : \
	(stage == GFX_SHADER_COMPUTE) ? \
		"compute" : "unknown")

#define _GFX_GET_SHADERC_LANGUAGE(language) \
	((language == GFX_GLSL) ? \
		shaderc_source_language_glsl : \
	(language == GFX_HLSL) ? \
		shaderc_source_language_hlsl : \
		shaderc_source_language_glsl)

#define _GFX_GET_SHADERC_KIND(stage) \
	((stage == GFX_SHADER_VERTEX) ? \
		shaderc_vertex_shader : \
	(stage == GFX_SHADER_TESS_CONTROL) ? \
		shaderc_tess_control_shader : \
	(stage == GFX_SHADER_TESS_EVALUATION) ? \
		shaderc_tess_evaluation_shader : \
	(stage == GFX_SHADER_GEOMETRY) ? \
		shaderc_geometry_shader : \
	(stage == GFX_SHADER_FRAGMENT) ? \
		shaderc_fragment_shader : \
	(stage == GFX_SHADER_COMPUTE) ? \
		shaderc_compute_shader : \
		shaderc_glsl_infer_from_source)


/****************************
 * Creates a new shader module to actually use.
 * shader->vk.module must be NULL, no prior shader module must be created.
 * @param size   Must be a multiple of sizeof(uint32_t).
 * @param shader Cannot be NULL.
 * @return Zero on failure.
 */
static int _gfx_shader_build(GFXShader* shader,
                             size_t size, const uint32_t* code)
{
	assert(shader != NULL);
	assert(shader->vk.module == VK_NULL_HANDLE);
	assert(sizeof(uint32_t) == 4); // Has to be according to Vulkan.
	assert(size % sizeof(uint32_t) == 0);

	_GFXContext* context = shader->context;

	// Create the Vulkan shader module.
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

	return 1;
}

/****************************/
GFX_API GFXShader* gfx_create_shader(GFXShaderStage stage, GFXDevice* device)
{
	// Allocate a new shader.
	GFXShader* shader = malloc(sizeof(GFXRenderer));
	if (shader == NULL) goto clean;

	// Get context associated with the device.
	// We need the device to set the compiler's target environment.
	_GFX_GET_DEVICE(shader->device, device);
	_GFX_GET_CONTEXT(shader->context, device, goto clean);

	shader->stage = stage;
	shader->vk.module = VK_NULL_HANDLE;

	return shader;


	// Clean on failure.
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

	// Destroy the shader module.
	context->vk.DestroyShaderModule(
		context->vk.device, shader->vk.module, NULL);

	free(shader);
}

/****************************/
GFX_API int gfx_shader_compile(GFXShader* shader, GFXShaderLanguage language,
                               const char* source, int optimize,
                               const char* file)
{
	assert(shader != NULL);
	assert(source != NULL);

	_GFXDevice* device = shader->device;

	// Already has a shader module.
	if (shader->vk.module != VK_NULL_HANDLE)
		return 1;

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
			"Could not create resources to compile %s shader.",
			_GFX_GET_STAGE_STRING(shader->stage));

		goto clean;
	}

	// Set source language.
	shaderc_compile_options_set_source_language(
		options, _GFX_GET_SHADERC_LANGUAGE(language));

#if !defined (NDEBUG)
	// If in debug mode, generate debug info :)
	shaderc_compile_options_set_generate_debug_info(
		options);
#endif

	// Add all these options only if we compile for this specific platform.
	// This will enable optimization for the target API and GPU limits.
	// Target API omits patch version (Shaderc doesn't understand it).
	if (optimize)
	{
		// Optimization level and target environment.
		shaderc_compile_options_set_optimization_level(
			options, shaderc_optimization_level_performance);

		shaderc_compile_options_set_target_env(
			options, shaderc_target_env_vulkan, VK_MAKE_VERSION(
				VK_VERSION_MAJOR(device->api),
				VK_VERSION_MINOR(device->api), 0));

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
		compiler, source, strlen(source),
		_GFX_GET_SHADERC_KIND(shader->stage),
		_GFX_GET_LANGUAGE_STRING(language),
		"main",
		options);

	shaderc_compilation_status status =
		shaderc_result_get_compilation_status(result);

	// Something went wrong.
	// We explicitly log shader errors/warnings.
	if (status != shaderc_compilation_status_success)
	{
		gfx_log_error(
			"Could not compile %s shader:\n%s",
			_GFX_GET_STAGE_STRING(shader->stage),
			shaderc_result_get_error_message(result));

		goto clean_result;
	}

	/// Get bytecode.
	size_t size = shaderc_result_get_length(result);
	const char* bytes = shaderc_result_get_bytes(result);

#if !defined (NDEBUG)
	// Victory!
	size_t warnings =
		shaderc_result_get_num_warnings(result);

	gfx_log_debug(
		"Successfully compiled %s shader:\n"
		"    Output size: %u words (%u bytes).\n"
		"    #warnings: %u.\n%s%s",
		_GFX_GET_STAGE_STRING(shader->stage),
		(unsigned int)(size / sizeof(uint32_t)),
		(unsigned int)size,
		(unsigned int)warnings,
		warnings > 0 ? "\n" : "",
		warnings > 0 ? shaderc_result_get_error_message(result) : "");
#endif

	// Attempt to build the shader module.
	// Round the size to a multiple of 4 just in case it isn't.
	size_t wordSize =
		(size / sizeof(uint32_t)) * sizeof(uint32_t);

	if (!_gfx_shader_build(shader, wordSize, (const uint32_t*)bytes))
		goto clean_result;

	// At this point we succeeded,
	// but try to write to file if asked.
	if (file != NULL)
	{
		// Open file (in binary mode!) and immediately write.
		// We treat any failure as a warning, as we do have a functional shader.
		FILE* f = fopen(file, "wb");

		if (f == NULL)
			gfx_log_warn("Could not open SPIR-V file: %s", file);

		else if (fwrite(bytes, 1, size, f) < size)
			gfx_log_warn("Could not write to SPIR-V file: %s", file);
	}

	// Get rid of the resources and return.
	shaderc_result_release(result);
	shaderc_compiler_release(compiler);
	shaderc_compile_options_release(options);

	return 1;


	// Clean on failure.
clean_result:
	shaderc_result_release(result);
clean:
	shaderc_compiler_release(compiler);
	shaderc_compile_options_release(options);
	shader->vk.module = VK_NULL_HANDLE;

	return 0;
}

/****************************/
GFX_API int gfx_shader_load(GFXShader* shader, const char* file)
{
	assert(shader != NULL);
	assert(file != NULL);

	// Already has a shader module.
	if (shader->vk.module != VK_NULL_HANDLE)
		return 1;

	// Open the file (in binary mode!) and get its byte-size.
	FILE* f = fopen(file, "rb");
	if (f == NULL) goto clean;

	fseek(f, 0, SEEK_END);

	long int size = ftell(f);
	if (size == -1L) goto clean_close;

	fseek(f, 0, SEEK_SET);

	// Read the contents and close the file.
	// We actually use malloc as this might be large.
	uint32_t* bytes = malloc((size_t)size);
	if (bytes == NULL) goto clean_close;

	if (fread(bytes, 1, (size_t)size, f) < (size_t)size)
	{
		free(bytes);
		goto clean_close;
	}

	fclose(f);

	// Attempt to build the shader module.
	// Round the size to a multiple of 4 just in case it isn't.
	size_t wordSize = ((size_t)size / sizeof(uint32_t)) * sizeof(uint32_t);

	if (!_gfx_shader_build(shader, wordSize, bytes))
	{
		free(bytes);
		goto clean;
	}

	// Yep that's it, get rid of temp buffer.
	free(bytes);

	return 1;


	// Clean on failure.
clean_close:
	fclose(f);
clean:
	gfx_log_error("Could not open SPIR-V file: %s", file);
	shader->vk.module = VK_NULL_HANDLE;

	return 0;
}
