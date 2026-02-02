/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>
#include <stdlib.h>


// Indirect command size compatibility.
static_assert(
	sizeof(GFXDrawCmd) % 4 == 0,
	"sizeof(GFXDrawCmd) must be a multiple of 4 bytes.");

static_assert(
	sizeof(GFXDrawIndexedCmd) % 4 == 0,
	"sizeof(GFXDrawIndexedCmd) must be a multiple of 4 bytes.");

static_assert(
	sizeof(GFXDispatchCmd) % 4 == 0,
	"sizeof(GFXDispatchCmd) must be a multiple of 4 bytes.");


// Get Vulkan index type.
#define _GFX_GET_VK_INDEX_TYPE(size) \
	((size) == sizeof(uint8_t) ? VK_INDEX_TYPE_UINT8 : \
	(size) == sizeof(uint16_t) ? VK_INDEX_TYPE_UINT16 : \
	(size) == sizeof(uint32_t) ? VK_INDEX_TYPE_UINT32 : \
	0) /* Should not happen. */


/****************************
 * Recording command buffer element definition.
 */
typedef struct _GFXCmdElem
{
	unsigned int    order; // Pass order.
	VkCommandBuffer cmd;

} _GFXCmdElem;


/****************************
 * Compares two user defined viewport descriptions.
 * @return Non-zero if equal.
 */
static inline bool _gfx_cmp_viewports(const GFXViewport* l,
                                      const GFXViewport* r)
{
	// Cannot use memcmp because of padding.
	const bool abs =
		(l->size == GFX_SIZE_ABSOLUTE) && (r->size == GFX_SIZE_ABSOLUTE) &&
		(l->x == r->x) &&
		(l->y == r->y) &&
		(l->width == r->width) &&
		(l->height == r->height);

	const bool rel =
		(l->size == GFX_SIZE_RELATIVE) && (r->size == GFX_SIZE_RELATIVE) &&
		(l->xOffset == r->xOffset) &&
		(l->yOffset == r->yOffset) &&
		(l->xScale == r->xScale) &&
		(l->yScale == r->yScale);

	return
		(abs || rel) &&
		(l->minDepth == r->minDepth) &&
		(l->maxDepth == r->maxDepth);
}

/****************************
 * Compares two user defined scissor descriptions.
 * @return Non-zero if equal.
 */
static inline bool _gfx_cmp_scissors(const GFXScissor* l,
                                     const GFXScissor* r)
{
	// Cannot use memcmp because of padding.
	const bool abs =
		(l->size == GFX_SIZE_ABSOLUTE) && (r->size == GFX_SIZE_ABSOLUTE) &&
		(l->x == r->x) &&
		(l->y == r->y) &&
		(l->width == r->width) &&
		(l->height == r->height);

	const bool rel =
		(l->size == GFX_SIZE_RELATIVE) && (r->size == GFX_SIZE_RELATIVE) &&
		(l->xOffset == r->xOffset) &&
		(l->yOffset == r->yOffset) &&
		(l->xScale == r->xScale) &&
		(l->yScale == r->yScale);

	return (abs || rel);
}

/****************************
 * Converts a GFXViewport into a VkViewport,
 * taking into account a given framebuffer width/height.
 */
static inline VkViewport _gfx_get_viewport(const GFXViewport* viewport,
                                           uint32_t fWidth, uint32_t fHeight)
{
	VkViewport vkViewport = {
		.minDepth = viewport->minDepth,
		.maxDepth = viewport->maxDepth
	};

	if (viewport->size == GFX_SIZE_ABSOLUTE)
	{
		vkViewport.x = viewport->x;
		vkViewport.y = viewport->y;
		vkViewport.width = viewport->width;
		vkViewport.height = viewport->height;
	}
	else
	{
		vkViewport.x = (float)fWidth * viewport->xOffset;
		vkViewport.y = (float)fHeight * viewport->yOffset;
		vkViewport.width = (float)fWidth * viewport->xScale;
		vkViewport.height = (float)fHeight * viewport->yScale;
	}

	return vkViewport;
}

/****************************
 * Converts a GFXScissor into a VkRect2D,
 * taking into account a given framebuffer width/height.
 */
static inline VkRect2D _gfx_get_scissor(const GFXScissor* scissor,
                                        uint32_t fWidth, uint32_t fHeight)
{
	VkRect2D vkScissor;

	if (scissor->size == GFX_SIZE_ABSOLUTE)
	{
		vkScissor.offset.x = scissor->x;
		vkScissor.offset.y = scissor->y;
		vkScissor.extent.width = scissor->width;
		vkScissor.extent.height = scissor->height;
	}
	else
	{
		vkScissor.offset.x = (int32_t)((float)fWidth * scissor->xOffset);
		vkScissor.offset.y = (int32_t)((float)fHeight * scissor->yOffset);
		vkScissor.extent.width = (uint32_t)((float)fWidth * scissor->xScale);
		vkScissor.extent.height = (uint32_t)((float)fHeight * scissor->yScale);
	}

	return vkScissor;
}

/****************************
 * Binds a graphics pipeline to the current recording.
 * @param recorder   Cannot be NULL, assumed to be in a callback.
 * @param renderable Cannot be NULL, assumed to be validated.
 * @return Zero on failure.
 */
