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
#include "groufix/core/formats.h"
#include "groufix/core/heap.h"
#include "groufix/core/window.h"
#include "groufix/def.h"


/**
 * Size class of a resource.
 */
typedef enum GFXSizeClass
{
	GFX_SIZE_ABSOLUTE,
	GFX_SIZE_RELATIVE

} GFXSizeClass;


/**
 * Attachment description.
 */
typedef struct GFXAttachment
{
	GFXImageType   type;
	GFXMemoryFlags flags;
	GFXImageUsage  usage;

	GFXFormat format;
	uint32_t  layers;

	// Optionally dynamic size.
	GFXSizeClass size;
	size_t       ref; // Index of the attachment the size is relative to.

	union {
		uint32_t width;
		float xScale;
	};

	union {
		uint32_t height;
		float yScale;
	};

	union {
		uint32_t depth;
		float zScale;
	};

} GFXAttachment;


/**
 * Renderer definition.
 */
typedef struct GFXRenderer GFXRenderer;


/**
 * Pass (i.e. render/compute pass) definition.
 */
typedef struct GFXPass GFXPass;


/**
 * Virtual frame definition.
 */
typedef struct GFXFrame GFXFrame;


/****************************
 * Renderer handling.
 ****************************/

/**
 * Creates a renderer.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @param frames Number of virtual frames, must be > 0 (preferably > 1).
 * @return NULL on failure.
 */
GFX_API GFXRenderer* gfx_create_renderer(GFXDevice* device, unsigned int frames);

/**
 * Destroys a renderer.
 * This will forcefully submit and block until rendering is done!
 */
GFX_API void gfx_destroy_renderer(GFXRenderer* renderer);

/**
 * Describes the properties of an image attachment of a renderer.
 * If the attachment already exists, it will be overwritten.
 * @param renderer Cannot be NULL.
 * @return Zero on failure.
 *
 * The GFX_MEMORY_HOST_VISIBLE flag is ignored, images cannot be mapped!
 * If anything needs to be detached, this will block until rendering is done!
 */
GFX_API int gfx_renderer_attach(GFXRenderer* renderer,
                                size_t index, GFXAttachment attachment);

/**
 * Attaches a window to an attachment index of a renderer.
 * If the attachment already exists, it will be overwritten.
 * @param renderer Cannot be NULL.
 * @param window   Cannot be NULL.
 * @return Zero on failure.
 *
 * Thread-safe with respect to window.
 * If anything needs to be detached, this will block until rendering is done!
 * Fails if the window was already attached to a renderer or the window and
 * renderer do not share a compatible device.
 */
GFX_API int gfx_renderer_attach_window(GFXRenderer* renderer,
                                       size_t index, GFXWindow* window);

/**
 * Detaches an attachment at a given index of a renderer.
 * Undescribed if not a window, detached if a window.
 * @param renderer Cannot be NULL.
 * @param index    Must be < largest attachment index of renderer.
 *
 * If anything is detached, this will block until rendering is done!
 */
GFX_API void gfx_renderer_detach(GFXRenderer* renderer,
                                 size_t index);

/**
 * Retrieves the properties of an image attachment of a renderer.
 * @param renderer Cannot be NULL.
 * @param index    Must be < largest attachment index of renderer.
 * @return Empty attachment of size 0x0x0 if no attachment.
 */
GFX_API GFXAttachment gfx_renderer_get_attach(GFXRenderer* renderer,
                                              size_t index);

/**
 * Retrieves a window at an attachment index of a renderer.
 * @param renderer Cannot be NULL.
 * @param index    Must be < largest attachment index of renderer.
 * @return NULL if no window is attached.
 */
GFX_API GFXWindow* gfx_renderer_get_window(GFXRenderer* renderer,
                                           size_t index);

/**
 * Adds a new (target) pass to the renderer given a set of parent.
 * A pass will be after all its parents in submission order.
 * Each element in parents must be associated with the same renderer.
 * @param renderer   Cannot be NULL.
 * @param numParents Number of parents, 0 for none.
 * @param parents    Parent passes, cannot be NULL if numParents > 0.
 * @return NULL on failure.
 *
 * The renderer shares resources with all passes, it cannot concurrently
 * operate with any pass and passes cannot concurrently operate among themselves.
 */
GFX_API GFXPass* gfx_renderer_add(GFXRenderer* renderer,
                                  size_t numParents, GFXPass** parents);

/**
 * Retrieves the number of target passes of a renderer.
 * A target pass is one that is not a parent off any pass (last in the path).
 * @param renderer Cannot be NULL.
 *
 * This number may change when a new pass is added.
 */
GFX_API size_t gfx_renderer_get_num_targets(GFXRenderer* renderer);

/**
 * Retrieves a target pass of a renderer.
 * @param renderer Can not be NULL.
 * @param target   Target index, must be < gfx_renderer_get_num(renderer).
 *
 * The index of each target may change when a new pass is added,
 * however their order remains fixed during the lifetime of the renderer.
 */
GFX_API GFXPass* gfx_renderer_get_target(GFXRenderer* renderer,
                                         size_t target);

/**
 * Acquires the next virtual frame of a renderer, blocks until available!
 * Cannot be called again until a call to gfx_frame_submit has been made.
 * @param renderer Cannot be NULL.
 * @return Always returns a valid frame.
 *
 * The renderer (or any of its passes) cannot be modified during or after
 * this call until gfx_frame_submit has returned.
 */
GFX_API GFXFrame* gfx_renderer_acquire(GFXRenderer* renderer);

/**
 * Submits the acquired virtual frame of a renderer.
 * Must be called exactly once for each call to gfx_renderer_acquire.
 * @param frame Cannot be NULL.
 *
 * Failure during submission cannot be recovered from,
 * any such failure is appropriately logged.
 */
GFX_API void gfx_frame_submit(GFXFrame* frame);


/****************************
 * Pass handling.
 ****************************/

/**
 * TODO: shader location == in add-order?
 * Consume an attachment of a renderer.
 * @param pass Cannot be NULL.
 * @param mask Access mask to consume the attachment with.
 */
GFX_API int gfx_pass_consume(GFXPass* pass, size_t index, GFXAccessMask mask);

/**
 * Release any consumption of an attachment of the renderer.
 * @param pass Cannot be NULL.
 */
GFX_API void gfx_pass_release(GFXPass* pass, size_t index);

/**
 * Retrieves the number of parents of a pass.
 * @param pass Cannot be NULL.
 */
GFX_API size_t gfx_pass_get_num_parents(GFXPass* pass);

/**
 * Retrieves a parent of a pass.
 * @param pass   Cannot be NULL.
 * @param parent Parent index, must be < gfx_pass_get_num_parents(pass).
 */
GFX_API GFXPass* gfx_pass_get_parent(GFXPass* pass, size_t parent);

/**
 * TODO: Totally temporary!
 * Makes the pass render the given things.
 */
GFX_API void gfx_pass_use(GFXPass* pass,
                          GFXPrimitive* primitive, GFXGroup* group);


#endif
