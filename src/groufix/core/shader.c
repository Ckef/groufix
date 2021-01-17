/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <shaderc/shaderc.h>
#include <stdlib.h>



/****************************/
GFX_API GFXShader* gfx_create_shader(GFXDevice* device)
{
	// Allocate a new shaderer.
	GFXShader* shader = malloc(sizeof(GFXRenderer));
	if (shader == NULL)
		goto clean;

	// Get context associated with the device.
	_GFX_GET_CONTEXT(shader->context, device, goto clean);

	// Initialize things.
	shader->vk.shader = VK_NULL_HANDLE;


	// Clean on failure.
clean:
	gfx_log_error("Could not create a new shaderer.");
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
		context->vk.device, shader->vk.shader, NULL);

	free(shader);
}