static bool _gfx_recorder_bind_renderable(GFXRecorder* recorder,
                                          GFXRenderable* renderable)
{
	assert(recorder != NULL);
	assert(renderable != NULL);

	_GFXContext* context = recorder->context;

	// Get pipeline from renderable.
	_GFXCacheElem* elem;
	if (!_gfx_renderable_pipeline(renderable, &elem, 0))
		return 0;

	// Bind as graphics pipeline.
	if (recorder->state.pipeline != elem)
	{
		recorder->state.pipeline = elem;
		context->vk.CmdBindPipeline(recorder->inp.cmd,
			VK_PIPELINE_BIND_POINT_GRAPHICS, elem->vk.pipeline);
	}

	return 1;
}

/****************************
 * Binds a compute pipeline to the current recording.
 * @param recorder   Cannot be NULL, assumed to be in a callback.
 * @param computable Cannot be NULL, assumed to be validated.
 * @return Zero on failure.
 */
static bool _gfx_recorder_bind_computable(GFXRecorder* recorder,
                                          GFXComputable* computable)
{
	assert(recorder != NULL);
	assert(computable != NULL);

	_GFXContext* context = recorder->context;

	// Get pipeline from computable.
	_GFXCacheElem* elem;
	if (!_gfx_computable_pipeline(computable, &elem, 0))
		return 0;

	// Bind as compute pipeline.
	if (recorder->state.pipeline != elem)
	{
		recorder->state.pipeline = elem;
		context->vk.CmdBindPipeline(recorder->inp.cmd,
			VK_PIPELINE_BIND_POINT_COMPUTE, elem->vk.pipeline);
	}

	return 1;
}

/****************************
 * Binds a vertex and/or index buffer to the current recording.
 * @param recorder  Cannot be NULL, assumed to be in a callback.
 * @param primitive Cannot be NULL.
 */
static void _gfx_recorder_bind_primitive(GFXRecorder* recorder,
                                         GFXPrimitive* primitive)
{
	assert(recorder != NULL);
	assert(primitive != NULL);

	_GFXContext* context = recorder->context;
	_GFXPrimitive* prim = (_GFXPrimitive*)primitive;

	// Bind vertex & index buffers.
	if (recorder->state.primitive != prim)
	{
		recorder->state.primitive = prim;
		VkBuffer vertexBuffs[prim->numBindings];
		VkDeviceSize vertexOffsets[prim->numBindings];

		for (size_t i = 0; i < prim->numBindings; ++i)
			vertexBuffs[i] = prim->bindings[i].buffer->vk.buffer,
			vertexOffsets[i] = prim->bindings[i].offset;

		context->vk.CmdBindVertexBuffers(recorder->inp.cmd,
			0, (uint32_t)prim->numBindings,
			vertexBuffs, vertexOffsets);

		if (primitive->numIndices > 0)
		{
			_GFXUnpackRef index =
				_gfx_ref_unpack(gfx_ref_prim_indices(primitive));

			context->vk.CmdBindIndexBuffer(recorder->inp.cmd,
				index.obj.buffer->vk.buffer,
				index.value,
				_GFX_GET_VK_INDEX_TYPE(primitive->indexSize));
		}
	}
}

/****************************
 * Claims (or creates) a command buffer from the current recording pool.
 * To unclaim, the current pool's used count should be decreased.
 * @param recorder Cannot be NULL.
 * @param type     Type of the pass, to inform pool selection.
 * @return The command buffer, NULL on failure.
 */
static VkCommandBuffer _gfx_recorder_claim(GFXRecorder* recorder,
                                           GFXPassType type)
{
	assert(recorder != NULL);

	_GFXContext* context = recorder->context;

	// Select recorder pool.
	_GFXRecorderPool* pool = &recorder->pools[
		type != GFX_PASS_COMPUTE_ASYNC ?
			recorder->current * 2 :
			recorder->current * 2 + 1];

	// If we still have enough command buffers, return the next one.
	if (pool->used < pool->vk.cmds.size)
		// Immediately increase used counter.
		return *(VkCommandBuffer*)gfx_vec_at(&pool->vk.cmds, pool->used++);

	// Otherwise, allocate a new one.
	if (!gfx_vec_push(&pool->vk.cmds, 1, NULL))
		return NULL;

	VkCommandBufferAllocateInfo cbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

		.pNext              = NULL,
		.commandPool        = pool->vk.pool,
		.level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer* cmd = gfx_vec_at(&pool->vk.cmds, pool->used);
	_GFX_VK_CHECK(
		context->vk.AllocateCommandBuffers(
			context->vk.device, &cbai, cmd),
		{
			gfx_vec_pop(&pool->vk.cmds, 1);
			return NULL;
		});

	// Increase used counter & return.
	++pool->used;
	return *cmd;
}

/****************************
 * Outputs a command buffer of a specific submission order.
 * @param recorder Cannot be NULL.
 * @return Zero on failure.
 */
