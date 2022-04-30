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


/****************************
 * Recording command buffer element definition.
 */
typedef struct _GFXCmdElem
{
	unsigned int    order; // Pass order.
	VkCommandBuffer cmd;

} _GFXCmdElem;


/****************************
 * Spin-locks a renderable for pipeline retrieval.
 */
static inline void _gfx_renderable_lock(GFXRenderable* renderable)
{
	bool l = 0;

	// Based on the glibc implementation of pthread_spin_lock.
	// We assume the first try will be mostly successful,
	// thus we use atomic_exchange, which is assumed to be fast on success.
	if (!atomic_exchange_explicit(&renderable->lock, 1, memory_order_acquire))
		return;

	// Otherwise we use a weak CAS loop and not an exchange so we bail out
	// after a failed attempt and fallback to an atomic_load.
	// This has the advantage that the atomic_load can be relaxed and we do not
	// force any expensive memory synchronizations and penalize other threads.
	while (!atomic_compare_exchange_weak_explicit(
		&renderable->lock, &l, 1,
		memory_order_acquire, memory_order_relaxed)) l = 0;
}

/****************************
 * Unlocks a renderable for pipeline retrieval.
 */
static inline void _gfx_renderable_unlock(GFXRenderable* renderable)
{
	atomic_store_explicit(&renderable->lock, 0, memory_order_release);
}

/****************************
 * Retrieves a graphics pipeline from the renderer's cache (or warms it up).
 * Essentially a wrapper for _gfx_cache_(get|warmup).
 * @param renderable Cannot be NULL.
 * @param elem       Output cache element, cannot be NULL if warmup is zero.
 * @param warmup     Non-zero to only warmup and not retrieve.
 * @return Zero on failure.
 *
 * Completely thread-safe with respect to the renderable!
 */
