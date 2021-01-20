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
#include <stdlib.h>
#include <string.h>


#define _GFX_GET_SHADERC_KIND(stage) \
	((stage == GFX_SHADER_VERTEX) ? \
		shaderc_glsl_vertex_shader : \
	(stage == GFX_SHADER_TESS_CONTROL) ? \
		shaderc_glsl_tess_control_shader : \
	(stage == GFX_SHADER_TESS_EVALUATION) ? \
		shaderc_glsl_tess_evaluation_shader : \
	(stage == GFX_SHADER_GEOMETRY) ? \
		shaderc_glsl_geometry_shader : \
	(stage == GFX_SHADER_FRAGMENT) ? \
		shaderc_glsl_fragment_shader : \
	(stage == GFX_SHADER_COMPUTE) ? \
		shaderc_glsl_compute_shader : \
		shaderc_glsl_infer_from_source)


/****************************
 * Retrieves a GFXShaderStage as a readable string.
 */
static const char* _gfx_shader_stage_string(GFXShaderStage stage)
{
	switch (stage)
	{
	case GFX_SHADER_VERTEX:
		return "vertex";

	case GFX_SHADER_TESS_CONTROL:
		return "tessellation control";

	case GFX_SHADER_TESS_EVALUATION:
		return "tessellation evaluation";

	case GFX_SHADER_GEOMETRY:
		return "geometry";

	case GFX_SHADER_FRAGMENT:
		return "fragment";

	case GFX_SHADER_COMPUTE:
		return "compute";

	default:
		return "unknown";
	}
}

/****************************/
GFX_API GFXShader* gfx_create_shader(GFXShaderStage stage, GFXDevice* device,
                                     const char* src)
{
	assert(src != NULL);

	// Allocate a new shader.
	GFXShader* shader = malloc(sizeof(GFXRenderer));
	if (shader == NULL)
		goto clean;

	// Get context associated with the device.
	// We need the device to set the compiler's target environment.
	_GFXDevice* dev;

	_GFX_GET_DEVICE(dev, device);
	_GFX_GET_CONTEXT(shader->context, device, goto clean);

	// Initialize things so we don't accidentally free garbage.
	shader->bin = NULL;
	shader->vk.shader = VK_NULL_HANDLE;

	// TODO: Concern myself about thread-safety of shaderc?
	// Create compiler and compile options.
	shaderc_compiler_t compiler =
		shaderc_compiler_initialize();
	shaderc_compile_options_t options =
		shaderc_compile_options_initialize();

	if (compiler == NULL || options == NULL)
		goto clean_shaderc;

#if !defined (NDEBUG)
	// If in debug mode, generate debug info :)
	shaderc_compile_options_set_generate_debug_info(
		options);
#endif

	shaderc_compile_options_set_optimization_level(
		options, shaderc_optimization_level_performance);

	shaderc_compile_options_set_target_env(
		options, shaderc_target_env_vulkan, dev->api);

	// Compile the shader.
	shader->bin = shaderc_compile_into_spv(
		compiler,
		src, strlen(src),
		_GFX_GET_SHADERC_KIND(stage),
		"main", // TODO: Eh?
		"main",
		options);

	shaderc_compilation_status status =
		shaderc_result_get_compilation_status(shader->bin);

	// Something went wrong.
	// We explicitly log shader errors/warnings.
	// TODO: Stream to the user as well?
	if (status != shaderc_compilation_status_success)
	{
		gfx_log_error(
			"Could not compile %s shader:\n%s",
			_gfx_shader_stage_string(stage),
			shaderc_result_get_error_message(shader->bin));

		goto clean_shaderc;
	}

	// Victory!
	gfx_log_debug(
		"Successfully compiled %s shader:\n"
		"    Output size: %u bytes.\n"
		"    #errors: %u.\n"
		"    #warnings: %u.\n",
		_gfx_shader_stage_string(stage),
		(unsigned int)shaderc_result_get_length(shader->bin),
		(unsigned int)shaderc_result_get_num_errors(shader->bin),
		(unsigned int)shaderc_result_get_num_warnings(shader->bin));

	// Get rid of the compiler and return.
	shaderc_compiler_release(compiler);
	shaderc_compile_options_release(options);

	return shader;


	// Clean on failure.
clean_shaderc:
	shaderc_result_release(shader->bin);
	shaderc_compiler_release(compiler);
	shaderc_compile_options_release(options);
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

	// Release Shaderc handle.
	// TODO: Prolly want to release this waaay earlier.
	shaderc_result_release(shader->bin);

	// Destroy the shader module.
	context->vk.DestroyShaderModule(
		context->vk.device, shader->vk.shader, NULL);

	free(shader);
}