static bool _gfx_recorder_output(GFXRecorder* recorder,
                                 unsigned int order, VkCommandBuffer cmd)
{
	// Find the right spot to insert at.
	// We assume the most prevelant way of recording stuff is in submission
	// order. Which would make backwards linear search perfect.
	size_t loc;
	for (loc = recorder->out.cmds.size; loc > 0; --loc)
	{
		unsigned int cOrder =
			((_GFXCmdElem*)gfx_vec_at(&recorder->out.cmds, loc-1))->order;

		if (cOrder <= order)
			break;
	}

	// Insert at found position.
	_GFXCmdElem elem = {
		.order = order,
		.cmd = cmd
	};

	return gfx_vec_insert(&recorder->out.cmds, 1, &elem, loc);
}

/****************************/
bool _gfx_recorder_reset(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	_GFXContext* context = recorder->context;

	// Clear output.
	gfx_vec_release(&recorder->out.cmds);

	// Set new current recording pools.
	recorder->current = recorder->renderer->current;

	// Then reset both graphics & compute.
	_GFXRecorderPool* pools = &recorder->pools[recorder->current * 2];

	for (unsigned int p = 0; p < 2; ++p)
	{
		// If the pool did not use some command buffers, free them.
		if (pools[p].used < pools[p].vk.cmds.size)
		{
			uint32_t unused =
				(uint32_t)(pools[p].vk.cmds.size - pools[p].used);

			context->vk.FreeCommandBuffers(context->vk.device,
				pools[p].vk.pool, unused,
				gfx_vec_at(&pools[p].vk.cmds, pools[p].used));

			gfx_vec_pop(&pools[p].vk.cmds, (size_t)unused);
		}

		// Try to reset the command pool.
		_GFX_VK_CHECK(context->vk.ResetCommandPool(
			context->vk.device, pools[p].vk.pool, 0), return 0);

		// No command buffers are in use anymore.
		pools[p].used = 0;
	}

	return 1;
}

/****************************/
void _gfx_recorder_record(GFXRecorder* recorder,
                          unsigned int order, VkCommandBuffer cmd)
{
	assert(recorder != NULL);
	assert(cmd != NULL);

	_GFXContext* context = recorder->context;

	// Do a binary search to find the left-most command buffer of this order.
	size_t l = 0;
	size_t r = recorder->out.cmds.size;

	while (l < r)
	{
		const size_t p = (l + r) >> 1;
		const _GFXCmdElem* e = gfx_vec_at(&recorder->out.cmds, p);

		if (e->order < order) l = p + 1;
		else r = p;
	}

	// Then find the right-most command buffer of this order.
	while (r < recorder->out.cmds.size)
	{
		const _GFXCmdElem* e = gfx_vec_at(&recorder->out.cmds, r);
		if (e->order > order) break;
		else ++r;
	}

	// Finally record them all into the given command buffer.
	if (r > l)
	{
		VkCommandBuffer buffs[r-l];
		for (size_t i = l; i < r; ++i) buffs[i-l] =
			((_GFXCmdElem*)gfx_vec_at(&recorder->out.cmds, i))->cmd;

		context->vk.CmdExecuteCommands(cmd, (uint32_t)(r-l), buffs);
	}
}

/****************************/
GFX_API GFXRecorder* gfx_renderer_add_recorder(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(!renderer->recording);

	_GFXContext* context = renderer->cache.context;

	// Allocate a new recorder.
	GFXRecorder* rec = malloc(
		sizeof(GFXRecorder) +
		sizeof(_GFXRecorderPool) * renderer->numFrames * 2);

	if (rec == NULL)
		goto error;

	// Create two command pools for each frame.
	// One for the graphics family and one for the compute family.
	VkCommandPoolCreateInfo gcpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = renderer->graphics.family
	};

	VkCommandPoolCreateInfo ccpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = renderer->compute.family
	};

	for (unsigned int i = 0; i < renderer->numFrames; ++i)
	{
		// Graphics pool.
		_GFX_VK_CHECK(
			context->vk.CreateCommandPool(
				context->vk.device, &gcpci, NULL, &rec->pools[i*2].vk.pool),
			{
				goto destroy_prev_pools;
			});

		// Compute pool.
		_GFX_VK_CHECK(
			context->vk.CreateCommandPool(
				context->vk.device, &ccpci, NULL, &rec->pools[i*2+1].vk.pool),
			{
				// Destroy graphics, then clean the rest.
				context->vk.DestroyCommandPool(
					context->vk.device, rec->pools[i*2].vk.pool, NULL);

				goto destroy_prev_pools;
			});

		continue; // Success!

	destroy_prev_pools:
		// If it failed, destroy all previous pools.
		for (; i > 0; --i)
		{
			context->vk.DestroyCommandPool(
				context->vk.device, rec->pools[i*2-1].vk.pool, NULL);
			context->vk.DestroyCommandPool(
				context->vk.device, rec->pools[i*2-2].vk.pool, NULL);
		}

		free(rec);
		goto error;
	}

	// Initialize the rest of the pools.
	rec->renderer = renderer;
	rec->context = context;
	rec->current = 0;
	rec->inp.pass = NULL;
	rec->inp.cmd = NULL;
	gfx_vec_init(&rec->out.cmds, sizeof(_GFXCmdElem));

	for (unsigned int i = 0; i < renderer->numFrames; ++i)
	{
		rec->pools[i*2].used = 0;
		rec->pools[i*2+1].used = 0;
		gfx_vec_init(&rec->pools[i*2].vk.cmds, sizeof(VkCommandBuffer));
		gfx_vec_init(&rec->pools[i*2+1].vk.cmds, sizeof(VkCommandBuffer));
	}

	// Ok so we cheat a little by checking if the renderer has a public frame.
	// If it does, we take its index to set the current pool.
	// Note that this is not thread-safe with frame operations!
	if (renderer->public != NULL)
		rec->current = renderer->current;

	// Init subordinate & link the recorder into the renderer.
	// Modifying the renderer, lock!
	// Also using this lock for access to the pool!
	_gfx_mutex_lock(&renderer->lock);

	_gfx_pool_sub(&renderer->pool, &rec->sub);
	gfx_list_insert_after(&renderer->recorders, &rec->list, NULL);

	_gfx_mutex_unlock(&renderer->lock);

	return rec;


	// Error on failure.
