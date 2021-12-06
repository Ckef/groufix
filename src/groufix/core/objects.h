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


#define _GFX_GET_VK_PRIMITIVE_TOPOLOGY(topo) \
	(((topo) == GFX_TOPO_POINT_LIST) ? \
		VK_PRIMITIVE_TOPOLOGY_POINT_LIST : \
	((topo) == GFX_TOPO_LINE_LIST) ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST : \
	((topo) == GFX_TOPO_LINE_STRIP) ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_STRIP : \
	((topo) == GFX_TOPO_TRIANGLE_LIST) ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : \
	((topo) == GFX_TOPO_TRIANGLE_STRIP) ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : \
	((topo) == GFX_TOPO_TRIANGLE_FAN) ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN : \
	((topo) == GFX_TOPO_LINE_LIST_ADJACENT) ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY : \
	((topo) == GFX_TOPO_LINE_STRIP_ADJACENT) ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY : \
	((topo) == GFX_TOPO_TRIANGLE_LIST_ADJACENT) ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY : \
	((topo) == GFX_TOPO_TRIANGLE_STRIP_ADJACENT) ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY : \
	((topo) == GFX_TOPO_PATCH_LIST) ? \
		VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)

#define _GFX_GET_VK_BUFFER_USAGE(flags, usage) \
	(((flags) & GFX_MEMORY_READ ? \
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT : (VkBufferUsageFlags)0) | \
	((flags) & GFX_MEMORY_WRITE ? \
		VK_BUFFER_USAGE_TRANSFER_DST_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_VERTEX ? \
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_INDEX ? \
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_UNIFORM ? \
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_INDIRECT ? \
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_STORAGE ? \
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_UNIFORM_TEXEL ? \
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_STORAGE_TEXEL ? \
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT : (VkBufferUsageFlags)0))

#define _GFX_GET_VK_IMAGE_TYPE(type) \
	(((type) == GFX_IMAGE_1D) ? VK_IMAGE_TYPE_1D : \
	((type) == GFX_IMAGE_2D) ? VK_IMAGE_TYPE_2D : \
	((type) == GFX_IMAGE_3D) ? VK_IMAGE_TYPE_3D : \
	((type) == GFX_IMAGE_3D_SLICED) ? VK_IMAGE_TYPE_3D : \
	((type) == GFX_IMAGE_CUBEMAP) ? VK_IMAGE_TYPE_2D : \
	VK_IMAGE_TYPE_2D)

#define _GFX_GET_VK_IMAGE_ASPECT(aspect) \
	(((aspect) & GFX_IMAGE_COLOR ? \
		VK_IMAGE_ASPECT_COLOR_BIT : (VkImageAspectFlags)0) | \
	((aspect) & GFX_IMAGE_DEPTH ? \
		VK_IMAGE_ASPECT_DEPTH_BIT : (VkImageAspectFlags)0) | \
	((aspect) & GFX_IMAGE_STENCIL ? \
		VK_IMAGE_ASPECT_STENCIL_BIT : (VkImageAspectFlags)0))

#define _GFX_GET_VK_IMAGE_USAGE(flags, usage) \
	(((flags) & GFX_MEMORY_READ ? \
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT : (VkImageUsageFlags)0) | \
	((flags) & GFX_MEMORY_WRITE ? \
		VK_IMAGE_USAGE_TRANSFER_DST_BIT : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED ? \
		VK_IMAGE_USAGE_SAMPLED_BIT : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED_LINEAR ? \
		VK_IMAGE_USAGE_SAMPLED_BIT : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED_MINMAX ? \
		VK_IMAGE_USAGE_SAMPLED_BIT : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_STORAGE ? \
		VK_IMAGE_USAGE_STORAGE_BIT : (VkImageUsageFlags)0))

