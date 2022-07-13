/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_SHADER_H
#define GFX_CORE_SHADER_H

#include "groufix/containers/io.h"
#include "groufix/core/device.h"
#include "groufix/def.h"


/**
 * Shader language.
 */
typedef enum GFXShaderLanguage
{
	GFX_GLSL,
	GFX_HLSL

} GFXShaderLanguage;


/**
 * Shader stage.
 */
typedef enum GFXShaderStage
{
	GFX_STAGE_ANY             = 0x0000,
	GFX_STAGE_VERTEX          = 0x0001,
	GFX_STAGE_TESS_CONTROL    = 0x0002,
	GFX_STAGE_TESS_EVALUATION = 0x0004,
	GFX_STAGE_GEOMETRY        = 0x0008,
	GFX_STAGE_FRAGMENT        = 0x0010,
	GFX_STAGE_COMPUTE         = 0x0020

} GFXShaderStage;

GFX_BIT_FIELD(GFXShaderStage)


/**
 * Shader definition.
 */
typedef struct GFXShader GFXShader;


/**
 * Creates a shader.
 * @param stage  Shader stage, exactly 1 stage must be set.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @return NULL on failure.
 */
GFX_API GFXShader* gfx_create_shader(GFXShaderStage stage, GFXDevice* device);

/**
 * Returns the device the shader was created for.
 * Can be called from any thread.
 */
GFX_API GFXDevice* gfx_shader_get_device(GFXShader* shader);

/**
 * Destroys a shader.
 */
GFX_API void gfx_destroy_shader(GFXShader* shader);

/**
 * Compiles a shader from GLSL/HLSL source into SPIR-V bytecode for use.
 * @param shader   Cannot be NULL.
 * @param optimize Non-zero to enable platform-specific compiler options.
 * @param src      Source stream, cannot be NULL.
 * @param inc      Optional stream includer.
 * @param out      Optional SPIR-V bytecode output stream.
 * @param err      Optional error/warning output stream.
 * @return Non-zero on success, no-op if shader already stores SPIR-V bytecode.
 *
 * Output stream failure is ignored.
 */
GFX_API bool gfx_shader_compile(GFXShader* shader, GFXShaderLanguage language,
                                bool optimize,
                                const GFXReader* src, const GFXIncluder* inc,
                                const GFXWriter* out, const GFXWriter* err);

/**
 * Loads SPIR-V bytecode for use.
 * @param shader Cannot be NULL.
 * @param src    Source bytecode stream, cannot be NULL.
 * @return Non-zero on success, no-op if shader already stores SPIR-V bytecode.
 */
GFX_API bool gfx_shader_load(GFXShader* shader, const GFXReader* src);


#endif