error:
	gfx_log_error("Could not add a new recorder to a renderer.");
	return NULL;
}

/****************************/
GFX_API void gfx_erase_recorder(GFXRecorder* recorder)
{
	assert(recorder != NULL);
	assert(!recorder->renderer->recording);

	GFXRenderer* renderer = recorder->renderer;

	// Unlink itself from the renderer & undo subordinate.
	// Locking for renderer and access to the pool!
	_gfx_mutex_lock(&renderer->lock);

	gfx_list_erase(&renderer->recorders, &recorder->list);
	_gfx_pool_unsub(&renderer->pool, &recorder->sub);

	// Stay locked; we need to make the command pools stale,
	// as its command buffers might still be in use by pending virtual frames!
	// Still, NOT thread-safe with respect to gfx_renderer_(acquire|submit)!
	for (unsigned int i = 0; i < renderer->numFrames; ++i)
	{
		// Graphics & compute pools.
		_gfx_push_stale(renderer,
			VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
			recorder->pools[i*2].vk.pool);
		_gfx_push_stale(renderer,
			VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
			recorder->pools[i*2+1].vk.pool);
	}

	_gfx_mutex_unlock(&renderer->lock);

	// Free all the memory.
	for (unsigned int i = 0; i < renderer->numFrames; ++i)
	{
		gfx_vec_clear(&recorder->pools[i*2].vk.cmds);
		gfx_vec_clear(&recorder->pools[i*2+1].vk.cmds);
	}

	gfx_vec_clear(&recorder->out.cmds);
	free(recorder);
}

/****************************/
GFX_API GFXRenderer* gfx_recorder_get_renderer(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	return recorder->renderer;
}

/****************************/
GFX_API void gfx_recorder_render(GFXRecorder* recorder, GFXPass* pass,
                                 void (*cb)(GFXRecorder*, void*),
                                 void* ptr)
{
	assert(recorder != NULL);
	assert(recorder->renderer->recording);
	assert(pass != NULL);
	assert(pass->renderer == recorder->renderer);
	assert(cb != NULL);

	GFXRenderer* rend = recorder->renderer;
	_GFXContext* context = recorder->context;

	// Ignore if pass is culled.
	if (pass->culled) return;

	// The pass must be a render pass.
	_GFXRenderPass* rPass = (_GFXRenderPass*)pass;
	if (pass->type != GFX_PASS_RENDER) goto error;

	// Check for the presence of a framebuffer.
	VkFramebuffer framebuffer = _gfx_pass_framebuffer(rPass, rend->public);
	if (framebuffer == VK_NULL_HANDLE) goto error;

	// Then, claim a command buffer to use.
	VkCommandBuffer cmd = _gfx_recorder_claim(recorder, pass->type);
	if (cmd == NULL) goto error;

	// Start recording with it.
	VkCommandBufferBeginInfo cbbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

		.pNext = NULL,
		.flags =
			VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
			VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,

		.pInheritanceInfo = (VkCommandBufferInheritanceInfo[]){{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,

			.pNext       = NULL,
			.renderPass  = rPass->vk.pass,
			.subpass     = rPass->out.subpass,
			.framebuffer = framebuffer,

			.occlusionQueryEnable = VK_FALSE,
			.queryFlags           = 0,
			.pipelineStatistics   = 0
		}}
	};

	_GFX_VK_CHECK(
		context->vk.BeginCommandBuffer(cmd, &cbbi),
		goto error);

	// Set viewport & scissor state.
	VkViewport viewport = _gfx_get_viewport(
		&rPass->state.viewport, rPass->build.fWidth, rPass->build.fHeight);
	VkRect2D scissor = _gfx_get_scissor(
		&rPass->state.scissor, rPass->build.fWidth, rPass->build.fHeight);

	recorder->state.viewport = rPass->state.viewport;
	recorder->state.scissor = rPass->state.scissor;
	recorder->state.lineWidth = 1.0f; // Also set a default line width.

	context->vk.CmdSetViewport(cmd, 0, 1, &viewport);
	context->vk.CmdSetScissor(cmd, 0, 1, &scissor);
	context->vk.CmdSetLineWidth(cmd, recorder->state.lineWidth);

	// Set recording input, record, unset input.
	recorder->inp.pass = pass;
	recorder->inp.cmd = cmd;
	recorder->state.pipeline = NULL;
	recorder->state.primitive = NULL;

	cb(recorder, ptr);

	recorder->inp.pass = NULL;
	recorder->inp.cmd = NULL;

	_GFX_VK_CHECK(
		context->vk.EndCommandBuffer(cmd),
		goto error);

	// Now insert the command buffer in its correct position.
	// Which is in submission order of the passes.
	if (!_gfx_recorder_output(recorder, pass->order, cmd))
		goto error;

	return;


	// Error on failure.
