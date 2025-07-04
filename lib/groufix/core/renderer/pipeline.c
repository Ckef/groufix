/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


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

/****************************/
bool _gfx_renderable_pipeline(GFXRenderable* renderable,
                              _GFXCacheElem** elem, bool warmup)
{
	assert(renderable != NULL);
	assert(warmup || elem != NULL);

	_GFXRenderPass* rPass = (_GFXRenderPass*)renderable->pass;

	// Firstly, spin-lock the renderable and check if we have an up-to-date
	// pipeline, if so, we can just return :)
	// Immediately unlock afterwards for maximum concurrency!
	_gfx_renderable_lock(renderable);

	if (
		renderable->pipeline != (uintptr_t)NULL &&
		renderable->gen == _GFX_PASS_GEN(rPass))
	{
		if (!warmup) *elem = (void*)renderable->pipeline;
		_gfx_renderable_unlock(renderable);
		return 1;
	}

	_gfx_renderable_unlock(renderable);

	// We do not have a pipeline, create a new one.
	// Multiple threads could end up creating the same new pipeline, but
	// this is not expected to be a consistently occuring event so it's fine.
	GFXTechnique* tech = renderable->technique;
	_GFXPrimitive* prim = (_GFXPrimitive*)renderable->primitive;

	const void* handles[_GFX_NUM_SHADER_STAGES + 2];
	uint32_t numShaders = 0;

	// Set & validate hashing handles.
	for (uint32_t s = 0; s < _GFX_NUM_SHADER_STAGES; ++s)
		if (tech->shaders[s] != NULL)
			// Shader pointers will be converted to handles down below.
			handles[numShaders++] = tech->shaders[s];

	if (tech->layout == NULL)
	{
		gfx_log_warn("Technique not locked while building pipeline.");
		return 0;
	}

	if (rPass->build.pass == NULL)
	{
		gfx_log_warn("Pass not warmed while building pipeline.");
		return 0;
	}

	handles[numShaders+0] = tech->layout;
	handles[numShaders+1] = rPass->build.pass;

	// Gather appropriate state data.
	const GFXRasterState* raster =
		(renderable->state != NULL && renderable->state->raster != NULL) ?
		renderable->state->raster : &rPass->state.raster;

	const GFXBlendState* blend =
		(renderable->state != NULL && renderable->state->blend != NULL) ?
		renderable->state->blend : &rPass->state.blend;

	const GFXDepthState* depth =
		(renderable->state != NULL && renderable->state->depth != NULL) ?
		renderable->state->depth : &rPass->state.depth;

	const GFXStencilState* stencil =
		(renderable->state != NULL && renderable->state->stencil != NULL) ?
		renderable->state->stencil : &rPass->state.stencil;

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
	VkPipelineColorBlendAttachmentState pcbas[rPass->vk.blends.size];

	for (size_t i = 0; i < rPass->vk.blends.size; ++i)
	{
		const GFXBlendOpState* blendOp = gfx_vec_at(&rPass->vk.blends, i);
		const char isInd = *(char*)(blendOp + 2);

		const GFXBlendOpState* color = isInd ? (blendOp + 0) : &blend->color;
		const GFXBlendOpState* alpha = isInd ? (blendOp + 1) : &blend->alpha;

		pcbas[i] = (VkPipelineColorBlendAttachmentState){
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
		};

		if (color->op != GFX_BLEND_NO_OP)
		{
			pcbas[i].blendEnable = VK_TRUE;
			pcbas[i].srcColorBlendFactor =
				_GFX_GET_VK_BLEND_FACTOR(color->srcFactor);
			pcbas[i].dstColorBlendFactor =
				_GFX_GET_VK_BLEND_FACTOR(color->dstFactor);
			pcbas[i].colorBlendOp =
				_GFX_GET_VK_BLEND_OP(color->op);
		}

		if (alpha->op != GFX_BLEND_NO_OP)
		{
			pcbas[i].blendEnable = VK_TRUE;
			pcbas[i].srcAlphaBlendFactor =
				_GFX_GET_VK_BLEND_FACTOR(alpha->srcFactor);
			pcbas[i].dstAlphaBlendFactor =
				_GFX_GET_VK_BLEND_FACTOR(alpha->dstFactor);
			pcbas[i].alphaBlendOp =
				_GFX_GET_VK_BLEND_OP(alpha->op);
		}
	}

	VkPipelineColorBlendStateCreateInfo pcbsci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,

		.pNext           = NULL,
		.flags           = 0,
		.logicOpEnable   = VK_FALSE,
		.logicOp         = VK_LOGIC_OP_COPY,
		.attachmentCount = (uint32_t)rPass->vk.blends.size,
		.pAttachments    = pcbas,
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

	if (!noRaster && (rPass->state.enabled & _GFX_PASS_DEPTH))
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

	if (!noRaster && (rPass->state.enabled & _GFX_PASS_STENCIL))
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
	VkPipelineShaderStageCreateInfo pstci[GFX_MAX(1, numShaders)];
	VkSpecializationInfo si[_GFX_NUM_SHADER_STAGES];
	VkSpecializationMapEntry sme[GFX_MAX(1, numConsts)];

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

		// And convert shaders to handles in the handles array.
		handles[s] = (void*)shader->handle;
	}

	// Build create info.
	const size_t numAttribs = prim != NULL ? prim->numAttribs : 0;
	const size_t numBindings = prim != NULL ? prim->numBindings : 0;
	VkVertexInputAttributeDescription viad[GFX_MAX(1, numAttribs)];
	VkVertexInputBindingDescription vibd[GFX_MAX(1, numBindings)];

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
		.renderPass          = rPass->vk.pass,
		.subpass             = rPass->out.subpass,
		.basePipelineHandle  = VK_NULL_HANDLE,
		.basePipelineIndex   = -1,
		.pRasterizationState = &prsci,
		.pTessellationState  = NULL,
		.pColorBlendState    = &pcbsci,

		.pDepthStencilState =
			// Even if rasterization is disabled, Vulkan expects this.
			rPass->state.enabled & (_GFX_PASS_DEPTH | _GFX_PASS_STENCIL) ?
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
				_GFX_GET_VK_PRIMITIVE_TOPOLOGY(prim->base.topology) :
				_GFX_GET_VK_PRIMITIVE_TOPOLOGY(raster->topo),

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

		.pMultisampleState = (VkPipelineMultisampleStateCreateInfo[]){{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,

			.pNext                 = NULL,
			.flags                 = 0,
			.sampleShadingEnable   = VK_FALSE,
			.minSampleShading      = 1.0f,
			.pSampleMask           = NULL,
			.alphaToCoverageEnable = VK_FALSE,
			.alphaToOneEnable      = VK_FALSE,
			.rasterizationSamples  =
				_GFX_GET_VK_SAMPLE_COUNT(
					GFX_MAX(raster->samples, rPass->state.samples)),
		}},

		.pDynamicState = (VkPipelineDynamicStateCreateInfo[]){{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,

			.pNext             = NULL,
			.flags             = 0,
			.dynamicStateCount = 3,
			.pDynamicStates    = (VkDynamicState[]){
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
				VK_DYNAMIC_STATE_LINE_WIDTH
			}
		}}
	};

	if (warmup)
		// If asked to warmup, just do that :)
		return _gfx_cache_warmup(&tech->renderer->cache, &gpci.sType, handles);
	else
	{
		// Otherwise, actually retrieve the pipeline.
		*elem = _gfx_cache_get(&tech->renderer->cache, &gpci.sType, handles);

		// Finally, update the stored pipeline!
		// Skip this step on failure tho.
		if (*elem == NULL) return 0;

		_gfx_renderable_lock(renderable);

		renderable->pipeline = (uintptr_t)(void*)*elem;
		renderable->gen = _GFX_PASS_GEN(rPass);

		_gfx_renderable_unlock(renderable);

		return 1;
	}
}