#define _GFX_GET_VK_FORMAT_FEATURES(flags, usage) \
	(((flags) & GFX_MEMORY_READ ? \
		VK_FORMAT_FEATURE_TRANSFER_SRC_BIT : (VkFormatFeatureFlags)0) | \
	((flags) & GFX_MEMORY_WRITE ? \
		VK_FORMAT_FEATURE_TRANSFER_DST_BIT : (VkFormatFeatureFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED ? \
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT : (VkFormatFeatureFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED_LINEAR ? \
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT : (VkFormatFeatureFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED_MINMAX ? \
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT : (VkFormatFeatureFlags)0) | \
	((usage) & GFX_IMAGE_STORAGE ? \
		VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT : (VkFormatFeatureFlags)0))

#define _GFX_GET_VK_ACCESS_FLAGS(mask, fmt) \
	(((mask) & GFX_ACCESS_VERTEX_READ ? \
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_INDEX_READ ? \
		VK_ACCESS_INDEX_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_UNIFORM_READ ? \
		VK_ACCESS_UNIFORM_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_INDIRECT_READ ? \
		VK_ACCESS_INDIRECT_COMMAND_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_SAMPLED_READ ? \
		VK_ACCESS_SHADER_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_STORAGE_READ ? \
		VK_ACCESS_SHADER_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_STORAGE_WRITE ? \
		VK_ACCESS_SHADER_WRITE_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_INPUT ? \
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_READ ? \
		(GFX_FORMAT_HAS_DEPTH(fmt) || GFX_FORMAT_HAS_STENCIL(fmt) ? \
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : \
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT) : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_WRITE ? \
		(GFX_FORMAT_HAS_DEPTH(fmt) || GFX_FORMAT_HAS_STENCIL(fmt) ? \
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : \
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_TRANSFER_READ ? \
		VK_ACCESS_TRANSFER_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_TRANSFER_WRITE ? \
		VK_ACCESS_TRANSFER_WRITE_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_HOST_READ ? \
		VK_ACCESS_HOST_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_HOST_WRITE ? \
		VK_ACCESS_HOST_WRITE_BIT : (VkAccessFlags)0))

#define _GFX_GET_VK_IMAGE_LAYOUT(mask, fmt) \
	(((mask) & GFX_ACCESS_ATTACHMENT_INPUT) && \
	!((mask) & ~(GFXAccessMask)( \
	GFX_ACCESS_ATTACHMENT_INPUT | GFX_ACCESS_SAMPLED_READ | GFX_ACCESS_DISCARD)) ? \
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : \
	((mask) & GFX_ACCESS_SAMPLED_READ) && \
	!((mask) & ~(GFXAccessMask)( \
	GFX_ACCESS_SAMPLED_READ | GFX_ACCESS_COMPUTE_ASYNC | GFX_ACCESS_DISCARD)) ? \
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : \
	((mask) & GFX_ACCESS_ATTACHMENT_READ) && \
	!((mask) & ~(GFXAccessMask)( \
	GFX_ACCESS_ATTACHMENT_READ | GFX_ACCESS_DISCARD)) ? \
		(GFX_FORMAT_HAS_DEPTH(fmt) || GFX_FORMAT_HAS_STENCIL(fmt) ? \
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : \
			VK_IMAGE_LAYOUT_GENERAL) : /* Only attachment read makes no sense. */ \
	((mask) & GFX_ACCESS_ATTACHMENT_WRITE) && \
	!((mask) & ~(GFXAccessMask)( \
	GFX_ACCESS_ATTACHMENT_READ | GFX_ACCESS_ATTACHMENT_WRITE | GFX_ACCESS_DISCARD)) ? \
		(GFX_FORMAT_HAS_DEPTH(fmt) || GFX_FORMAT_HAS_STENCIL(fmt) ? \
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : \
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) : \
	((mask) & GFX_ACCESS_TRANSFER_READ) && \
	!((mask) & ~(GFXAccessMask)( \
	GFX_ACCESS_TRANSFER_READ | GFX_ACCESS_TRANSFER_ASYNC | GFX_ACCESS_DISCARD)) ? \
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : \
	((mask) & GFX_ACCESS_TRANSFER_WRITE) && \
	!((mask) & ~(GFXAccessMask)( \
	GFX_ACCESS_TRANSFER_WRITE | GFX_ACCESS_TRANSFER_ASYNC | GFX_ACCESS_DISCARD)) ? \
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : \
	(mask == 0) ? \
		VK_IMAGE_LAYOUT_UNDEFINED : /* Default is to discard. */ \
		VK_IMAGE_LAYOUT_GENERAL)

#define _GFX_GET_VK_PIPELINE_STAGE(mask, fmt, stage) \
	(((mask) & GFX_ACCESS_VERTEX_READ ? \
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_INDEX_READ ? \
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_INDIRECT_READ ? \
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT : (VkPipelineStageFlags)0) | \
	((mask) & (GFX_ACCESS_UNIFORM_READ | GFX_ACCESS_SAMPLED_READ | \
	GFX_ACCESS_STORAGE_READ | GFX_ACCESS_STORAGE_WRITE | GFX_ACCESS_ATTACHMENT_INPUT) ? \
		(((stage) == 0 || ((stage) & GFX_STAGE_VERTEX) ? \
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : (VkPipelineStageFlags)0) | \
		((stage) == 0 || ((stage) & GFX_STAGE_TESS_CONTROL) ? \
			VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT : (VkPipelineStageFlags)0) | \
		((stage) == 0 || ((stage) & GFX_STAGE_TESS_EVALUATION) ? \
			VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT : (VkPipelineStageFlags)0) | \
		((stage) == 0 || ((stage) & GFX_STAGE_GEOMETRY) ? \
			VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT : (VkPipelineStageFlags)0) | \
		((stage) == 0 || ((stage) & GFX_STAGE_FRAGMENT) ? \
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : (VkPipelineStageFlags)0) | \
		((stage) == 0 || ((stage) & GFX_STAGE_COMPUTE) ? \
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : (VkPipelineStageFlags)0)) : \
		(VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_READ ? \
		(GFX_FORMAT_HAS_DEPTH(fmt) || GFX_FORMAT_HAS_STENCIL(fmt) ? \
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | \
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : \
			(VkPipelineStageFlags)0) : /* Only attachment read makes no sense. */ \
			(VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_WRITE ? \
		(GFX_FORMAT_HAS_DEPTH(fmt) || GFX_FORMAT_HAS_STENCIL(fmt) ? \
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | \
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : \
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_TRANSFER_READ ? \
		VK_PIPELINE_STAGE_TRANSFER_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_TRANSFER_WRITE ? \
		VK_PIPELINE_STAGE_TRANSFER_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_HOST_READ ? \
		VK_PIPELINE_STAGE_HOST_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_HOST_WRITE ? \
		VK_PIPELINE_STAGE_HOST_BIT : (VkPipelineStageFlags)0))


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
 * Memory objects.
 ****************************/

/**
 * Internal heap.
 */
struct GFXHeap
{
	_GFXDevice*   device;    // For format operations.
	_GFXAllocator allocator; // Its context member is the used _GFXContext*.
	_GFXMutex     lock;
	_GFXQueue     graphics;
	_GFXQueue     transfer;

	GFXList buffers;    // References _GFXBuffer.
	GFXList images;     // References _GFXImage.
	GFXList primitives; // References _GFXPrimitive.
	GFXList groups;     // References _GFXGroup.


	// Vulkan fields.
	struct
	{
		VkCommandPool gPool; // Graphics pool.
		VkCommandPool tPool; // Transfer pool.
		_GFXMutex     gLock;
		_GFXMutex     tLock;

	} vk;
};


/**
 * Staging buffer.
 */
typedef struct _GFXStaging
{
	GFXListNode  list;  // Base-type, linked into the parent object.
	_GFXMemAlloc alloc; // Stores the size.


	// Vulkan fields.
	struct
	{
		VkBuffer buffer;
		void*    ptr;

	} vk;

} _GFXStaging;


/**
 * Internal buffer.
 */
typedef struct _GFXBuffer
{
	GFXBuffer   base;
	GFXHeap*    heap;
	GFXListNode list;

	_GFXMemAlloc alloc;
	GFXList      staging; // References _GFXStaging.


	// Vulkan fields.
	struct
	{
		VkBuffer buffer;

	} vk;

} _GFXBuffer;


/**
 * Internal image.
 */
typedef struct _GFXImage
{
	GFXImage    base;
	GFXHeap*    heap;
	GFXListNode list;

	_GFXMemAlloc alloc;
	GFXList      staging; // References _GFXStaging.


	// Vulkan fields.
	struct
	{
		VkFormat format;
		VkImage  image;

	} vk;

} _GFXImage;


/**
 * Internal primitive geometry (superset of buffer).
 */
typedef struct _GFXPrimitive
{
	GFXPrimitive base;
	_GFXBuffer   buffer; // vk.buffer is VK_NULL_HANDLE if nothing is allocated.

	GFXBufferRef refVertex; // Can be GFX_REF_NULL.
	GFXBufferRef refIndex;  // Can be GFX_REF_NULL.

	size_t       numAttribs;
	GFXAttribute attribs[];

} _GFXPrimitive;


/**
 * Internal resource group (superset of buffer).
 */
typedef struct _GFXGroup
{
	GFXGroup   base;
	_GFXBuffer buffer; // vk.buffer is VK_NULL_HANDLE if nothing is allocated.

	size_t     numBindings;
	GFXBinding bindings[]; // No reference is GFX_REF_NULL!

} _GFXGroup;


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
		VkFormat    format;
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
 * Frame synchronization (swapchain acquisition) object.
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


/****************************
 * User visible renderer objects.
 ****************************/

/**
 * Internal virtual frame.
 */
struct GFXFrame
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
};


/**
 * Internal renderer.
 */
struct GFXRenderer
{
	_GFXDevice*  device; // For format operations.
	_GFXContext* context;
	_GFXQueue    graphics;
	_GFXQueue    present;

	// Render frame (i.e. collection of virtual frames).
	GFXDeque frames; // Stores GFXFrame.
	GFXFrame pFrame; // Public frame, vk.done is VK_NULL_HANDLE if absent.


	// Render backing (i.e. attachments).
	struct
	{
		_GFXAllocator allocator;
		GFXVec        attachs; // Stores _GFXAttach.

		int built;

	} backing;


	// Render graph (directed acyclic graph of passes).
	struct
	{
		GFXVec targets; // Stores GFXPass* (target passes, tree roots).
		GFXVec passes;  // Stores GFXPass* (in submission order).

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
 * Internal pass (i.e. render/compute pass).
 */
struct GFXPass
{
	GFXRenderer* renderer;
	unsigned int level; // Determines submission order.

	GFXVec consumes; // Stores { size_t, GFXAccessMask }.


	// Building output (can be invalidated).
	struct
	{
		size_t backing; // Window attachment index (or SIZE_MAX).

		// TODO: Super temporary!!
		_GFXPrimitive* primitive;
		_GFXGroup* group;
		GFXShader* vertex;
		GFXShader* fragment;

	} build;


	// Vulkan fields.
	struct
	{
		// TODO: Most of these are temporary..
		VkRenderPass          pass;
		GFXVec                framebuffers; // Stores VkFramebuffer.
		VkDescriptorSetLayout setLayout;
		VkDescriptorPool      pool;
		VkSampler             sampler;
		VkImageView           view;
		VkDescriptorSet       set;
		VkPipelineLayout      pipeLayout;
		VkPipeline            pipeline;

	} vk;


	// Parent passes.
	size_t   numParents;
	GFXPass* parents[];
};


/****************************
 * Resource reference operations.
 ****************************/

/**
 * Unpacked memory resource reference.
 * Access is not thread-safe with respect to the referenced object (!).
 */
typedef struct _GFXUnpackRef
{
	// Unpacked reference value(s),
	//  buffer offset | attachment index.
	uint64_t value;


	// Referenced object (all mutually exclusive).
	struct
	{
		_GFXBuffer*  buffer;
		_GFXImage*   image;
		GFXRenderer* renderer;

	} obj;

} _GFXUnpackRef;


/**
 * Check for equality of unpacked references & getters.
 * Only checks for resource equality, offsets are ignored.
 * Getters will resolve to NULL or 0 if none found.
 */
#define _GFX_UNPACK_REF_IS_EQUAL(refa, refb) \
	(((refa).obj.buffer != NULL && \
		(refa).obj.buffer == (refb).obj.buffer) || \
	((refa).obj.image != NULL && \
		(refa).obj.image == (refb).obj.image) || \
	((refa).obj.renderer != NULL && (refa).value == (refb).value && \
		(refa).obj.renderer == (refb).obj.renderer))

#define _GFX_UNPACK_REF_CONTEXT(ref) \
	((ref).obj.buffer != NULL ? \
		(ref).obj.buffer->heap->allocator.context : \
	(ref).obj.image != NULL ? \
		(ref).obj.image->heap->allocator.context : \
	(ref).obj.renderer != NULL ? \
		(ref).obj.renderer->context : NULL)

#define _GFX_UNPACK_REF_ALLOC(ref) \
	((ref).obj.buffer != NULL ? \
		&(ref).obj.buffer->heap->allocator : \
	(ref).obj.image != NULL ? \
		&(ref).obj.image->heap->allocator : \
	(ref).obj.renderer != NULL ? \
		&(ref).obj.renderer->backing.allocator : NULL)

#define _GFX_UNPACK_REF_HEAP(ref) \
	((ref).obj.buffer != NULL ? (ref).obj.buffer->heap : \
	(ref).obj.image != NULL ? (ref).obj.image->heap : NULL)

#define _GFX_UNPACK_REF_ATTACH(ref) \
	((ref).obj.renderer == NULL ? NULL : \
		&((_GFXAttach*)gfx_vec_at(&(ref).obj.renderer->backing.attachs, \
			(ref).value))->image)

/**
 * Retrieves the memory flags associated with an unpacked reference.
 * Meant for the debug build, where we validate flags and usages.
 */
#if defined (NDEBUG)
	#define _GFX_UNPACK_REF_FLAGS(ref) \
		_Static_assert(0, "Use _GFX_UNPACK_REF_FLAGS in debug mode only.")
#else
	#define _GFX_UNPACK_REF_FLAGS(ref) \
		((ref).obj.buffer != NULL ? \
			(ref).obj.buffer->base.flags : \
		(ref).obj.image != NULL ? \
			(ref).obj.image->base.flags : \
		(ref).obj.renderer != NULL ? \
			_GFX_UNPACK_REF_ATTACH(ref)->base.flags : 0)
#endif


/**
 * Calculates the remaining size of a buffer reference from its offset.
 * The size is dictated by the top-most object being referenced, not by the
 * underlying resource (e.g. the size claimed for a group buffer).
 * @return Zero if ref is not a buffer reference.
 */
uint64_t _gfx_ref_size(GFXBufferRef ref);

/**
 * Resolves & validates a memory reference, meaning:
 * if it references a reference, it will recursively return that reference.
 * @return A user-land reference to the object actually holding the memory.
 *
 * Assumes no self-references exist!
 * Returns GFX_REF_NULL and warns when the reference is invalid.
 */
GFXReference _gfx_ref_resolve(GFXReference ref);

/**
 * Resolves & unpacks a memory resource reference, meaning:
 * if an object is composed of other memory objects internally, it will be
 * 'unpacked' into its elementary non-composed memory objects.
 *
 * Returns empty (all NULL's) and warns when the reference is invalid.
 * If in debug mode & out of bounds, it silently warns.
 */
_GFXUnpackRef _gfx_ref_unpack(GFXReference ref);

/**
 * Creates a staging buffer for a memory resource references.
 * @param ref  Unpacked reference to stage, must be valid and non-empty.
 * @param size Must be > 0.
 * @return NULL on failure.
 *
 * Thread-safe with respect to the associated heap!
 * Will fail if the resource was not allocated from a heap.
 */
_GFXStaging* _gfx_create_staging(const _GFXUnpackRef* ref,
                                 VkBufferUsageFlags usage, uint64_t size);

/**
 * Destroys a staging buffer, freeing all related resources.
 * @param staging Cannot be NULL.
 * @param ref     Must reference the same resource staging was created for.
 *
 * Thread-safe with respect to the associated heap!
 */
void _gfx_destroy_staging(_GFXStaging* staging,
                          const _GFXUnpackRef* ref);


/****************************
 * Dependency injection objects & operations.
 ****************************/

/**
 * Dependency injection metadata.
 */
typedef struct _GFXInjection
{
	// Operation input, must be pre-initialized!
	struct
	{
		uint32_t family;
		size_t   numRefs; // May be zero!

		const _GFXUnpackRef* refs;
		const GFXAccessMask* masks;
		const uint64_t*      sizes; // Must contain _gfx_ref_size(..)!

	} inp;


	// Synchronization output.
	struct
	{
		size_t       numWaits;
		VkSemaphore* waits;

		size_t       numSigs;
		VkSemaphore* sigs;

		// TODO: Add invalidated reference's actual image/buffer handles,
		// e.g. for resized attachments (so we can save/use/destroy its history).

	} out;

} _GFXInjection;


/**
 * Synchronization (metadata) object.
 */
typedef struct _GFXSync
{
	_GFXUnpackRef ref;
	GFXRange      range; // Unpacked, i.e. normalized offset & non-zero size.
	unsigned long tag;   // So we can recycle, 0 = yet untagged.

	// Claimed by (injections can be async), may be NULL.
	const _GFXInjection* inj;


	// Stage in the object's lifecycle.
	enum
	{
		_GFX_SYNC_UNUSED, // Everything but `vk.signaled` is undefined.
		_GFX_SYNC_PREPARE,
		_GFX_SYNC_PENDING,
		_GFX_SYNC_CATCH,
		_GFX_SYNC_USED

	} stage;


	// Vulkan fields.
	struct
	{
		VkSemaphore signaled; // May be VK_NULL_HANDLE.
		int         discard;  // Non-zero to discard the contents.

		// Barrier metadata.
		VkAccessFlags srcAccess;
		VkAccessFlags dstAccess;
		VkImageLayout oldLayout;
		VkImageLayout newLayout;
		uint32_t      srcFamily;
		uint32_t      dstFamily;

		VkPipelineStageFlags srcStage;
		VkPipelineStageFlags dstStage;

		// Unpacked for locality.
		VkBuffer buffer;
		VkImage  image;

	} vk;

} _GFXSync;


/**
 * Internal dependency object.
 */
struct GFXDependency
{
	_GFXContext* context;
	GFXVec       syncs; // Stores _GFXSync.
	_GFXMutex    lock;

	// Vulkan family indices.
	uint32_t graphics;
	uint32_t compute;
	uint32_t transfer;
};


/**
 * TODO: Somehow generate or pass a tag for recycling.
 * Starts a new dependency injection by catching pending signal commands.
 * The object pointed to by injection cannot be moved or copied!
 * @param context   Cannot be NULL.
 * @param cmd       To record barriers to, cannot be VK_NULL_HANDLE.
 * @param numInjs   Number of given injection commands.
 * @param injs      Given injection commands.
 * @param injection Input & output injection metadata, cannot be NULL.
 * @param Zero on failure, must call _gfx_deps_abort.
 *
 * Thread-safe with respect to all dependency objects!
 * Either `_gfx_deps_abort` or `_gfx_deps_finish` must be called with the same
 * injection object (and other inputs) to appropriately cleanup and free all
 * metadata. Note: this call itself can only be called once!
 */
int _gfx_deps_catch(_GFXContext* context, VkCommandBuffer cmd,
                    size_t numInjs, const GFXInject* injs,
                    _GFXInjection* injection);

/**
 * Injects dependencies by preparing new signal commands.
 * @see _gfx_deps_catch.
 *
 * Thread-safe with respect to all dependency objects!
 * Must have succesfully returned from _gfx_deps_catch with injection as
 * argument before calling, as must all other inputs be the same.
 */
int _gfx_deps_prepare(VkCommandBuffer cmd,
                      size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection);

/**
 * Aborts a dependency injection, freeing all data.
 * @see _gfx_deps_catch.
 *
 * Thread-safe with respect to all dependency objects!
 * The content of injection is invalidated after this call.
 */
void _gfx_deps_abort(size_t numInjs, const GFXInject* injs,
                     _GFXInjection* injection);

/**
 * Finalizes a dependency injection, all signal commands are made visible for
 * future wait commands and all wait commands are finalized and cleaned up.
 * @see _gfx_deps_catch.
 *
 * Thread-safe with respect to all dependency objects!
 * The content of injection is invalidated after this call.
 */
void _gfx_deps_finish(size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection);


/****************************
 * Virtual 'render' frame.
 ****************************/

/**
 * Initializes a virtual frame of a renderer.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @return Zero on failure.
 */
int _gfx_frame_init(GFXRenderer* renderer, GFXFrame* frame);

/**
 * Clears a virtual frame of a renderer.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 *
 * This will block until the frame is done rendering!
 */
void _gfx_frame_clear(GFXRenderer* renderer, GFXFrame* frame);

/**
 * Acquires all relevant resources for a virtual frame to be recorded.
 * Cannot be called again until a call to _gfx_frame_submit has been made.
 * This will block until pending submissions of the frame are done rendering,
 * where possible it will reuse its resources afterwards.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL, may not be in renderer->frames!
 * @return Zero if the frame (or renderer) could not be built.
 *
 * This may call _gfx_sync_frames internally on-swapchain recreate.
 * Failure is considered fatal, swapchains could be left in an incomplete state.
 */
int _gfx_frame_acquire(GFXRenderer* renderer, GFXFrame* frame);

/**
 * Records & submits a virtual frame.
 * Must be called exactly once for each call to _gfx_frame_acquire.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @return Zero if the frame could not be submitted.
 *
 * Failure is considered fatal, swapchains could be left in an incomplete state.
 */
int _gfx_frame_submit(GFXRenderer* renderer, GFXFrame* frame);

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
 * This will call _gfx_sync_frames internally when window attachments exist!
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
 * This will call _gfx_sync_frames internally when the graph got invalidated!
 */
int _gfx_render_graph_build(GFXRenderer* renderer);

/**
 * (Re)builds render graph resources dependent on the given attachment index.
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
 * It will in turn call the relevant _gfx_pass_destruct calls.
 */
void _gfx_render_graph_destruct(GFXRenderer* renderer, size_t index);

/**
 * Invalidates the render graph, forcing it to destruct and rebuild everything
 * the next time _gfx_render_graph_build is called.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_graph_invalidate(GFXRenderer* renderer);


/****************************
 * Pass (nodes in the render graph).
 ****************************/

/**
 * Creates a pass, referencing all parents.
 * Each element in parents must be associated with the same renderer.
 * @param renderer   Cannot be NULL.
 * @param numParents Number of parents, 0 for none.
 * @param parents    Parent passes, cannot be NULL if numParents > 0.
 * @return NULL on failure.
 */
GFXPass* _gfx_create_pass(GFXRenderer* renderer,
                          size_t numParents, GFXPass** parents);

/**
 * Destroys a pass, unreferencing all parents.
 * Undefined behaviour if destroying a pass that is referenced by another.
 * @param pass Cannot be NULL.
 */
void _gfx_destroy_pass(GFXPass* pass);

/**
 * TODO: Currently it builds a lot, this will be offloaded to different objects.
 * TODO: Merge passes with the same resolution into subpasses.
 * (Re)builds the Vulkan object structure.
 * @param pass  Cannot be NULL.
 * @param flags What resources should be recreated (0 to recreate nothing).
 * @return Non-zero if valid and built.
 */
int _gfx_pass_build(GFXPass* pass, _GFXRecreateFlags flags);

/**
 * Destructs the Vulkan object structure, non-recursively.
 * @param pass Cannot be NULL.
 *
 * Must be called before detaching any attachment it uses!
 */
void _gfx_pass_destruct(GFXPass* pass);

/**
 * Records the pass into the command buffers of a frame.
 * The frame's command buffers must be in the recording state (!).
 * @param pass  Cannot be NULL.
 * @param frame Cannot be NULL, must be of the same renderer as pass.
 *
 * No-op if the pass is not built.
 */
void _gfx_pass_record(GFXPass* pass, GFXFrame* frame);


#endif