error:
	gfx_log_error("Recorder failed to record render commands.");
}

/****************************/
GFX_API void gfx_recorder_compute(GFXRecorder* recorder, GFXPass* pass,
                                  void (*cb)(GFXRecorder*, void*),
                                  void* ptr)
{
	assert(recorder != NULL);
	assert(recorder->renderer->recording);
	assert(pass != NULL);
	assert(pass->renderer == recorder->renderer);
	assert(cb != NULL);

	_GFXContext* context = recorder->context;

	// Ignore if pass is culled.
	if (pass->culled) return;

	// The pass must be a compute pass.
	if (pass->type == GFX_PASS_RENDER) goto error;

	// Then, claim a command buffer to use.
	VkCommandBuffer cmd = _gfx_recorder_claim(recorder, pass->type);
	if (cmd == NULL) goto error;

	// Start recording with it.
	VkCommandBufferBeginInfo cbbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

		.pNext = NULL,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,

		.pInheritanceInfo = (VkCommandBufferInheritanceInfo[]){{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,

			.pNext       = NULL,
			.renderPass  = VK_NULL_HANDLE,
			.subpass     = 0,
			.framebuffer = VK_NULL_HANDLE,

			.occlusionQueryEnable = VK_FALSE,
			.queryFlags           = 0,
			.pipelineStatistics   = 0
		}}
	};

	_GFX_VK_CHECK(
		context->vk.BeginCommandBuffer(cmd, &cbbi),
		goto error);

	// Set recording input, record, unset input.
	recorder->inp.pass = pass;
	recorder->inp.cmd = cmd;
	recorder->state.pipeline = NULL;
	recorder->state.primitive = NULL;

	cb(recorder, ptr);

	recorder->inp.pass = NULL;
	recorder->inp.cmd = NULL;

	_GFX_VK_CHECK(
		context->vk.EndCommandBuffer(cmd),
		goto error);

	// Now insert the command buffer in its correct position.
	// Which is in submission order of the passes.
	if (!_gfx_recorder_output(recorder, pass->order, cmd))
		goto error;

	return;


	// Error on failure.
error:
	gfx_log_error("Recorder failed to record compute commands.");
}

/****************************/
GFX_API unsigned int gfx_recorder_get_frame_index(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	return recorder->current;
}

/****************************/
GFX_API GFXPass* gfx_recorder_get_pass(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	return recorder->inp.pass;
}

/****************************/
GFX_API void gfx_recorder_get_size(GFXRecorder* recorder,
                                   uint32_t* width, uint32_t* height, uint32_t* layers)
{
	assert(recorder != NULL);
	assert(width != NULL);
	assert(height != NULL);
	assert(layers != NULL);

	if (recorder->inp.pass && recorder->inp.pass->type == GFX_PASS_RENDER)
		*width = ((_GFXRenderPass*)recorder->inp.pass)->build.fWidth,
		*height = ((_GFXRenderPass*)recorder->inp.pass)->build.fHeight,
		*layers = ((_GFXRenderPass*)recorder->inp.pass)->build.fLayers;
	else
		// Output 0,0,0 if no associated pass.
		*width = 0,
		*height = 0,
		*layers = 0;
}

/****************************/
GFX_API void gfx_pass_get_size(GFXPass* pass,
                               uint32_t* width, uint32_t* height, uint32_t* layers)
{
	assert(pass != NULL);
	assert(width != NULL);
	assert(height != NULL);
	assert(layers != NULL);

	if (!pass->culled && pass->type == GFX_PASS_RENDER)
		*width = ((_GFXRenderPass*)pass)->build.fWidth,
		*height = ((_GFXRenderPass*)pass)->build.fHeight,
		*layers = ((_GFXRenderPass*)pass)->build.fLayers;
	else
		*width = 0,
		*height = 0,
		*layers = 0;
}

/****************************/
GFX_API GFXViewport gfx_recorder_get_viewport(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	if (recorder->inp.pass && recorder->inp.pass->type == GFX_PASS_RENDER)
		return recorder->state.viewport;
	else
		return (GFXViewport){
			.size = GFX_SIZE_ABSOLUTE,
			.x = 0.0f,
			.y = 0.0f,
			.width = 0.0f,
			.height = 0.0f,
			.minDepth = 0.0f,
			.maxDepth = 0.0f
		};
}

/****************************/
GFX_API GFXScissor gfx_recorder_get_scissor(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	if (recorder->inp.pass && recorder->inp.pass->type == GFX_PASS_RENDER)
		return recorder->state.scissor;
	else
		return (GFXScissor){
			.size = GFX_SIZE_ABSOLUTE,
			.x = 0,
			.y = 0,
			.width = 0,
			.height = 0
		};
}

/****************************/
GFX_API float gfx_recorder_get_line_width(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	if (recorder->inp.pass && recorder->inp.pass->type == GFX_PASS_RENDER)
		return recorder->state.lineWidth;
	else
		return 0.0f;
}

