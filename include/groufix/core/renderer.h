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
 * Logical renderer definition.
 */
typedef struct GFXRenderer GFXRenderer;


/**
 * Logical render pass definition.
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
 * Adds a new (target) render pass to the renderer.
 * @param renderer Cannot be NULL.
 * @param numDeps  Number of dependencies, 0 for no dependencies.
 * @param deps     Passes it depends on, cannot be NULL if numDeps > 0.
 * @return NULL on failure.
 *
 * A render pass can not be removed, nor can its dependencies be changed
 * once it has been added to a renderer.
 */
GFX_API GFXRenderPass* gfx_renderer_add(GFXRenderer* renderer, size_t numDeps,
                                        GFXRenderPass** deps);

/**
 * Retrieves the number of target render passes of a renderer.
 * A target pass is one that no other pass depends on (last in the path).
 * @param renderer Cannot be NULL.
 *
 * This number may change when a new render pass is added.
 */
GFX_API size_t gfx_renderer_get_num(GFXRenderer* renderer);

/**
 * Retrieves a target render pass of a renderer.
 * @param renderer Cannot be NULL.
 * @param index Must be < gfx_renderer_get_num(renderer).
 *
 * The index of each target may change when a new render pass is added,
 * however their order remains fixed during the lifetime of the renderer.
 */
GFX_API GFXRenderPass* gfx_renderer_get(GFXRenderer* renderer, size_t index);

/**
 * Submits a target of the renderer to the GPU.
 * The given index is the index of the target render pass to submit.
 * @param renderer Cannot be NULL.
 * @param target   Must be < gfx_renderer_get_num(renderer).
 * @return Zero on failure.
 */
GFX_API int gfx_renderer_submit(GFXRenderer* renderer, size_t target);


/****************************
 * Logical render pass.
 ****************************/

/**
 * Retrieves the number of passes a single render pass depends on.
 * @param pass Cannot be NULL.
 */
GFX_API size_t gfx_render_pass_get_num(GFXRenderPass* pass);

/**
 * Retrieves a dependency of a render pass.
 * @param pass Cannot be NULL.
 * @param index Must be < gfx_render_pass_get_num(pass).
 */
GFX_API GFXRenderPass* gfx_render_pass_get(GFXRenderPass* pass, size_t index);

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
