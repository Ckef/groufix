/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_OBJECTS_H
#define _GFX_CORE_OBJECTS_H

#include "groufix/containers/deque.h"
#include "groufix/containers/list.h"
#include "groufix/containers/vec.h"
#include "groufix/core/mem.h"
#include "groufix/core.h"


/****************************
 * Memory objects.
 ****************************/

/**
 * Internal heap.
 */
struct GFXHeap
{
	_GFXAllocator allocator; // its context member is the used _GFXContext*.
	_GFXMutex     lock;

	GFXList buffers; // References _GFXBuffer.
	GFXList images;  // References _GFXImage.
	GFXList meshes;  // References _GFXMesh.
};


/**
 * Internal buffer.
 */
typedef struct _GFXBuffer
{
	GFXBuffer   base;
	GFXHeap*    heap;
	GFXListNode list;

	_GFXMemAlloc alloc;


	// Vulkan fields.
	struct
	{
		VkBuffer buffer;

	} vk;

} _GFXBuffer;


/**
 * TODO: Incomplete definition.
 * Internal image.
 */
typedef struct _GFXImage
{
	GFXImage    base;
	GFXHeap*    heap;
	GFXListNode list;

	_GFXMemAlloc alloc;


	// Vulkan fields.
	struct
	{
		VkImage image;

	} vk;

} _GFXImage;


/**
 * Internal mesh (superset of buffer).
 */
typedef struct _GFXMesh
{
	GFXMesh      base;
	_GFXBuffer   buffer; // vk.buffer is VK_NULL_HANDLE if nothing is allocated.

	GFXBufferRef refVertex; // Can be GFX_REF_NULL.
	GFXBufferRef refIndex;  // Can be GFX_REF_NULL.

	size_t stride; // i.e. vertex size in bytes.
	size_t indexSize;

	size_t numAttribs;
	size_t offsets[]; // TODO: Add format to attributes?

} _GFXMesh;


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
 * Internal renderer objects.
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
		VkCommandPool pool;  // TODO: Temporary?

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
 * Internal virtual frame swapchain reference.
 */
typedef struct _GFXFrameSwap
{
	GFXWindow* window;
	uint32_t   image; // Swapchain image index (or UINT32_MAX).


	// Vulkan fields.
	struct
	{
		VkSemaphore available;

	} vk;

} _GFXFrameSwap;


/**
 * Internal virtual frame.
 * TODO: Add command buffers.
 */
typedef struct _GFXFrame
{
	GFXVec swaps; // Stores _GFXFrameSwap.


	// Vulkan fields.
	struct
	{
		VkSemaphore rendered;
		VkFence     done; // For resource access.

	} vk;

} _GFXFrame;


/****************************
 * User visible renderer objects.
 ****************************/

/**
 * Internal renderer.
 */
struct GFXRenderer
{
	_GFXContext* context;
	_GFXQueue    graphics;
	_GFXQueue    present;

	// Render frame (i.e. collection of virtual frames).
	GFXDeque     frames; // Stores _GFXFrame.


	// Render backing (i.e. attachments).
	struct
	{
		GFXVec attachs; // Stores _GFXAttach.

		size_t numWindows; // For each frame's swapchain refs.
		int    built;

	} backing;


	// Render graph (directed acyclic graph of passes).
	struct
	{
		GFXVec targets; // Stores GFXRenderPass* (target passes, tree roots).
		GFXVec passes;  // Stores GFXRenderPass* (in submission order).

		int built;
		int valid;

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
		_GFXMesh*  mesh;
		GFXShader* vertex;
		GFXShader* fragment;

	} build;


	// Vulkan fields.
	struct
	{
		// TODO: Most of these are temporary..
		VkRenderPass     pass;
		VkPipelineLayout layout;
		VkPipeline       pipeline;
		GFXVec           framebuffers; // Stores VkFramebuffer.
		GFXVec           commands;     // Stores VkCommandBuffer.

	} vk;


	// Dependency passes.
	size_t         numDeps;
	GFXRenderPass* deps[];
};


/****************************
 * Resource reference operations.
 ****************************/

/**
 * Unpacked memory resource reference.
 */
typedef struct _GFXUnpackRef
{
	// Referenced object.
	struct
	{
		_GFXBuffer*   buffer;
		_GFXMesh*     mesh; // If not NULL, buffer will not be NULL either!
		_GFXImage*    image;
		GFXRenderer*  renderer;

	} obj;


	// Reference value,
	//  buffer offset | attachment index.
	size_t value;

} _GFXUnpackRef;


/**
 * Resolves a memory reference, meaning:
 * if it references a reference, it will recursively return that reference.
 * @return A reference to the user-visible object actually holding the memory.
 *
 * Assumes no self-references exist!
 */
