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
	GFXList groups;  // References _GFXGroup.
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

	size_t       numAttribs;
	GFXAttribute attribs[];

} _GFXMesh;


/**
 * TODO: Incomplete definition.
 * Internal resource group (superset of buffer).
 */
typedef struct _GFXGroup
{
	GFXGroup   base;
	_GFXBuffer buffer; // vk.buffer is VK_NULL_HANDLE if nothing is allocated.

	size_t     numBindings;
	GFXBinding bindings[];

} _GFXGroup;


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
	_GFXWindow*       window;
	_GFXRecreateFlags flags; // Used by virtual frames, from last submission.


	// Vulkan fields.
	struct
	{
		GFXVec views; // Stores VkImageView, on-swapchain recreate.

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
 * Virtual frame synchronization object.
 */
typedef struct _GFXFrameSync
{
	_GFXWindow* window;
	size_t      backing; // Attachment index.
	uint32_t    image;   // Swapchain image index (or UINT32_MAX).


	// Vulkan fields.
	struct
	{
		VkSemaphore available;

	} vk;

} _GFXFrameSync;


/**
 * Internal virtual frame.
 */
typedef struct _GFXFrame
{
	GFXVec refs;  // Stores size_t, for each attachment; index into syncs (or SIZE_MAX).
	GFXVec syncs; // Stores _GFXFrameSync.


	// Vulkan fields.
	struct
	{
		VkCommandBuffer cmd;
		VkSemaphore     rendered;
		VkFence         done; // For resource access.

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

		int built;

	} backing;


	// Render graph (directed acyclic graph of passes).
	struct
	{
		GFXVec targets; // Stores GFXRenderPass* (target passes, tree roots).
		GFXVec passes;  // Stores GFXRenderPass* (in submission order).

		int built;
		int valid;

	} graph;


	// Vulkan fields.
	struct
	{
		VkCommandPool pool; // TODO: Multiple for threaded recording?

	} vk;
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
		size_t backing; // Window attachment index (or SIZE_MAX).

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
		_GFXImage*    image;
		GFXRenderer*  renderer;

	} obj;


	// Unpacked reference value,
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
 * if an object is composed of other memory objects internally, it will be
 * 'unpacked' into its elementary non-composed memory objects.
 *
 * Comes with free reference validation when in debug mode!
 */
_GFXUnpackRef _gfx_ref_unpack(GFXReference ref);


/****************************
 * Virtual 'render' frame.
 ****************************/

/**
 * Initializes a virtual frame of a renderer.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @return Zero on failure.
 */
int _gfx_frame_init(GFXRenderer* renderer, _GFXFrame* frame);

/**
 * Clears a virtual frame of a renderer.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 *
 * This will block until the frame is done rendering!
 */
void _gfx_frame_clear(GFXRenderer* renderer, _GFXFrame* frame);

/**
 * Records & submits a virtual frame.
 * This will block until pending submissions of the frame are done rendering,
 * where possible it will reuse its resources afterwards.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL, may not be in renderer->frames!.
 * @return Zero if the frame (or renderer) could not be built.
 *
 * This may call _gfx_sync_frames internally on-swapchain recreate.
 * Failure is considered fatal, swapchains could be left in an incomplete state.
 */
int _gfx_frame_submit(GFXRenderer* renderer, _GFXFrame* frame);

/**
 * Blocks until all virtual frames of a renderer are done.
 * @param renderer Cannot be NULL.
 * @return Non-zero if successfully synchronized.
 */
int _gfx_sync_frames(GFXRenderer* renderer);


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
 * If window attachments exist, will block until rendering is done!
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
 * (Re)builds render backing resources dependent on the given attachment index.
 * Suitable for on-swapchain recreate (e.g. a window resize or smth).
 * @param renderer Cannot be NULL.
 * @param flags    Must contain the _GFX_RECREATE bit.
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
 * (Re)builds render pass resources dependent on the given attachment index.
 * Suitable for on-swapchain recreate (e.g. a window resize or smth).
 * @param renderer Cannot be NULL.
 * @param flags    Must contain the _GFX_RECREATE bit.
 */
void _gfx_render_graph_rebuild(GFXRenderer* renderer, size_t index,
                               _GFXRecreateFlags flags);

/**
 * Immediately destruct everything that depends on the attachment at index.
 * @param renderer Cannot be NULL.
 *
 * Must be called before detaching the attachment at index!
 * It will in turn call the relevant _gfx_render_pass_destruct calls.
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
 */
int _gfx_render_pass_build(GFXRenderPass* pass,
                           _GFXRecreateFlags flags);

/**
 * Records the pass into the command buffers of a frame.
 * The frame's command buffers must be in the recording state (!).
 * @param pass  Cannot be NULL.
 * @param frame Cannot be NULL, must be of the same renderer as pass.
 *
 * No-op if the pass is not built.
 */
void _gfx_render_pass_record(GFXRenderPass* pass, _GFXFrame* frame);

/**
 * Destructs the Vulkan object structure, non-recursively.
 * @param pass Cannot be NULL.
 *
 * Must be called before detaching any attachment it uses!
 */
void _gfx_render_pass_destruct(GFXRenderPass* pass);


#endif
