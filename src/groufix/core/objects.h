/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_OBJECTS_H
#define _GFX_CORE_OBJECTS_H

#include "groufix/containers/vec.h"
#include "groufix/core.h"


/****************************
 * Shading objects.
 ****************************/

/**
 * Internal shader.
 */
struct GFXShader
{
	_GFXDevice*  device; // Associated GPU to use as target environment.
	_GFXContext* context;

	GFXShaderStage stage;


	// Vulkan fields.
	struct
	{
		VkShaderModule module;

	} vk;
};


/****************************
 * Render objects.
 ****************************/

/**
 * Image (implicit) attachment.
 */
typedef struct _GFXImageAttach
{
	GFXAttachment base;


	// Vulkan fields.
	struct
	{
		VkImage     image;
		VkImageView view;

	} vk;

} _GFXImageAttach;


/**
 * Window attachment.
 */
typedef struct _GFXWindowAttach
{
	_GFXWindow* window;
	uint32_t    image; // Swapchain image index (or UINT32_MAX).


	// Vulkan fields.
	struct
	{
		GFXVec        views; // Stores VkImageView, on-swapchain recreate.
		VkCommandPool pool;

	} vk;

} _GFXWindowAttach;


/**
 * Internal attachment description.
 */
typedef struct _GFXAttach
{
	// Attachment type.
	enum
	{
		_GFX_ATTACH_EMPTY,
		_GFX_ATTACH_IMAGE,
		_GFX_ATTACH_WINDOW

	} type;

	// Attachment data.
	union
	{
		_GFXImageAttach image;
		_GFXWindowAttach window;
	};

} _GFXAttach;


/**
 * Internal renderer.
 */
struct GFXRenderer
{
	_GFXContext* context;


	// Chosen graphics family.
	struct
	{
		uint32_t   family;
		VkQueue    queue; // Queue chosen from the family.
		_GFXMutex* lock;

	} graphics;


	// Render frame (i.e. attachments).
	struct
	{
		GFXVec attachs; // Stores _GFXAttach.

		int built;

	} frame;


	// Render graph.
	struct
	{
		GFXVec targets; // Stores GFXRenderPass* (target passes, tree roots).
		GFXVec passes;  // Stores GFXRenderPass* (in submission order).

		int built;

	} graph;
};


/**
 * Internal render pass.
 */
struct GFXRenderPass
{
	GFXRenderer* renderer;
	unsigned int level; // Determines submission order.

	GFXVec reads;  // Stores size_t.
	GFXVec writes; // Stores size_t.


	// Building output (can be invalidated).
	struct
	{
		size_t backing; // Attachment index (or SIZE_MAX).

		// TODO: Super temporary!!
		GFXShader* vertex;
		GFXShader* fragment;

	} build;


	// Vulkan fields.
	struct
	{
		VkRenderPass     pass;
		VkPipelineLayout layout;
		VkPipeline       pipeline;
		GFXVec           framebuffers; // Stores VkFramebuffer.
		GFXVec           commands;     // Stores VkCommandBuffer.

	} vk;


	size_t         numDeps;
	GFXRenderPass* deps[]; // Dependency passes.
};


/****************************
 * Renderer's render- frame, graph and pass.
 ****************************/

/**
 * Initializes the render frame of a renderer.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_frame_init(GFXRenderer* renderer);

/**
 * Clears the render frame of a renderer, destroying all images.
 * @param renderer Cannot be NULL.
 *
 * If attachments exist, will block until rendering is done!
 */
void _gfx_render_frame_clear(GFXRenderer* renderer);

/**
 * Builds not yet built resources of the render frame.
 * Will resolve to a no-op if everything is already built.
 * @param renderer Cannot be NULL.
 * @return Non-zero if the entire frame is in a built state.
 */
int _gfx_render_frame_build(GFXRenderer* renderer);

/**
 * Signals the render frame to (re)build resources dependent on the given
 * attachment index, building may be postponed to _gfx_render_frame_build.
 * Suitable for on-swapchain recreate (e.g. a window resize or smth).
 * @param renderer Cannot be NULL.
 *
 * If rebuilding swapchain resources, this will block until rendering is done!
 */
void _gfx_render_frame_rebuild(GFXRenderer* renderer, size_t index);

/**
 * Initializes the render graph of a renderer.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_graph_init(GFXRenderer* renderer);

/**
 * Clears the render graph of a renderer, destroying all passes.
 * @param renderer Cannot be NULL.
 *
 * If passes exist, will block until rendering is done!
 */
void _gfx_render_graph_clear(GFXRenderer* renderer);

/**
 * (Re)builds the render graph and all its resources.
 * Will resolve to a no-op if everything is already built.
 * @param renderer Cannot be NULL.
 * @param Non-zero if the entire graph is in a built state.
 *
 * If using swapchain resources, this will block until rendering is done!
 */
int _gfx_render_graph_build(GFXRenderer* renderer);

/**
 * Signals the render graph to (re)build resources dependent on the given
 * attachment index, building may be postponed to _gfx_render_graph_build.
 * Suitable for on-swapchain recreate (e.g. a window resize or smth).
 * @param renderer Cannot be NULL.
 *
 * Does not synchronize anything before potentially rebuilding!
 */
void _gfx_render_graph_rebuild(GFXRenderer* renderer, size_t index);

/**
 * Immediately destruct everything that depends on the attachment at index.
 * This will implicitly trigger a rebuild for obvious reasons.
 * @param renderer Cannot be NULL.
 *
 * Must be called before detaching any attachment!
 * It will in turn call the relevant _gfx_render_pass_destruct calls.
 * Also, does not synchronize anything before destructing!
 */
void _gfx_render_graph_destruct(GFXRenderer* renderer, size_t index);

/**
 * Creates a render pass, referencing all dependencies.
 * Each element in deps must be associated with the same renderer.
 * @param renderer Cannot be NULL.
 * @param numDeps  Number of dependencies, 0 for none.
 * @param deps     Passes it depends on, cannot be NULL if numDeps > 0.
 * @return NULL on failure.
 */
GFXRenderPass* _gfx_create_render_pass(GFXRenderer* renderer,
                                       size_t numDeps, GFXRenderPass** deps);

/**
 * Destroys a render pass, unreferencing all dependencies.
 * Undefined behaviour if destroying a pass that is referenced by another.
 * @param pass Cannot be NULL.
 */
void _gfx_destroy_render_pass(GFXRenderPass* pass);

/**
 * TODO: Dependencies.
 * TODO: Build recursively?
 * TODO: Merge passes with the same resolution into subpasses.
 * (Re)builds the Vulkan object structure.
 * @param pass Cannot be NULL.
 * @return Non-zero if valid and built.
 *
 * Does not synchronize anything before rebuilding!
 */
int _gfx_render_pass_build(GFXRenderPass* pass);

/**
 * Destructs the Vulkan object structure, non-recursively.
 * @param pass Cannot be NULL.
 *
 * Must be called before detaching any attachment it uses!
 * Also, does not synchronize anything before destructing!
 */
void _gfx_render_pass_destruct(GFXRenderPass* pass);


#endif
