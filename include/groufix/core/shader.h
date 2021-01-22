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
	GFX_SHADER_VERTEX,
	GFX_SHADER_TESS_CONTROL,
	GFX_SHADER_TESS_EVALUATION,
	GFX_SHADER_GEOMETRY,
	GFX_SHADER_FRAGMENT,
	GFX_SHADER_COMPUTE

} GFXShaderStage;


/**
 * Shader definition.
 */
typedef struct GFXShader GFXShader;


/**
 * Creates a shader.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @param src    Source string, cannot be NULL, must be NULL-terminated.
 * @return NULL on failure.
 */
GFX_API GFXShader* gfx_create_shader(GFXShaderStage stage, GFXDevice* device);

/**
 * Destroys a shader.
 */
GFX_API void gfx_destroy_shader(GFXShader* shader);

/**
 * TODO: Stream compiler errors/warnings to user.
 * TODO: Allow recompilation (and reload), cause modules can be destroyed?
 * Compiles a shader from GLSL source into SPIR-V bytecode for use.
 * @param shader   Cannot be NULL.
 * @param source   Must be NULL-terminated, cannot be NULL.
 * @param optimize Non-zero to enable platform-specific bytecode optimization.
 * @param file     Must be NULL or NULL-terminated.
 * @return Non-zero on success, no-op if shader already stores SPIR-V bytecode.
 *
 * Optionally writes SPIR-V bytecode to file, if this action fails, it will log
 * a warning, but the compiled shader will still be used by the shader.
 * Non-existing directories will NOT get automatically created by this call.
 */
GFX_API int gfx_shader_compile(GFXShader* shader, GFXShaderLanguage language,
                               const char* source, int optimize,
                               const char* file);

/**
 * Loads SPIR-V bytecode from a file for use.
 * @param shader Cannot be NULL.
 * @param file   Must be NULL-terminated, cannot be NULL.
 * @return Non-zero on success, no-op if shader already stores SPIR-V bytecode.
 */
GFX_API int gfx_shader_load(GFXShader* shader, const char* file);


#endif
