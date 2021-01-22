/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_OBJECTS_H
#define _GFX_CORE_OBJECTS_H

#include "groufix/core.h"


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
 * Renderer & render graphs.
 ****************************/

/**
 * Internal attachment description.
 */
typedef struct _GFXAttach
{
	size_t        index;
	GFXAttachment base;

} _GFXAttach;


/**
 * Window attachment.
 */
typedef struct _GFXWindowAttach
{
	size_t      index;
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
 * Internal renderer.
 */
struct GFXRenderer
{
	_GFXContext* context;

	GFXVec attachs; // Stores _GFXAttach.
	GFXVec windows; // Stores _GFXWindowAttach.

	GFXVec targets; // Stores GFXRenderPass* (target passes, roots of trees).
	GFXVec passes;  // Stores GFXRenderPass* (in submission order).

	int built;


	// Chosen graphics family.
	struct
	{
		uint32_t   family;
		VkQueue    queue; // Queue chosen from the family.
		_GFXMutex* lock;

	} graphics;
};


/**
 * Internal render pass.
 */
struct GFXRenderPass
{
	GFXRenderer* renderer;
	unsigned int level; // Determines submission order.
	unsigned int refs;  // Number of passes that depend on this one.

	GFXVec reads;  // Stores size_t.
	GFXVec writes; // Stores size_t.


	// TODO: Super temporary!!
	GFXShader* vertex;
	GFXShader* fragment;


	// Building output (can be invalidated).
	struct
	{
		size_t backing; // Index into renderer->windows (or SIZE_MAX).

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
 * Render pass management.
 ****************************/

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
 * If writing to a window attachment, _gfx_render_pass_destruct must be called
 * before the window is detached.
 */
int _gfx_render_pass_rebuild(GFXRenderPass* pass);

/**
 * Destructs the Vulkan object structure, non-recursively.
 * @param pass Cannot be NULL.
 *
 * Does not synchronize anything before destructing!
 * If built, this must be called before detaching the output window attachment.
 */
void _gfx_render_pass_destruct(GFXRenderPass* pass);


#endif
