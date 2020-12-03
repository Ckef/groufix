/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_RENDERER_H
#define GFX_CORE_RENDERER_H

#include "groufix/core/device.h"
#include "groufix/core/window.h"
#include "groufix/def.h"


/**
 * Logical render pass.
 */
typedef struct GFXRenderPass GFXRenderPass;


/**
 * TODO: Improve API, is a mockup.
 * Creates a logical render pass.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @return NULL on failure.
 */
GFX_API GFXRenderPass* gfx_create_render_pass(GFXDevice* device);

/**
 * TODO: Improve API, is a mockup.
 * Destroys a logical render pass.
 */
GFX_API void gfx_destroy_render_pass(GFXRenderPass* pass);

/**
 * TODO: Improve API, is a mockup.
 * Attaches a window to a render pass.
 * @param pass   Cannot be NULL.
 * @param window NULL to detach the current window.
 * @return Zero if the window and render pass do not share a compatible device.
 *
 * A window referenced by multiple passes is not synchronized.
 */
GFX_API int gfx_render_pass_attach_window(GFXRenderPass* pass,
                                          GFXWindow* window);

/**
 * TODO: Improve API, is a mockup.
 * Submits the render pass to the GPU.
 * @param pass Cannot be NULL.
 * @return Zero on failure.
 */
GFX_API int gfx_render_pass_submit(GFXRenderPass* pass);


#endif