GFXReference _gfx_ref_resolve(GFXReference ref);

/**
 * Resolves & unpacks a memory resource reference, meaning:
 * the related referenced objects and values are retrieved, if an object is
 * composed of other memory objects internally, those will be 'unpacked' as well.
 *
 * Comes with free reference validation when in debug mode!
 */
_GFXUnpackRef _gfx_ref_unpack(GFXReference ref);


/****************************
 * Virtual 'render' frame.
 ****************************/

/**
 * Initializes a virtual frame.
 * @param context Cannot be NULL.
 * @param frame   Cannot be NULL.
 * @return Zero on failure.
 */
int _gfx_frame_init(_GFXContext* context, _GFXFrame* frame);

/**
 * Clears a virtual frame for a renderer, will block until done.
 * @param context Must be the same context given in _gfx_frame_init.
 * @param frame   Cannot be NULL.
 */
void _gfx_frame_clear(_GFXContext* context, _GFXFrame* frame);

/**
 * Records & submits the frame, taking input from a renderer.
 * This will block until frame is available, reusing previous resources.
 * @param frame    Cannot be NULL.
 * @param renderer Must use the same context given in _gfx_frame_init.
 * @return Zero if the renderer could not be built.
 */
int _gfx_frame_submit(_GFXFrame* frame, GFXRenderer* renderer);


/****************************
 * Render- backing and graph.
 ****************************/

/**
 * Initializes the render backing of a renderer.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_backing_init(GFXRenderer* renderer);

/**
 * Clears the render backing of a renderer, destroying all images.
 * @param renderer Cannot be NULL.
 *
 * If attachments exist, will block until rendering is done!
 */
void _gfx_render_backing_clear(GFXRenderer* renderer);

/**
 * Builds not yet built resources of the render backing.
 * Will resolve to a no-op if everything is already built.
 * @param renderer Cannot be NULL.
 * @return Non-zero if the entire backing is in a built state.
 */
int _gfx_render_backing_build(GFXRenderer* renderer);

/**
 * Signals the render backing to (re)build resources dependent on the given
 * attachment index, building may be postponed to _gfx_render_backing_build.
 * Suitable for on-swapchain recreate (e.g. a window resize or smth).
 * @param renderer Cannot be NULL.
 * @param flags    What resources should be recreated (0 to do nothing).
 *
 * If rebuilding swapchain resources, this will block until rendering is done!
 */
void _gfx_render_backing_rebuild(GFXRenderer* renderer, size_t index,
                                 _GFXRecreateFlags flags);

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
 * If the graph got invalidated, this will block until rendering is done!
 */
int _gfx_render_graph_build(GFXRenderer* renderer);

/**
 * Signals the render graph to (re)build resources dependent on the given
 * attachment index, building may be postponed to _gfx_render_graph_build.
 * Suitable for on-swapchain recreate (e.g. a window resize or smth).
 * @param renderer Cannot be NULL.
 * @param flags    What resources should be recreated (0 to do nothing).
 *
 * Does not synchronize anything before potentially rebuilding!
 */
void _gfx_render_graph_rebuild(GFXRenderer* renderer, size_t index,
                               _GFXRecreateFlags flags);

/**
 * Immediately destruct everything that depends on the attachment at index.
 * This will implicitly trigger a rebuild for obvious reasons.
 * @param renderer Cannot be NULL.
 *
 * Must be called before detaching the attachment at index!
 * It will in turn call the relevant _gfx_render_pass_destruct calls.
 * Also, does not synchronize anything before destructing!
 */
void _gfx_render_graph_destruct(GFXRenderer* renderer, size_t index);

/**
 * Invalidates the render graph, forcing it to destruct and rebuild everything
 * the next time _gfx_render_graph_build is called.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_graph_invalidate(GFXRenderer* renderer);


/****************************
 * Render pass (nodes in the render graph).
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
 * TODO: Currently it builds a lot, this will be offloaded to different objects.
 * TODO: Merge passes with the same resolution into subpasses.
 * (Re)builds the Vulkan object structure.
 * @param pass  Cannot be NULL.
 * @param flags What resources should be recreated (0 to recreate nothing).
 * @return Non-zero if valid and built.
 *
 * If a resource does not exist yet, it will be built no matter what flags is.
 * Does not synchronize anything before rebuilding!
 */
int _gfx_render_pass_build(GFXRenderPass* pass,
                           _GFXRecreateFlags flags);

/**
 * Destructs the Vulkan object structure, non-recursively.
 * @param pass Cannot be NULL.
 *
 * Must be called before detaching any attachment it uses!
 * Also, does not synchronize anything before destructing!
 */
void _gfx_render_pass_destruct(GFXRenderPass* pass);


#endif
