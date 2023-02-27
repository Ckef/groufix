/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>


/****************************
 * Recording command buffer element definition.
 */
typedef struct _GFXCmdElem
{
	unsigned int    order; // Pass order.
	VkCommandBuffer cmd;

} _GFXCmdElem;


/****************************
 * Claims (or creates) a command buffer from the current recording pool.
 * To unclaim, the current pool's used count should be decreased.
 * @param recorder Cannot be NULL.
 * @return The command buffer, NULL on failure.
 */
static VkCommandBuffer _gfx_recorder_claim(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	_GFXContext* context = recorder->context;
	_GFXRecorderPool* pool = &recorder->pools[recorder->current];

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
	if (recorder->bind.pipeline != elem)
	{
		recorder->bind.pipeline = elem;
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
	if (recorder->bind.pipeline != elem)
	{
		recorder->bind.pipeline = elem;
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
	if (recorder->bind.primitive != prim)
	{
		recorder->bind.primitive = prim;
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
				primitive->indexSize == sizeof(uint16_t) ?
					VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
		}
	}
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

	// Set new current recording pool.
	recorder->current = recorder->renderer->current;
	_GFXRecorderPool* pool = &recorder->pools[recorder->current];

	// If the pool did not use some command buffers, free them.
	if (pool->used < pool->vk.cmds.size)
	{
		uint32_t unused =
			(uint32_t)(pool->vk.cmds.size - pool->used);

		context->vk.FreeCommandBuffers(context->vk.device,
			pool->vk.pool, unused, gfx_vec_at(&pool->vk.cmds, pool->used));

		gfx_vec_pop(&pool->vk.cmds, (size_t)unused);
	}

	// Try to reset the command pool.
	_GFX_VK_CHECK(
		context->vk.ResetCommandPool(
			context->vk.device, pool->vk.pool, 0),
		{
			gfx_log_warn("Resetting of recorder failed.");
			return 0;
		});

	// No command buffers are in use anymore.
	pool->used = 0;

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
		sizeof(_GFXRecorderPool) * renderer->numFrames);

	if (rec == NULL)
		goto error;

	// TODO:COM: Add more pools for async compute?
	// Create one command pool for each frame.
	VkCommandPoolCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = renderer->graphics.family
	};

	for (unsigned int i = 0; i < renderer->numFrames; ++i)
	{
		_GFX_VK_CHECK(
			context->vk.CreateCommandPool(
				context->vk.device, &cpci, NULL, &rec->pools[i].vk.pool),
			{
				// Destroy all pools on failure.
				while (i > 0) context->vk.DestroyCommandPool(
					context->vk.device, rec->pools[--i].vk.pool, NULL);

				free(rec);
				goto error;
			});
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
		rec->pools[i].used = 0;
		gfx_vec_init(&rec->pools[i].vk.cmds, sizeof(VkCommandBuffer));
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
		_gfx_push_stale(renderer,
			VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
			recorder->pools[i].vk.pool);

	_gfx_mutex_unlock(&renderer->lock);

	// Free all the memory.
	for (unsigned int i = 0; i < renderer->numFrames; ++i)
		gfx_vec_clear(&recorder->pools[i].vk.cmds);

	gfx_vec_clear(&recorder->out.cmds);
	free(recorder);
}

