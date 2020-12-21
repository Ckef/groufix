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
 * Logical renderer.
 */
typedef struct GFXRenderer GFXRenderer;


/**
 * Logical render pass.
 */
typedef struct GFXRenderPass GFXRenderPass;



/****************************
 * Logical renderer.
 ****************************/

/**
 * Creates a logical renderer.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @return NULL on failure.
 */
GFX_API GFXRenderer* gfx_create_renderer(GFXDevice* device);

/**
 * Destroys a logical renderer.
 */
GFX_API void gfx_destroy_renderer(GFXRenderer* renderer);

/**
 * TODO: Improve API, is a mockup.
 * Appends a new render pass to the renderer.
 * @param renderer Cannot be NULL.
 * @return NULL on failure.
 */
GFX_API GFXRenderPass* gfx_renderer_add(GFXRenderer* renderer);

/**
 * Submits the renderer to the GPU.
 * @param renderer Cannot be NULL.
 * @return Zero on failure.
 */
GFX_API int gfx_renderer_submit(GFXRenderer* renderer);


/****************************
 * Logical render pass.
 ****************************/

/**
 * TODO: Improve API, is a mockup.
 * TODO: Attach multiple windows?
 * TODO: Make access to window thread-safe.
 * Attaches a window to a render pass.
 * @param pass   Cannot be NULL.
 * @param window NULL to detach the current window.
 * @return Zero if the window and render pass do not share a compatible device.
 *
 * A window referenced by multiple passes is not synchronized.
 */
GFX_API int gfx_render_pass_attach_window(GFXRenderPass* pass,
                                          GFXWindow* window);


#endif