static bool _gfx_renderable_pipeline(GFXRenderable* renderable,
                                     _GFXCacheElem** elem, bool warmup)
{
	assert(renderable != NULL);
	assert(warmup || elem != NULL);

	// Firstly, spin-lock the renderable and check if we have an up-to-date
	// pipeline, if so, we can just return :)
	// Immediately unlock afterwards for maximum concurrency!
	_gfx_renderable_lock(renderable);

	if (
		renderable->pipeline != (uintptr_t)NULL &&
		renderable->gen == renderable->pass->gen)
	{
		if (!warmup) *elem = (void*)renderable->pipeline;
		_gfx_renderable_unlock(renderable);
		return 1;
	}

	_gfx_renderable_unlock(renderable);

	// We do not have a pipeline, create a new one.
	// Multiple threads could end up creating the same new pipeline, but
	// this is not expected to be a consistently occuring event so it's fine.
	GFXRenderer* renderer = renderable->pass->renderer;
	GFXPass* pass = renderable->pass;
	GFXTechnique* tech = renderable->technique;
	_GFXPrimitive* prim = (_GFXPrimitive*)renderable->primitive;

	const void* handles[_GFX_NUM_SHADER_STAGES + 2];
	uint32_t numShaders = 0;

	// Set & validate hashing handles.
	for (uint32_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		if (tech->shaders[s] != NULL)
			handles[numShaders++] = tech->shaders[s];

	handles[numShaders+0] = tech->layout;
	handles[numShaders+1] = pass->build.pass;

	if (handles[numShaders+0] == NULL || handles[numShaders+1] == NULL)
	{
		gfx_log_warn("Invalid renderable; pipeline not built.");
		return 0;
	}

	// Build create info.
	const size_t numAttribs = prim != NULL ? prim->numAttribs : 0;
	const size_t numBindings = prim != NULL ? prim->numBindings : 0;
	VkPipelineShaderStageCreateInfo pstci[numShaders > 0 ? numShaders : 1];
	VkVertexInputAttributeDescription viad[numAttribs > 0 ? numAttribs : 1];
	VkVertexInputBindingDescription vibd[numBindings > 0 ? numBindings : 1];

	for (uint32_t s = 0; s < numShaders; ++s)
		pstci[s] = (VkPipelineShaderStageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,

			.pNext  = NULL,
			.flags  = 0,
			.stage  = _GFX_GET_VK_SHADER_STAGE(((GFXShader*)handles[s])->stage),
			.module = ((GFXShader*)handles[s])->vk.module,
			.pName  = "main",

			.pSpecializationInfo = NULL
		};

	for (size_t i = 0; i < numAttribs; ++i)
		viad[i] = (VkVertexInputAttributeDescription){
			.location = (uint32_t)i,
			.binding  = prim->attribs[i].binding,
			.format   = prim->attribs[i].vk.format,
			.offset   = prim->attribs[i].offset
		};

	for (size_t i = 0; i < numBindings; ++i)
		vibd[i] = (VkVertexInputBindingDescription){
			.binding   = (uint32_t)i,
			.stride    = prim->bindings[i].stride,
			// TODO: Make it take from input.
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		};

	VkGraphicsPipelineCreateInfo gpci = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,

		.pNext              = NULL,
		.flags              = 0,
		.stageCount         = numShaders,
		.pStages            = pstci,
		.layout             = tech->layout->vk.layout,
		.renderPass         = pass->vk.pass,
		.subpass            = 0,
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex  = -1,
		.pTessellationState = NULL,
		.pDepthStencilState = NULL,

		.pVertexInputState = (VkPipelineVertexInputStateCreateInfo[]){{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,

			.pNext                           = NULL,
			.flags                           = 0,
			.vertexAttributeDescriptionCount = (uint32_t)numAttribs,
			.pVertexAttributeDescriptions    = viad,
			.vertexBindingDescriptionCount   = (uint32_t)numBindings,
			.pVertexBindingDescriptions      = vibd
		}},

		.pInputAssemblyState = (VkPipelineInputAssemblyStateCreateInfo[]){{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,

			.pNext    = NULL,
			.flags    = 0,
			.topology = prim != NULL ?
				_GFX_GET_VK_PRIMITIVE_TOPOLOGY(prim->base.topology) : 0,

			.primitiveRestartEnable = VK_FALSE
		}},

		.pViewportState = (VkPipelineViewportStateCreateInfo[]){{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,

			.pNext         = NULL,
			.flags         = 0,
			.viewportCount = 1,
			.pViewports    = NULL,
			.scissorCount  = 1,
			.pScissors     = NULL
		}},

		// TODO: Somehow take as input.
		.pRasterizationState = (VkPipelineRasterizationStateCreateInfo[]){{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,

			.pNext                   = NULL,
			.flags                   = 0,
			.depthClampEnable        = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode             = VK_POLYGON_MODE_FILL,
			.cullMode                = VK_CULL_MODE_BACK_BIT,
			.frontFace               = VK_FRONT_FACE_CLOCKWISE,
			.depthBiasEnable         = VK_FALSE,
			.depthBiasConstantFactor = 0.0f,
			.depthBiasClamp          = 0.0f,
			.depthBiasSlopeFactor    = 0.0f,
			.lineWidth               = 1.0f
		}},

		// TODO: Somehow take as input.
		.pMultisampleState = (VkPipelineMultisampleStateCreateInfo[]){{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,

			.pNext                 = NULL,
			.flags                 = 0,
			.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable   = VK_FALSE,
			.minSampleShading      = 1.0f,
			.pSampleMask           = NULL,
			.alphaToCoverageEnable = VK_FALSE,
			.alphaToOneEnable      = VK_FALSE
		}},

		// TODO: Take as input from the pass.
		.pColorBlendState = (VkPipelineColorBlendStateCreateInfo[]){{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,

			.pNext           = NULL,
			.flags           = 0,
			.logicOpEnable   = VK_FALSE,
			.logicOp         = VK_LOGIC_OP_COPY,
			.attachmentCount = 1,
			.blendConstants  = { 0.0f, 0.0f, 0.0f, 0.0f },

			.pAttachments = (VkPipelineColorBlendAttachmentState[]){{
				.blendEnable         = VK_FALSE,
				.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
				.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
				.colorBlendOp        = VK_BLEND_OP_ADD,
				.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
				.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
				.alphaBlendOp        = VK_BLEND_OP_ADD,
				.colorWriteMask      =
					VK_COLOR_COMPONENT_R_BIT |
					VK_COLOR_COMPONENT_G_BIT |
					VK_COLOR_COMPONENT_B_BIT |
					VK_COLOR_COMPONENT_A_BIT
			}}
		}},

		.pDynamicState = (VkPipelineDynamicStateCreateInfo[]){{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,

			.pNext             = NULL,
			.flags             = 0,
			.dynamicStateCount = 2,
			.pDynamicStates    = (VkDynamicState[]){
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR
			}
		}}
	};

	if (warmup)
		// If asked to warmup, just do that :)
		return _gfx_cache_warmup(&renderer->cache, &gpci.sType, handles);
	else
	{
		// Otherwise, actually retrieve the pipeline.
		*elem = _gfx_cache_get(&renderer->cache, &gpci.sType, handles);

		// Finally, update the stored pipeline!
		// Skip this step on failure tho.
		if (*elem == NULL) return 0;

		_gfx_renderable_lock(renderable);

		renderable->pipeline = (uintptr_t)(void*)*elem;
		renderable->gen = pass->gen;

		_gfx_renderable_unlock(renderable);

		return 1;
	}
}