/****************************/
bool _gfx_computable_pipeline(GFXComputable* computable,
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
	const void* handles[2];

	// Set & validate hashing handles.
	const uint32_t stage = _GFX_GET_SHADER_STAGE_INDEX(GFX_STAGE_COMPUTE);
	const GFXShader* shader = tech->shaders[stage];

	if (shader == NULL)
	{
		gfx_log_warn("Missing compute shader while building pipeline.");
		return 0;
	}

	if (tech->layout == NULL)
	{
		gfx_log_warn("Technique not locked while building pipeline.");
		return 0;
	}

	handles[0] = (void*)shader->handle;
	handles[1] = tech->layout;

	// Build create info.
	const size_t numConsts = tech->constants.size;
	VkSpecializationInfo si[_GFX_NUM_SHADER_STAGES];
	VkSpecializationMapEntry sme[GFX_MAX(1, numConsts)];

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
			.module = shader->vk.module,
			.pName  = "main",

			// Do not pass anything if no entries; for smaller hashes!
			.pSpecializationInfo =
				(si[stage].mapEntryCount > 0) ? si + stage : NULL
		}
	};

	if (warmup)
		// If asked to warmup, just do that :)
		return _gfx_cache_warmup(&tech->renderer->cache, &cpci.sType, handles);
	else
	{
		// Otherwise, actually retrieve the pipeline.
		*elem = _gfx_cache_get(&tech->renderer->cache, &cpci.sType, handles);

		// Finally, update the stored pipeline!
		// Skip this step on failure tho.
		if (*elem == NULL) return 0;

		atomic_store_explicit(
			&computable->pipeline,
			(uintptr_t)(void*)*elem, memory_order_relaxed);

		return 1;
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
			pass->renderer->cache.context))
	{
		gfx_log_error(
			"Could not initialize renderable; its pass and technique must "
			"share a renderer and be built on the same logical Vulkan "
			"device as its primitive.");

		return 0;
	}

	// Renderables must be built for a render pass!
	if (pass->type != GFX_PASS_RENDER)
	{
		gfx_log_error(
			"Could not initialize renderable; pass must be a render pass.");

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

	if (!success)
	{
		gfx_log_error("Could not warm renderable; graph warmup failed.");
		return 0;
	}

	// Then build it.
	if (!_gfx_renderable_pipeline(renderable, NULL, 1))
	{
		gfx_log_error("Could not warm renderable; pipeline not built.");
		return 0;
	}

	return 1;
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

	// Just build it.
	if (!_gfx_computable_pipeline(computable, NULL, 1))
	{
		gfx_log_error("Could not warm computable; pipeline not built.");
		return 0;
	}

	return 1;
}
