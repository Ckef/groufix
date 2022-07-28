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


#define _GFX_GET_VK_PRIMITIVE_TOPOLOGY(topo) \
	((topo) == GFX_TOPO_POINT_LIST ? \
		VK_PRIMITIVE_TOPOLOGY_POINT_LIST : \
	(topo) == GFX_TOPO_LINE_LIST ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST : \
	(topo) == GFX_TOPO_LINE_STRIP ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_STRIP : \
	(topo) == GFX_TOPO_TRIANGLE_LIST ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : \
	(topo) == GFX_TOPO_TRIANGLE_STRIP ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : \
	(topo) == GFX_TOPO_TRIANGLE_FAN ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN : \
	(topo) == GFX_TOPO_LINE_LIST_ADJACENT ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY : \
	(topo) == GFX_TOPO_LINE_STRIP_ADJACENT ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY : \
	(topo) == GFX_TOPO_TRIANGLE_LIST_ADJACENT ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY : \
	(topo) == GFX_TOPO_TRIANGLE_STRIP_ADJACENT ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY : \
	(topo) == GFX_TOPO_PATCH_LIST ? \
		VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)

#define _GFX_GET_VK_POLYGON_MODE(mode) \
	(((mode) == GFX_RASTER_POINT) ? VK_POLYGON_MODE_POINT : \
	((mode) == GFX_RASTER_LINE) ? VK_POLYGON_MODE_LINE : \
	((mode) == GFX_RASTER_FILL) ? VK_POLYGON_MODE_FILL : \
	VK_POLYGON_MODE_FILL)

#define _GFX_GET_VK_FRONT_FACE(front) \
	(((front) == GFX_FRONT_FACE_CCW) ? VK_FRONT_FACE_COUNTER_CLOCKWISE : \
	((front) == GFX_FRONT_FACE_CW) ? VK_FRONT_FACE_CLOCKWISE : \
	VK_FRONT_FACE_CLOCKWISE)