/****************************
 * Retrieves a compute pipeline from the renderer's cache (or warms it up).
 * Essentially a wrapper for _gfx_cache_(get|warmup).
 * @param computable Cannot be NULL.
 * @see _gfx_renderable_pipeline.
 *
 * Completely thread-safe with respect to the computable!
 */
static bool _gfx_computable_pipeline(GFXComputable* computable,
                                     _GFXCacheElem** elem, bool warmup)
{
	assert(computable != NULL);
	assert(warmup || elem != NULL);

	// Unlike for renderables,
	// we can just check the pipeline and return when it's there!
	_GFXCacheElem* pipeline = (void*)atomic_load_explicit(
		&computable->pipeline, memory_order_relaxed);

	if (pipeline != NULL)
	{
		if (!warmup) *elem = pipeline;
		return 1;
	}

	// We do not have a pipeline, create a new one.
	// Again, multiple threads creating the same one is fine.
	GFXRenderer* renderer = computable->technique->renderer;
	GFXTechnique* tech = computable->technique;
	const void* handles[2];

	// Set & validate hashing handles.
	handles[0] = tech->shaders[_GFX_GET_SHADER_STAGE_INDEX(GFX_STAGE_COMPUTE)];
	handles[1] = tech->layout;

	if (handles[0] == NULL || handles[1] == NULL)
	{
		gfx_log_warn("Invalid computable; pipeline not built.");
		return 0;
	}

	// Build create info.
	VkComputePipelineCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,

		.pNext              = NULL,
		.flags              = 0,
		.layout             = tech->layout->vk.layout,
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex  = -1,

		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,

			.pNext               = NULL,
			.flags               = 0,
			.stage               = VK_SHADER_STAGE_COMPUTE_BIT,
			.module              = ((GFXShader*)handles[0])->vk.module,
			.pName               = "main",
			.pSpecializationInfo = NULL
		}
	};

	if (warmup)
		// If asked to warmup, just do that :)
		return _gfx_cache_warmup(&renderer->cache, &cpci.sType, handles);
	else
	{
		// Otherwise, actually retrieve the pipeline.
		*elem = _gfx_cache_get(&renderer->cache, &cpci.sType, handles);

		// Finally, update the stored pipeline!
		// Skip this step on failure tho.
		if (*elem == NULL) return 0;

		atomic_store_explicit(
			&computable->pipeline,
			(uintptr_t)(void*)*elem, memory_order_relaxed);

		return 1;
	}
}

/****************************
 * Claims (or creates) a command buffer from the current recording pool.
 * To unclaim, the current pool's used count should be decreased.
 * @param pool Cannot be NULL.
 * @return The command buffer, NULL on failure.
 */