/****************************/
GFX_API void gfx_recorder_render(GFXRecorder* recorder, GFXPass* pass,
                                 void (*cb)(GFXRecorder*, unsigned int, void*),
                                 void* ptr)
{
	assert(recorder != NULL);
	assert(recorder->renderer->recording);
	assert(pass != NULL);
	assert(pass->renderer == recorder->renderer);
	assert(cb != NULL);

	GFXRenderer* rend = recorder->renderer;
	_GFXContext* context = recorder->context;

	// The pass must be a render pass.
	_GFXRenderPass* rPass = (_GFXRenderPass*)pass;
	if (pass->type != GFX_PASS_RENDER) goto error;

	// Check for the presence of a framebuffer.
	VkFramebuffer framebuffer = _gfx_pass_framebuffer(rPass, rend->public);
	if (framebuffer == VK_NULL_HANDLE) goto error;

	// Then, claim a command buffer to use.
	VkCommandBuffer cmd = _gfx_recorder_claim(recorder);
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
	// TODO: Define public GFXRenderArea with a GFXSizeClass?
	VkViewport viewport = {
		.x        = 0.0f,
		.y        = 0.0f,
		.width    = (float)rPass->build.fWidth,
		.height   = (float)rPass->build.fHeight,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = {
			rPass->build.fWidth,
			rPass->build.fHeight
		}
	};

	context->vk.CmdSetViewport(cmd, 0, 1, &viewport);
	context->vk.CmdSetScissor(cmd, 0, 1, &scissor);

	// Set recording input, record, unset input.
	recorder->inp.pass = &rPass->base;
	recorder->inp.cmd = cmd;
	recorder->bind.pipeline = NULL;
	recorder->bind.primitive = NULL;

	cb(recorder, recorder->current, ptr);

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
GFX_API void gfx_recorder_compute(GFXRecorder* recorder, GFXComputeFlags flags,
                                  GFXPass* pass,
                                  void (*cb)(GFXRecorder*, unsigned int, void*),
                                  void* ptr)
{
	assert(recorder != NULL);
	assert(recorder->renderer->recording);
	assert(flags & GFX_COMPUTE_ASYNC || pass != NULL);
	assert(pass == NULL || pass->renderer == recorder->renderer);
	assert(cb != NULL);

	// TODO:COM: flags is completely ignored, implement async compute :)

	_GFXContext* context = recorder->context;

	// The pass must be a compute pass.
	_GFXComputePass* cPass = (_GFXComputePass*)pass;
	if (pass != NULL && pass->type != GFX_PASS_COMPUTE) goto error;

	// Then, claim a command buffer to use.
	VkCommandBuffer cmd = _gfx_recorder_claim(recorder);
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
	recorder->bind.pipeline = NULL;
	recorder->bind.primitive = NULL;

	cb(recorder, recorder->current, ptr);

	recorder->inp.pass = NULL;
	recorder->inp.cmd = NULL;

	_GFX_VK_CHECK(
		context->vk.EndCommandBuffer(cmd),
		goto error);

	// Now insert the command buffer in its correct position.
	// Which is in submission order of the passes.
	// Or entirely at the end if async (i.e. no pass given).
	const unsigned int order =
		cPass != NULL ? cPass->base.order : UINT_MAX;

	if (!_gfx_recorder_output(recorder, order, cmd))
		goto error;

	return;


	// Error on failure.
error:
	gfx_log_error("Recorder failed to record compute commands.");
}

/****************************/
GFX_API void gfx_recorder_get_size(GFXRecorder* recorder,
                                   uint32_t* width, uint32_t* height, uint32_t* layers)
{
	assert(recorder != NULL);
	assert(recorder->inp.pass != NULL);
	assert(recorder->inp.cmd != NULL);
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

	if (pass->type == GFX_PASS_RENDER)
		*width = ((_GFXRenderPass*)pass)->build.fWidth,
		*height = ((_GFXRenderPass*)pass)->build.fHeight,
		*layers = ((_GFXRenderPass*)pass)->build.fLayers;
	else
		*width = 0,
		*height = 0,
		*layers = 0;
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
		uint32_t offs[numOffsets > 0 ? numOffsets : 1];

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
	assert(size > 0);
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
GFX_API void gfx_cmd_draw_from(GFXRecorder* recorder, GFXRenderable* renderable,
                               uint32_t count,
                               uint32_t stride, GFXBufferRef ref)
{
	static_assert(
		sizeof(GFXDrawCmd) % 4 == 0,
		"sizeof(GFXDrawCmd) must be a multiple of 4 bytes.");

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
	static_assert(
		sizeof(GFXDrawIndexedCmd) % 4 == 0,
		"sizeof(GFXDrawIndexedCmd) must be a multiple of 4 bytes.");

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
                              uint32_t xSize, uint32_t ySize, uint32_t zSize)
{
	assert(recorder != NULL);
	assert(recorder->inp.pass == NULL || recorder->inp.pass->type == GFX_PASS_COMPUTE);
	assert(recorder->inp.cmd != NULL);
	assert(computable != NULL);
	assert(computable->technique != NULL);
	assert(computable->technique->renderer == recorder->renderer);
	assert(xSize > 0);
	assert(ySize > 0);
	assert(zSize > 0);

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
	context->vk.CmdDispatch(recorder->inp.cmd, xSize, ySize, zSize);
}

/****************************/
GFX_API void gfx_cmd_dispatch_base(GFXRecorder* recorder, GFXComputable * computable,
                                   uint32_t xBase, uint32_t yBase, uint32_t zBase,
                                   uint32_t xSize, uint32_t ySize, uint32_t zSize)
{
	assert(recorder != NULL);
	assert(recorder->inp.pass == NULL || recorder->inp.pass->type == GFX_PASS_COMPUTE);
	assert(recorder->inp.cmd != NULL);
	assert(computable != NULL);
	assert(computable->technique != NULL);
	assert(computable->technique->renderer == recorder->renderer);
	assert(xSize > 0);
	assert(ySize > 0);
	assert(zSize > 0);

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
		xBase, yBase, zBase, xSize, ySize, zSize);
}

/****************************/
GFX_API void gfx_cmd_dispatch_from(GFXRecorder* recorder, GFXComputable* computable,
                                   GFXBufferRef ref)
{
	assert(GFX_REF_IS_BUFFER(ref));
	assert(recorder != NULL);
	assert(recorder->inp.pass == NULL || recorder->inp.pass->type == GFX_PASS_COMPUTE);
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