#define _GFX_GET_VK_CULL_MODE(cull) \
	(((cull) == GFX_CULL_FRONT ? \
		VK_CULL_MODE_FRONT_BIT : (VkCullModeFlags)0) | \
	((cull) == GFX_CULL_BACK ? \
		VK_CULL_MODE_BACK_BIT : (VkCullModeFlags)0))


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
	GFXPass* pass = renderable->pass;
	GFXRenderer* renderer = pass->renderer;
	GFXTechnique* tech = renderable->technique;
	_GFXPrimitive* prim = (_GFXPrimitive*)renderable->primitive;

	const void* handles[_GFX_NUM_SHADER_STAGES + 2];
	uint32_t numShaders = 0;

	// TODO: Use other handles so shaders can be destroyed?
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

	// Gather appropriate state data.
	const GFXRasterState* raster =
		(renderable->state != NULL && renderable->state->raster != NULL) ?
		renderable->state->raster : &pass->state.raster;

	const GFXBlendState* blend =
		(renderable->state != NULL && renderable->state->blend != NULL) ?
		renderable->state->blend : &pass->state.blend;

	const GFXDepthState* depth =
		(renderable->state != NULL && renderable->state->depth != NULL) ?
		renderable->state->depth : &pass->state.depth;

	const GFXStencilState* stencil =
		(renderable->state != NULL && renderable->state->stencil != NULL) ?
		renderable->state->stencil : &pass->state.stencil;

	// Build rasterization info.
	const bool noRaster = (raster->mode == GFX_RASTER_DISCARD);

	VkPipelineRasterizationStateCreateInfo prsci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,

		.pNext                   = NULL,
		.flags                   = 0,
		.depthClampEnable        = VK_FALSE,
		.rasterizerDiscardEnable = VK_TRUE,
		.polygonMode             = VK_POLYGON_MODE_FILL,
		.cullMode                = VK_CULL_MODE_NONE,
		.frontFace               = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable         = VK_FALSE,
		.depthBiasConstantFactor = 0.0f,
		.depthBiasClamp          = 0.0f,
		.depthBiasSlopeFactor    = 0.0f,
		.lineWidth               = 1.0f
	};

	if (!noRaster)
	{
		prsci.rasterizerDiscardEnable = VK_FALSE;

		prsci.polygonMode = _GFX_GET_VK_POLYGON_MODE(raster->mode);
		prsci.cullMode = _GFX_GET_VK_CULL_MODE(raster->cull);
		prsci.frontFace = _GFX_GET_VK_FRONT_FACE(raster->front);
	}

	// Build blend info.
	VkPipelineColorBlendStateCreateInfo pcbsci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,

		.pNext           = NULL,
		.flags           = 0,
		.logicOpEnable   = VK_FALSE,
		.logicOp         = VK_LOGIC_OP_COPY,
		.attachmentCount = (uint32_t)pass->vk.blends.size,
		.pAttachments    = gfx_vec_at(&pass->vk.blends, 0),
		.blendConstants  = { 0.0f, 0.0f, 0.0f, 0.0f }
	};

	if (!noRaster)
	{
		if (blend->logic != GFX_LOGIC_NO_OP)
		{
			pcbsci.logicOpEnable = VK_TRUE;
			pcbsci.logicOp = _GFX_GET_VK_LOGIC_OP(blend->logic);
		}
		else
		{
			pcbsci.blendConstants[0] = blend->constants[0];
			pcbsci.blendConstants[1] = blend->constants[1];
			pcbsci.blendConstants[2] = blend->constants[2];
			pcbsci.blendConstants[3] = blend->constants[3];
		}
	}

	// Build depth/stencil info.
	const VkStencilOpState sos = {
		.failOp      = VK_STENCIL_OP_KEEP,
		.passOp      = VK_STENCIL_OP_KEEP,
		.depthFailOp = VK_STENCIL_OP_KEEP,
		.compareOp   = VK_COMPARE_OP_NEVER,
		.compareMask = 0,
		.writeMask   = 0,
		.reference   = 0
	};

	VkPipelineDepthStencilStateCreateInfo pdssci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,

		.pNext                 = NULL,
		.flags                 = 0,
		.depthTestEnable       = VK_FALSE,
		.depthWriteEnable      = VK_FALSE,
		.depthCompareOp        = VK_COMPARE_OP_ALWAYS,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable     = VK_FALSE,
		.front                 = sos,
		.back                  = sos,
		.minDepthBounds        = 0.0f,
		.maxDepthBounds        = 1.0f
	};

	if (!noRaster && (pass->state.enabled & _GFX_PASS_DEPTH))
	{
		pdssci.depthTestEnable = VK_TRUE;
		pdssci.depthCompareOp = _GFX_GET_VK_COMPARE_OP(depth->cmp);

		if (depth->flags & GFX_DEPTH_WRITE)
			pdssci.depthWriteEnable = VK_TRUE;

		if (depth->flags & GFX_DEPTH_BOUNDED)
		{
			pdssci.depthBoundsTestEnable = VK_TRUE;
			pdssci.minDepthBounds = depth->minDepth;
			pdssci.maxDepthBounds = depth->maxDepth;
		}
	}

	if (!noRaster && (pass->state.enabled & _GFX_PASS_STENCIL))
	{
		pdssci.stencilTestEnable = VK_TRUE;

		pdssci.front = (VkStencilOpState){
			.failOp = _GFX_GET_VK_STENCIL_OP(stencil->front.fail),
			.passOp = _GFX_GET_VK_STENCIL_OP(stencil->front.pass),
			.depthFailOp = _GFX_GET_VK_STENCIL_OP(stencil->front.depthFail),
			.compareOp = _GFX_GET_VK_COMPARE_OP(stencil->front.cmp),
			.compareMask = stencil->front.cmpMask,
			.writeMask = stencil->front.writeMask,
			.reference = stencil->front.reference
		};

		pdssci.back = (VkStencilOpState){
			.failOp = _GFX_GET_VK_STENCIL_OP(stencil->back.fail),
			.passOp = _GFX_GET_VK_STENCIL_OP(stencil->back.pass),
			.depthFailOp = _GFX_GET_VK_STENCIL_OP(stencil->back.depthFail),
			.compareOp = _GFX_GET_VK_COMPARE_OP(stencil->back.cmp),
			.compareMask = stencil->back.cmpMask,
			.writeMask = stencil->back.writeMask,
			.reference = stencil->back.reference
		};
	}

	// Build shader info.
	const size_t numConsts = tech->constants.size;
	VkPipelineShaderStageCreateInfo pstci[numShaders > 0 ? numShaders : 1];
	VkSpecializationInfo si[_GFX_NUM_SHADER_STAGES];
	VkSpecializationMapEntry sme[numConsts > 0 ? numConsts : 1];

	_gfx_tech_get_constants(tech, si, sme);

	for (uint32_t s = 0; s < numShaders; ++s)
	{
		const GFXShader* shader = handles[s];
		const uint32_t stage = _GFX_GET_SHADER_STAGE_INDEX(shader->stage);

		pstci[s] = (VkPipelineShaderStageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,

			.pNext  = NULL,
			.flags  = 0,
			.stage  = _GFX_GET_VK_SHADER_STAGE(shader->stage),
			.module = shader->vk.module,
			.pName  = "main",

			// Do not pass anything if no entries; for smaller hashes!
			.pSpecializationInfo =
				(si[stage].mapEntryCount > 0) ? si + stage : NULL
		};
	}

	// Build create info.
	const size_t numAttribs = prim != NULL ? prim->numAttribs : 0;
	const size_t numBindings = prim != NULL ? prim->numBindings : 0;
	VkVertexInputAttributeDescription viad[numAttribs > 0 ? numAttribs : 1];
	VkVertexInputBindingDescription vibd[numBindings > 0 ? numBindings : 1];

	for (size_t i = 0; i < numAttribs; ++i)
		viad[i] = (VkVertexInputAttributeDescription){
			.location = (uint32_t)i,
			.binding  = prim->attribs[i].binding,
			.format   = prim->attribs[i].vk.format,
			.offset   = prim->attribs[i].base.offset
		};

	for (size_t i = 0; i < numBindings; ++i)
		vibd[i] = (VkVertexInputBindingDescription){
			.binding   = (uint32_t)i,
			.stride    = prim->bindings[i].stride,
			.inputRate = prim->bindings[i].rate
		};

	VkGraphicsPipelineCreateInfo gpci = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,

		.pNext               = NULL,
		.flags               = 0,
		.stageCount          = numShaders,
		.pStages             = pstci,
		.layout              = tech->vk.layout,
		.renderPass          = pass->vk.pass,
		.subpass             = 0,
		.basePipelineHandle  = VK_NULL_HANDLE,
		.basePipelineIndex   = -1,
		.pRasterizationState = &prsci,
		.pTessellationState  = NULL,
		.pColorBlendState    = &pcbsci,

		.pDepthStencilState =
			// Even if rasterization is disabled, Vulkan expects this.
			pass->state.enabled & (_GFX_PASS_DEPTH | _GFX_PASS_STENCIL) ?
			&pdssci : NULL,

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
	GFXTechnique* tech = computable->technique;
	GFXRenderer* renderer = tech->renderer;
	const void* handles[2];

	// TODO: Use other shader handle so the shader can be destroyed?
	// Set & validate hashing handles.
	const uint32_t stage = _GFX_GET_SHADER_STAGE_INDEX(GFX_STAGE_COMPUTE);
	handles[0] = tech->shaders[stage];
	handles[1] = tech->layout;

	if (handles[0] == NULL || handles[1] == NULL)
	{
		gfx_log_warn("Invalid computable; pipeline not built.");
		return 0;
	}

	// Build create info.
	const size_t numConsts = tech->constants.size;
	VkSpecializationInfo si[_GFX_NUM_SHADER_STAGES];
	VkSpecializationMapEntry sme[numConsts > 0 ? numConsts : 1];

	_gfx_tech_get_constants(tech, si, sme);

	VkComputePipelineCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,

		.pNext              = NULL,
		.flags              = 0,
		.layout             = tech->vk.layout,
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex  = -1,

		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,

			.pNext  = NULL,
			.flags  = 0,
			.stage  = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = ((GFXShader*)handles[0])->vk.module,
			.pName  = "main",

			// Do not pass anything if no entries; for smaller hashes!
			.pSpecializationInfo =
				(si[stage].mapEntryCount > 0) ? si + stage : NULL
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
	if (recorder->bind.gPipeline != elem)
	{
		recorder->bind.gPipeline = elem;
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
	if (recorder->bind.cPipeline != elem)
	{
		recorder->bind.cPipeline = elem;
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
bool _gfx_recorder_reset(GFXRecorder* recorder, unsigned int frame)
{
	assert(recorder != NULL);
	assert(frame < recorder->renderer->numFrames);

	_GFXContext* context = recorder->context;

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
GFX_API bool gfx_renderable(GFXRenderable* renderable,
                            GFXPass* pass, GFXTechnique* tech, GFXPrimitive* prim,
                            const GFXRenderState* state)
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
			"share a renderer and be built on the same logical Vulkan "
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
	renderable->state = state;

	atomic_store_explicit(&renderable->lock, 0, memory_order_relaxed);
	renderable->pipeline = (uintptr_t)NULL;
	renderable->gen = 0;

	return 1;
}

/****************************/
GFX_API bool gfx_renderable_warmup(GFXRenderable* renderable)
{
	assert(renderable != NULL);

	GFXRenderer* renderer = renderable->pass->renderer;

	// To build pipelines, we need the Vulkan render pass.
	// This is the exact reason we can warmup all passes of the render graph!
	// Sadly this is not thread-safe at all, so we re-use the renderer's lock.
	_gfx_mutex_lock(&renderer->lock);
	bool success = _gfx_render_graph_warmup(renderer);
	_gfx_mutex_unlock(&renderer->lock);

	if (success)
		return _gfx_renderable_pipeline(renderable, NULL, 1);

	return 0;
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

	return 1;
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

	// Check for the presence of a framebuffer.
	VkFramebuffer framebuffer = _gfx_pass_framebuffer(pass, &rend->pFrame);
	if (framebuffer == VK_NULL_HANDLE) return;

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
			.renderPass  = pass->vk.pass,
			.subpass     = 0,
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
		.width    = (float)pass->build.fWidth,
		.height   = (float)pass->build.fHeight,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = {
			pass->build.fWidth,
			pass->build.fHeight
		}
	};

	context->vk.CmdSetViewport(cmd, 0, 1, &viewport);
	context->vk.CmdSetScissor(cmd, 0, 1, &scissor);

	// Set recording input, record, unset input.
	recorder->inp.pass = pass;
	recorder->inp.cmd = cmd;
	recorder->bind.gPipeline = NULL;
	recorder->bind.cPipeline = NULL;
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

	// TODO: Implement.
}

/****************************/
GFX_API void gfx_recorder_get_size(GFXRecorder* recorder,
                                   uint32_t* width, uint32_t* height, uint32_t* layers)
{
	assert(recorder != NULL);
	assert(recorder->inp.cmd != NULL);
	assert(width != NULL);
	assert(height != NULL);
	assert(layers != NULL);

	// Output 0,0 if no associated pass.
	if (recorder->inp.pass == NULL)
		*width = 0,
		*height = 0,
		*layers = 0;
	else
		*width = recorder->inp.pass->build.fWidth,
		*height = recorder->inp.pass->build.fHeight,
		*layers = recorder->inp.pass->build.fLayers;
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
                          uint32_t firstVertex, uint32_t vertices,
                          uint32_t firstInstance, uint32_t instances)
{
	assert(recorder != NULL);
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
                                  uint32_t firstIndex, uint32_t indices,
                                  int32_t vertexOffset,
                                  uint32_t firstInstance, uint32_t instances)
{
	assert(recorder != NULL);
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
GFX_API void gfx_cmd_dispatch(GFXRecorder* recorder, GFXComputable* computable,
                              uint32_t groupX, uint32_t groupY, uint32_t groupZ)
{
	assert(recorder != NULL);
	assert(recorder->inp.cmd != NULL);
	// TODO: Check that the input pass is a compute pass?
	assert(computable != NULL);
	assert(computable->technique != NULL);
	assert(computable->technique->renderer == recorder->renderer);
	assert(groupX > 0);
	assert(groupY > 0);
	assert(groupZ > 0);

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
	context->vk.CmdDispatch(recorder->inp.cmd, groupX, groupY, groupZ);
}