static VkCommandBuffer _gfx_recorder_claim(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	_GFXContext* context = recorder->renderer->allocator.context;
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
 * Outputs a command buffer of a specific submission order.
 * @param recorder Cannot be NULL.
 * @param elem     Command buffer + order to output.
 * @return Zero on failure.
 */
static bool _gfx_recorder_output(GFXRecorder* recorder, const _GFXCmdElem* elem)
{
	// Find the right spot to insert at.
	// We assume the most prevelant way of recording stuff is in submission
	// order. Which would make backwards linear search perfect.
	size_t loc;
	for (loc = recorder->out.cmds.size; loc > 0; --loc)
	{
		unsigned int order =
			((_GFXCmdElem*)gfx_vec_at(&recorder->out.cmds, loc-1))->order;

		if (order <= elem->order)
			break;
	}

	// Insert at found position.
	return gfx_vec_insert(&recorder->out.cmds, 1, elem, loc);
}

/****************************/
bool _gfx_recorder_reset(GFXRecorder* recorder, unsigned int frame)
{
	assert(recorder != NULL);
	assert(frame < recorder->renderer->numFrames);

	_GFXContext* context = recorder->renderer->allocator.context;

	// Set new current index & clear output.
	recorder->current = frame;
	gfx_vec_release(&recorder->out.cmds);

	// Try to reset the command pool.
	_GFX_VK_CHECK(
		context->vk.ResetCommandPool(
			context->vk.device, recorder->pools[frame].vk.pool, 0),
		{
			gfx_log_warn("Resetting of recorder failed.");
			return 0;
		});

	// TODO: Maybe here or somewhere else; free buffers we're not using.

	// No command buffers are in use anymore.
	recorder->pools[frame].used = 0;

	return 1;
}

/****************************/
void _gfx_recorder_record(GFXRecorder* recorder, unsigned int order,
                          VkCommandBuffer cmd)
{
	assert(recorder != NULL);
	assert(cmd != NULL);

	_GFXContext* context = recorder->renderer->allocator.context;

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
GFX_API bool gfx_renderable(GFXRenderable* renderable,
                            GFXPass* pass, GFXTechnique* tech, GFXPrimitive* prim)
{
	assert(renderable != NULL);
	assert(pass != NULL);
	assert(tech != NULL);

	// Neat place to check renderer & context sharing.
	if (
		pass->renderer != tech->renderer ||
		(prim != NULL &&
			((_GFXPrimitive*)prim)->buffer.heap->allocator.context !=
			pass->renderer->allocator.context))
	{
		gfx_log_error(
			"Could not initialize renderable; its pass and technique must "
			"share a renderable and be built on the same logical Vulkan "
			"device as its primitive.");

		return 0;
	}

	// Renderables cannot hold compute shaders!
	if (tech->shaders[_GFX_GET_SHADER_STAGE_INDEX(GFX_STAGE_COMPUTE)] != NULL)
	{
		gfx_log_error(
			"Could not initialize renderable; cannot hold a compute shader.");

		return 0;
	}

	// Init renderable, store NULL as pipeline.
	renderable->pass = pass;
	renderable->technique = tech;
	renderable->primitive = prim;

	atomic_store_explicit(&renderable->lock, 0, memory_order_relaxed);
	renderable->pipeline = (uintptr_t)NULL;
	renderable->gen = 0;

	return 1;
}

/****************************/
GFX_API bool gfx_renderable_warmup(GFXRenderable* renderable)
{
	assert(renderable != NULL);

	// TODO: Need to somehow verify that the pass is up to date and built!

	return _gfx_renderable_pipeline(renderable, NULL, 1);
}

/****************************/
GFX_API bool gfx_computable(GFXComputable* computable,
                            GFXTechnique* tech)
{
	assert(computable != NULL);
	assert(tech != NULL);

	// Computables can only hold compute shaders!
	if (tech->shaders[_GFX_GET_SHADER_STAGE_INDEX(GFX_STAGE_COMPUTE)] == NULL)
	{
		gfx_log_error(
			"Could not initialize computable; can only hold a compute shader.");

		return 0;
	}

	// Init computable, store NULL as pipeline.
	computable->technique = tech;
	atomic_store_explicit(
		&computable->pipeline, (uintptr_t)NULL, memory_order_relaxed);

	return 0;
}

/****************************/
GFX_API bool gfx_computable_warmup(GFXComputable* computable)
{
	assert(computable != NULL);

	return _gfx_computable_pipeline(computable, NULL, 1);
}

/****************************/
GFX_API GFXRecorder* gfx_renderer_add_recorder(GFXRenderer* renderer)
{
	assert(renderer != NULL);
	assert(!renderer->recording);

	_GFXContext* context = renderer->allocator.context;

	// Allocate a new recorder.
	GFXRecorder* rec = malloc(
		sizeof(GFXRecorder) +
		sizeof(_GFXRecorderPool) * renderer->numFrames);

	if (rec == NULL)
		goto error;

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
	if (renderer->pFrame.vk.done != VK_NULL_HANDLE)
		rec->current = renderer->pFrame.index;

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
	// Still, NOT thread-safe with respect to the virtual frame deque!
	for (unsigned int i = 0; i < renderer->numFrames; ++i)
		_gfx_push_stale(renderer,
			VK_NULL_HANDLE, VK_NULL_HANDLE, recorder->pools[i].vk.pool);

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
	_GFXContext* context = rend->allocator.context;

	// Firstly, claim a command buffer to use.
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
			.renderPass  = pass->vk.pass,
			.subpass     = 0, // TODO: Determine this.
			.framebuffer = _gfx_pass_framebuffer(pass, &rend->pFrame),

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

	cb(recorder, recorder->current, ptr);

	recorder->inp.pass = NULL;
	recorder->inp.cmd = NULL;

	_GFX_VK_CHECK(
		context->vk.EndCommandBuffer(cmd),
		goto error);

	// Now insert the command buffer in its correct position.
	// Which is in submission order of the passes.
	_GFXCmdElem elem = {
		.order = pass->order,
		.cmd = cmd
	};

	if (!_gfx_recorder_output(recorder, &elem))
		goto error;

	return;


	// Error on failure.
error:
	gfx_log_error("Recorder failed to record render commands.");
}

/****************************/
GFX_API void gfx_recorder_compute(GFXRecorder* recorder, GFXComputeFlags flags,
                                  GFXPass* relative,
                                  void (*cb)(GFXRecorder*, unsigned int, void*),
                                  void* ptr,
                                  size_t numDeps, const GFXInject* deps)
{
	assert(recorder != NULL);
	assert(recorder->renderer->recording);
	assert(relative == NULL || relative->renderer == recorder->renderer);
	assert(cb != NULL);
	assert(numDeps == 0 || deps != NULL);

	// TODO: Implement.
}