/****************************/
GFX_API void gfx_cmd_bind(GFXRecorder* recorder, GFXTechnique* technique,
                          size_t firstSet,
                          size_t numSets, size_t numDynamics,
                          GFXSet** sets,
                          const uint32_t* offsets)
{
	assert(recorder != NULL);
	assert(recorder->inp.cmd != NULL);
	assert(technique != NULL);
	assert(technique->renderer == recorder->renderer);
	assert(firstSet < technique->numSets);
	assert(numSets > 0);
	assert(numSets <= technique->numSets - firstSet);
	assert(sets != NULL);
	assert(numDynamics == 0 || offsets != NULL);

	_GFXContext* context = recorder->context;

	// Check technique.
	if (technique->layout == NULL)
	{
		gfx_log_error(
			"Technique not locked during bind command; "
			"command not recorded.");

		return;
	}

	// Get all the Vulkan descriptor sets.
	// And count the number of dynamic offsets.
	VkDescriptorSet dSets[numSets];
	size_t numOffsets = 0;

	for (size_t s = 0; s < numSets; ++s)
	{
		_GFXPoolElem* elem = _gfx_set_get(sets[s], &recorder->sub);
		if (elem == NULL)
		{
			gfx_log_error(
				"Failed to get Vulkan descriptor set during bind command; "
				"command not recorded.");

			return;
		}

		dSets[s] = elem->vk.set;
		numOffsets += sets[s]->numDynamics;
	}

	// Record the bind command.
	const VkPipelineBindPoint bindPoint =
		technique->shaders[_GFX_GET_SHADER_STAGE_INDEX(GFX_STAGE_COMPUTE)] == NULL ?
		VK_PIPELINE_BIND_POINT_GRAPHICS :
		VK_PIPELINE_BIND_POINT_COMPUTE;

	if (numDynamics >= numOffsets)
	{
		// If enough dynamic offsets are given, just pass that array.
		context->vk.CmdBindDescriptorSets(recorder->inp.cmd,
			bindPoint, technique->vk.layout,
			(uint32_t)firstSet, (uint32_t)numSets, dSets,
			(uint32_t)numOffsets, offsets);
	}
	else
	{
		// If not, create a new array,
		// set all trailing 'empty' offsets to 0.
		uint32_t offs[GFX_MAX(1, numOffsets)];

		for (size_t d = 0; d < numOffsets; ++d)
			offs[d] = d < numDynamics ? offsets[d] : 0;

		context->vk.CmdBindDescriptorSets(recorder->inp.cmd,
			bindPoint, technique->vk.layout,
			(uint32_t)firstSet, (uint32_t)numSets, dSets,
			(uint32_t)numOffsets, offs);
	}
}

/****************************/
GFX_API void gfx_cmd_push(GFXRecorder* recorder, GFXTechnique* technique,
                          uint32_t offset,
                          uint32_t size, const void* data)
{
	assert(recorder != NULL);
	assert(recorder->inp.cmd != NULL);
	assert(technique != NULL);
	assert(technique->renderer == recorder->renderer);
	assert(offset % 4 == 0);
	assert(offset < technique->pushSize);
	assert(size % 4 == 0);
	assert(size <= technique->pushSize - offset);
	assert(data != NULL);

	_GFXContext* context = recorder->context;

	// Check technique.
	if (technique->layout == NULL)
	{
		gfx_log_error(
			"Technique not locked during push command; "
			"command not recorded.");

		return;
	}

	// Take all remaining bytes if asked.
	if (size == 0)
		size = technique->pushSize - offset;

	// Record the push command.
	context->vk.CmdPushConstants(recorder->inp.cmd,
		technique->vk.layout,
		_GFX_GET_VK_SHADER_STAGE(technique->pushStages),
		offset, size, data);
}

/****************************/
GFX_API void gfx_cmd_draw(GFXRecorder* recorder, GFXRenderable* renderable,
                          uint32_t vertices, uint32_t instances,
                          uint32_t firstVertex, uint32_t firstInstance)
{
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.pass->type == GFX_PASS_RENDER);
	assert(recorder->inp.cmd != NULL);
	assert(renderable != NULL);
	assert(renderable->pass == recorder->inp.pass);
	assert(renderable->technique != NULL);
	assert(vertices > 0 || renderable->primitive != NULL);
	assert(instances > 0);
	assert(renderable->primitive == NULL ||
		(firstVertex < renderable->primitive->numVertices &&
		vertices <= renderable->primitive->numVertices - firstVertex));

	_GFXContext* context = recorder->context;

	// Take entire primitive if asked.
	if (vertices == 0)
		vertices = renderable->primitive->numVertices - firstVertex;

	// Bind pipeline.
	if (!_gfx_recorder_bind_renderable(recorder, renderable))
	{
		gfx_log_error(
			"Failed to get Vulkan graphics pipeline during draw command; "
			"command not recorded.");

		return;
	}

	// Bind primitive.
	if (renderable->primitive != NULL)
		_gfx_recorder_bind_primitive(recorder, renderable->primitive);

	// Record the draw command.
	context->vk.CmdDraw(recorder->inp.cmd,
		vertices, instances, firstVertex, firstInstance);
}

