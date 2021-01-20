/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_SHADER_H
#define GFX_CORE_SHADER_H

#include "groufix/core/device.h"


/**
 * Shader definition.
 */
typedef struct GFXShader GFXShader;


/**
 * TODO: Want to be able to input spir-v.
 * TODO: Let user set log level?
 * TODO: Stream compiler errors/warnings to user.
 * TODO: Define shader type.
 * Creates a shader.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @param src    Source string, cannot be NULL, must be NULL-terminated.
 * @return NULL on failure.
 */
GFX_API GFXShader* gfx_create_shader(GFXDevice* device, const char* src);

/**
 * Destroys a shader.
 */
GFX_API void gfx_destroy_shader(GFXShader* shader);


#endif
