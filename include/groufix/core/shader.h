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
 * Creates a shader.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @return NULL on failure.
 */
GFX_API GFXShader* gfx_create_shader(GFXDevice* device);

/**
 * Destroys a shader.
 */
GFX_API void gfx_destroy_shader(GFXShader* shader);


#endif