/****************************/
GFX_API void gfx_cmd_draw_indexed(GFXRecorder* recorder, GFXRenderable* renderable,
                                  uint32_t indices, uint32_t instances,
                                  uint32_t firstIndex, int32_t vertexOffset,
                                  uint32_t firstInstance)
{
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.pass->type == GFX_PASS_RENDER);
	assert(recorder->inp.cmd != NULL);
	assert(renderable != NULL);
	assert(renderable->pass == recorder->inp.pass);
	assert(renderable->technique != NULL);
	assert(indices > 0 || renderable->primitive != NULL);
	assert(instances > 0);
	assert(renderable->primitive == NULL ||
		(firstIndex < renderable->primitive->numIndices &&
		indices <= renderable->primitive->numIndices - firstIndex));

	_GFXContext* context = recorder->context;

	// Take entire primitive if asked.
	if (indices == 0)
		indices = renderable->primitive->numIndices - firstIndex;

	// Bind pipeline.
	if (!_gfx_recorder_bind_renderable(recorder, renderable))
	{
		gfx_log_error(
			"Failed to get Vulkan graphics pipeline during draw command; "
			"command not recorded.");

		return;
	}

	// Bind primitive.
	if (renderable->primitive != NULL)
		_gfx_recorder_bind_primitive(recorder, renderable->primitive);

	// Record the draw command.
	context->vk.CmdDrawIndexed(recorder->inp.cmd,
		indices, instances, firstIndex, vertexOffset, firstInstance);
}

/****************************/
GFX_API void gfx_cmd_draw_prim(GFXRecorder* recorder, GFXRenderable* renderable,
                               uint32_t instances, uint32_t firstInstance)
{
	// Mostly relies on called function for asserts.

	assert(recorder != NULL);
	assert(renderable != NULL);
	assert(renderable->primitive != NULL);
	assert(instances > 0);

	if (renderable->primitive->numIndices > 0)
		gfx_cmd_draw_indexed(recorder, renderable, 0, instances, 0, 0, firstInstance);
	else
		gfx_cmd_draw(recorder, renderable, 0, instances, 0, firstInstance);
}

/****************************/
GFX_API void gfx_cmd_draw_from(GFXRecorder* recorder, GFXRenderable* renderable,
                               uint32_t count,
                               uint32_t stride, GFXBufferRef ref)
{
	assert(GFX_REF_IS_BUFFER(ref));
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.pass->type == GFX_PASS_RENDER);
	assert(recorder->inp.cmd != NULL);
	assert(renderable != NULL);
	assert(renderable->pass == recorder->inp.pass);
	assert(renderable->technique != NULL);
	assert(count <= 1 || stride == 0 ||
		(stride % 4 == 0 && stride >= sizeof(GFXDrawCmd)));

	_GFXContext* context = recorder->context;

	// Tightly packed if asked.
	if (stride == 0) stride = sizeof(GFXDrawCmd);

	// Unpack reference & validate.
	_GFXUnpackRef unp = _gfx_ref_unpack(ref);
	if (unp.obj.buffer == NULL)
	{
		gfx_log_error(
			"Failed to retrieve indirect buffer during draw command; "
			"command not recorded.");

		return;
	}

	// Bind pipeline.
	if (!_gfx_recorder_bind_renderable(recorder, renderable))
	{
		gfx_log_error(
			"Failed to get Vulkan graphics pipeline during draw command; "
			"command not recorded.");

		return;
	}

	// Bind primitive.
	if (renderable->primitive != NULL)
		_gfx_recorder_bind_primitive(recorder, renderable->primitive);

	// Record the draw command.
	context->vk.CmdDrawIndirect(recorder->inp.cmd,
		unp.obj.buffer->vk.buffer, unp.value, count, stride);
}

/****************************/
GFX_API void gfx_cmd_draw_indexed_from(GFXRecorder* recorder, GFXRenderable* renderable,
                                       uint32_t count,
                                       uint32_t stride, GFXBufferRef ref)
{
	assert(GFX_REF_IS_BUFFER(ref));
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.pass->type == GFX_PASS_RENDER);
	assert(recorder->inp.cmd != NULL);
	assert(renderable != NULL);
	assert(renderable->pass == recorder->inp.pass);
	assert(renderable->technique != NULL);
	assert(count <= 1 || stride == 0 ||
		(stride % 4 == 0 && stride >= sizeof(GFXDrawIndexedCmd)));

	_GFXContext* context = recorder->context;

	// Tightly packed if asked.
	if (stride == 0) stride = sizeof(GFXDrawIndexedCmd);

	// Unpack reference & validate.
	_GFXUnpackRef unp = _gfx_ref_unpack(ref);
	if (unp.obj.buffer == NULL)
	{
		gfx_log_error(
			"Failed to retrieve indirect buffer during draw command; "
			"command not recorded.");

		return;
	}

	// Bind pipeline.
	if (!_gfx_recorder_bind_renderable(recorder, renderable))
	{
		gfx_log_error(
			"Failed to get Vulkan graphics pipeline during draw command; "
			"command not recorded.");

		return;
	}

	// Bind primitive.
	if (renderable->primitive != NULL)
		_gfx_recorder_bind_primitive(recorder, renderable->primitive);

	// Record the draw command.
	context->vk.CmdDrawIndexedIndirect(recorder->inp.cmd,
		unp.obj.buffer->vk.buffer, unp.value, count, stride);
}

/****************************/
GFX_API void gfx_cmd_dispatch(GFXRecorder* recorder, GFXComputable* computable,
                              uint32_t xCount, uint32_t yCount, uint32_t zCount)
{
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.pass->type != GFX_PASS_RENDER);
	assert(recorder->inp.cmd != NULL);
	assert(computable != NULL);
	assert(computable->technique != NULL);
	assert(computable->technique->renderer == recorder->renderer);
	assert(xCount > 0);
	assert(yCount > 0);
	assert(zCount > 0);

	_GFXContext* context = recorder->context;

	// Bind pipeline.
	if (!_gfx_recorder_bind_computable(recorder, computable))
	{
		gfx_log_error(
			"Failed to get Vulkan compute pipeline during dispatch command; "
			"command not recorded.");

		return;
	}

	// Record the dispatch command.
	context->vk.CmdDispatch(recorder->inp.cmd, xCount, yCount, zCount);
}

/****************************/
GFX_API void gfx_cmd_dispatch_base(GFXRecorder* recorder, GFXComputable * computable,
                                   uint32_t xBase, uint32_t yBase, uint32_t zBase,
                                   uint32_t xCount, uint32_t yCount, uint32_t zCount)
{
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.pass->type != GFX_PASS_RENDER);
	assert(recorder->inp.cmd != NULL);
	assert(computable != NULL);
	assert(computable->technique != NULL);
	assert(computable->technique->renderer == recorder->renderer);
	assert(xCount > 0);
	assert(yCount > 0);
	assert(zCount > 0);

	_GFXContext* context = recorder->context;

	// Bind pipeline.
	if (!_gfx_recorder_bind_computable(recorder, computable))
	{
		gfx_log_error(
			"Failed to get Vulkan compute pipeline during dispatch command; "
			"command not recorded.");

		return;
	}

	// Record the dispatch command.
	context->vk.CmdDispatchBase(recorder->inp.cmd,
		xBase, yBase, zBase, xCount, yCount, zCount);
}

/****************************/
GFX_API void gfx_cmd_dispatch_from(GFXRecorder* recorder, GFXComputable* computable,
                                   GFXBufferRef ref)
{
	assert(GFX_REF_IS_BUFFER(ref));
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.pass->type != GFX_PASS_RENDER);
	assert(recorder->inp.cmd != NULL);
	assert(computable != NULL);
	assert(computable->technique != NULL);
	assert(computable->technique->renderer == recorder->renderer);

	_GFXContext* context = recorder->context;

	// Unpack reference & validate.
	_GFXUnpackRef unp = _gfx_ref_unpack(ref);
	if (unp.obj.buffer == NULL)
	{
		gfx_log_error(
			"Failed to retrieve indirect buffer during dispatch command; "
			"command not recorded.");

		return;
	}

	// Bind pipeline.
	if (!_gfx_recorder_bind_computable(recorder, computable))
	{
		gfx_log_error(
			"Failed to get Vulkan compute pipeline during dispatch command; "
			"command not recorded.");

		return;
	}

	// Record the dispatch command.
	context->vk.CmdDispatchIndirect(recorder->inp.cmd,
		unp.obj.buffer->vk.buffer, unp.value);
}

/****************************/
GFX_API void gfx_cmd_set_viewport(GFXRecorder* recorder, GFXViewport viewport)
{
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.pass->type == GFX_PASS_RENDER);
	assert(recorder->inp.cmd != NULL);

	_GFXContext* context = recorder->context;
	_GFXRenderPass* rPass = (_GFXRenderPass*)recorder->inp.pass;

	// Compare & set viewport state.
	if (!_gfx_cmp_viewports(&recorder->state.viewport, &viewport))
	{
		VkViewport vkViewport = _gfx_get_viewport(
			&viewport, rPass->build.fWidth, rPass->build.fHeight);

		context->vk.CmdSetViewport(recorder->inp.cmd, 0, 1, &vkViewport);
		recorder->state.viewport = viewport;
	}
}

/****************************/
GFX_API void gfx_cmd_set_scissor(GFXRecorder* recorder, GFXScissor scissor)
{
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.pass->type == GFX_PASS_RENDER);
	assert(recorder->inp.cmd != NULL);

	_GFXContext* context = recorder->context;
	_GFXRenderPass* rPass = (_GFXRenderPass*)recorder->inp.pass;

	// Compare & set scissor state.
	if (!_gfx_cmp_scissors(&recorder->state.scissor, &scissor))
	{
		VkRect2D vkScissor = _gfx_get_scissor(
			&scissor, rPass->build.fWidth, rPass->build.fHeight);

		context->vk.CmdSetScissor(recorder->inp.cmd, 0, 1, &vkScissor);
		recorder->state.scissor = scissor;
	}
}

/****************************/
GFX_API void gfx_cmd_set_line_width(GFXRecorder* recorder, float lineWidth)
{
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.pass->type == GFX_PASS_RENDER);
	assert(recorder->inp.cmd != NULL);

	_GFXContext* context = recorder->context;

	// Compare & set line width state.
	if (recorder->state.lineWidth != lineWidth)
	{
		context->vk.CmdSetLineWidth(recorder->inp.cmd, lineWidth);
		recorder->state.lineWidth = lineWidth;
	}
}
