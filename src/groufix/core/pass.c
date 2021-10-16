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
#include <string.h>


/****************************
 * Destructs the Vulkan object structure, non-recursively.
 * @param pass  Cannot be NULL.
 * @param flags What resources should be destroyed (0 to do nothing).
 */
static void _gfx_pass_destruct_partial(GFXPass* pass,
                                       _GFXRecreateFlags flags)
{
	assert(pass != NULL);

	_GFXContext* context = pass->renderer->context;

	// The recreate flag is always set if anything is set and signals that
	// the actual images have been recreated.
	// So we destroy the framebuffer, which references the actual images.
	if (flags & _GFX_RECREATE)
	{
		for (size_t i = 0; i < pass->vk.framebuffers.size; ++i)
		{
			VkFramebuffer* frame =
				gfx_vec_at(&pass->vk.framebuffers, i);
			context->vk.DestroyFramebuffer(
				context->vk.device, *frame, NULL);
		}

		gfx_vec_release(&pass->vk.framebuffers);
	}

	// Second, we check if the render pass needs to be destroyed.
	// This is only the case when the format has changed.
	if (flags & _GFX_REFORMAT)
	{
		context->vk.DestroyRenderPass(
			context->vk.device, pass->vk.pass, NULL);

		pass->vk.pass = VK_NULL_HANDLE;
	}

	// Lastly we recreate all things that depend on a size.
	// They also depend on the render pass so we check for a reformat too.
	// TODO: They do not all have to depend on the render pass, to optimize..
	if (flags & (_GFX_REFORMAT | _GFX_RESIZE))
	{
		context->vk.DestroyPipeline(
			context->vk.device, pass->vk.pipeline, NULL);

		pass->vk.pipeline = VK_NULL_HANDLE;
	}
}

/****************************
 * Picks a window to use as back-buffer, silently logging issues.
 * @param pass Cannot be NULL.
 * @return The picked backing, SIZE_MAX if none found.
 */
static size_t _gfx_pass_pick_backing(GFXPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;

	size_t backing = SIZE_MAX;

	// Validate that there is exactly 1 window we write to.
	// We don't really have to but we're nice, in case of Vulkan spam...
	for (size_t w = 0; w < pass->writes.size; ++w)
	{
		size_t b = *(size_t*)gfx_vec_at(&pass->writes, w);

		// Validate that the attachment exists and is a window.
		if (b >= rend->backing.attachs.size)
			continue;

		_GFXAttach* at = gfx_vec_at(&rend->backing.attachs, b);
		if (at->type != _GFX_ATTACH_WINDOW)
			continue;

		// If it is, check if we already had a backing window.
		if (backing == SIZE_MAX)
			backing = b;
		else
		{
			// If so, well we cannot, throw a warning.
			gfx_log_warn(
				"A single pass can only write to a single "
				"window attachment at a time.");

			break;
		}
	}

	return backing;
}

/****************************
 * Builds all missing resources of the Vulkan object structure.
 * @return Non-zero on success.
 */
static int _gfx_pass_build_objects(GFXPass* pass)
{
	assert(pass != NULL);
	assert(pass->build.primitive != NULL); // TODO: Obviously temporary.
	assert(pass->build.group != NULL); // TODO: Uh oh, temporary.

	GFXRenderer* rend = pass->renderer;
	_GFXDevice* device = rend->device;
	_GFXContext* context = rend->context;
	_GFXAttach* at = NULL;
	_GFXPrimitive* prim = pass->build.primitive;
	_GFXGroup* group = pass->build.group;

	// Get the backing window attachment.
	if (pass->build.backing != SIZE_MAX)
		at = gfx_vec_at(&rend->backing.attachs, pass->build.backing);

	// Skip if there's no render target (e.g. minimized window).
	// TODO: Future: if no backing window, do smth else.
	if (at == NULL || at->window.vk.views.size == 0)
		return 1;

	// Create render pass.
	if (pass->vk.pass == VK_NULL_HANDLE)
	{
		VkAttachmentDescription ad = {
			.flags          = 0,
			.format         = at->window.window->frame.format,
			.samples        = VK_SAMPLE_COUNT_1_BIT,
			.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		};

		VkAttachmentReference ar = {
			.attachment = 0,
			.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		};

		VkSubpassDescription sd = {
			.flags                   = 0,
			.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount    = 0,
			.pInputAttachments       = NULL,
			.colorAttachmentCount    = 1,
			.pColorAttachments       = &ar,
			.pResolveAttachments     = NULL,
			.pDepthStencilAttachment = NULL,
			.preserveAttachmentCount = 0,
			.pPreserveAttachments    = NULL
		};

		VkRenderPassCreateInfo rpci = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,

			.pNext           = NULL,
			.flags           = 0,
			.attachmentCount = 1,
			.pAttachments    = &ad,
			.subpassCount    = 1,
			.pSubpasses      = &sd,
			.dependencyCount = 0,
			.pDependencies   = NULL
		};

		_GFX_VK_CHECK(context->vk.CreateRenderPass(
			context->vk.device, &rpci, NULL, &pass->vk.pass), goto error);
	}

	// Create framebuffers.
	if (pass->vk.framebuffers.size == 0)
	{
		// Reserve the exact amount, it's probably not gonna change.
		// TODO: Instead of building all framebuffers, just cache the ones we need?
		if (!gfx_vec_reserve(&pass->vk.framebuffers, at->window.vk.views.size))
			goto error;

		for (size_t i = 0; i < at->window.vk.views.size; ++i)
		{
			VkFramebufferCreateInfo fci = {
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,

				.pNext           = NULL,
				.flags           = 0,
				.renderPass      = pass->vk.pass,
				.attachmentCount = 1,
				.pAttachments    = gfx_vec_at(&at->window.vk.views, i),
				.width           = at->window.window->frame.width,
				.height          = at->window.window->frame.height,
				.layers          = 1
			};

			VkFramebuffer frame;
			_GFX_VK_CHECK(context->vk.CreateFramebuffer(
				context->vk.device, &fci, NULL, &frame), goto error);

			gfx_vec_push(&pass->vk.framebuffers, 1, &frame);
		}
	}

	// Create descriptor set layout.
	// TODO: Will be cached.
	if (pass->vk.setLayout == VK_NULL_HANDLE)
	{
		VkDescriptorSetLayoutCreateInfo dslci = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,

			.pNext        = NULL,
			.flags        = 0,
			.bindingCount = 1,
			.pBindings = (VkDescriptorSetLayoutBinding[]){{
				.binding            = 0,
				.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount    = 1,
				.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT,
				.pImmutableSamplers = NULL
			}}
		};

		_GFX_VK_CHECK(context->vk.CreateDescriptorSetLayout(
			context->vk.device, &dslci, NULL, &pass->vk.setLayout), goto error);
	}

	// Create descriptor pool.
	if (pass->vk.pool == VK_NULL_HANDLE)
	{
		VkDescriptorPoolCreateInfo dpci = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,

			.pNext         = NULL,
			.flags         = 0,
			.maxSets       = 1,
			.poolSizeCount = 1,
			.pPoolSizes = (VkDescriptorPoolSize[]){{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1
			}}
		};

		_GFX_VK_CHECK(context->vk.CreateDescriptorPool(
			context->vk.device, &dpci, NULL, &pass->vk.pool), goto error);
	}

	// Allocate descriptor set.
	// TODO: Will be cached.
	if (pass->vk.set == VK_NULL_HANDLE)
	{
		VkDescriptorSetAllocateInfo dsai = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,

			.pNext              = NULL,
			.descriptorPool     = pass->vk.pool,
			.descriptorSetCount = 1,
			.pSetLayouts        = &pass->vk.setLayout
		};

		_GFX_VK_CHECK(context->vk.AllocateDescriptorSets(
			context->vk.device, &dsai, &pass->vk.set), goto error);

		// Write the first array element of the first binding of the group lol.
		// TODO: binding.numElements > 0 means dynamic.
		// TODO: Renderable objects should define what range of a group to
		// take as their data, from which descriptor set shite can be derived.
		_GFXUnpackRef ubo = _gfx_ref_unpack(
			gfx_ref_group_buffer(&group->base, 0, 0, 0));

		if (ubo.obj.buffer == NULL)
			goto error;

		VkDescriptorBufferInfo dbi = {
			.buffer = ubo.obj.buffer->vk.buffer,
			.offset = ubo.value,
			.range  = group->bindings[0].elementSize
		};

		VkWriteDescriptorSet wds = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,

			.pNext            = NULL,
			.dstSet           = pass->vk.set,
			.dstBinding       = 0,
			.dstArrayElement  = 0,
			.descriptorCount  = 1,
			.descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo      = &dbi,
		};

		context->vk.UpdateDescriptorSets(
			context->vk.device, 1, &wds, 0, NULL);
	}

	// Create pipeline layout.
	// TODO: Will be cached.
	if (pass->vk.pipeLayout == VK_NULL_HANDLE)
	{
		VkPipelineLayoutCreateInfo plci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,

			.pNext                  = NULL,
			.flags                  = 0,
			.setLayoutCount         = 1,
			.pSetLayouts            = &pass->vk.setLayout,
			.pushConstantRangeCount = 0,
			.pPushConstantRanges    = NULL
		};

		_GFX_VK_CHECK(context->vk.CreatePipelineLayout(
			context->vk.device, &plci, NULL, &pass->vk.pipeLayout), goto error);
	}

	// Create pipeline.
	// TODO: Will be cached.
	if (pass->vk.pipeline == VK_NULL_HANDLE)
	{
		// Pipeline shader stages.
		VkPipelineShaderStageCreateInfo pstci[] = {
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,

				.pNext               = NULL,
				.flags               = 0,
				.stage               = VK_SHADER_STAGE_VERTEX_BIT,
				.module              = pass->build.vertex->vk.module,
				.pName               = "main",
				.pSpecializationInfo = NULL
			}, {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,

				.pNext               = NULL,
				.flags               = 0,
				.stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module              = pass->build.fragment->vk.module,
				.pName               = "main",
				.pSpecializationInfo = NULL
			}
		};

		// Pipeline vertex input state.
		VkVertexInputAttributeDescription viad[prim->numAttribs];

		for (size_t i = 0; i < prim->numAttribs; ++i)
		{
			GFXFormat fmt = prim->attribs[i].format;
			viad[i] = (VkVertexInputAttributeDescription){
				.location = (uint32_t)i,
				.binding  = 0,
				.format   = _gfx_resolve_format(device, &fmt, NULL),
				.offset   = prim->attribs[i].offset
			};
		}

		VkPipelineVertexInputStateCreateInfo pvisci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,

			.pNext                           = NULL,
			.flags                           = 0,
			.vertexAttributeDescriptionCount = (uint32_t)prim->numAttribs,
			.pVertexAttributeDescriptions    = viad,
			.vertexBindingDescriptionCount   = 1,
			.pVertexBindingDescriptions = (VkVertexInputBindingDescription[]){{
				.binding   = 0,
				.stride    = prim->base.stride,
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			}}
		};

		// Pipeline input assembly state.
		VkPipelineInputAssemblyStateCreateInfo piasci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,

			.pNext                  = NULL,
			.flags                  = 0,
			.topology               = _GFX_GET_VK_PRIMITIVE_TOPOLOGY(prim->base.topology),
			.primitiveRestartEnable = VK_FALSE
		};

		// Pipeline viewport state.
		VkViewport viewport = {
			.x        = 0.0f,
			.y        = 0.0f,
			.width    = (float)at->window.window->frame.width,
			.height   = (float)at->window.window->frame.height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};

		VkRect2D scissor = {
			.offset = { 0, 0 },
			.extent = {
				at->window.window->frame.width,
				at->window.window->frame.height
			}
		};

		VkPipelineViewportStateCreateInfo pvsci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,

			.pNext         = NULL,
			.flags         = 0,
			.viewportCount = 1,
			.pViewports    = &viewport,
			.scissorCount  = 1,
			.pScissors     = &scissor
		};

		// Pipeline rasterization state.
		VkPipelineRasterizationStateCreateInfo prsci = {
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
		};

		// Pipeline multisample state
		VkPipelineMultisampleStateCreateInfo pmsci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,

			.pNext                 = NULL,
			.flags                 = 0,
			.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable   = VK_FALSE,
			.minSampleShading      = 1.0,
			.pSampleMask           = NULL,
			.alphaToCoverageEnable = VK_FALSE,
			.alphaToOneEnable      = VK_FALSE
		};

		// Pipeline color blend state.
		VkPipelineColorBlendAttachmentState pcbas = {
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

		VkPipelineColorBlendStateCreateInfo pcbsci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,

			.pNext           = NULL,
			.flags           = 0,
			.logicOpEnable   = VK_FALSE,
			.logicOp         = VK_LOGIC_OP_COPY,
			.attachmentCount = 1,
			.pAttachments    = &pcbas,
			.blendConstants  = { 0.0f, 0.0f, 0.0f, 0.0f }
		};

		// Finally create graphics pipeline.
		VkGraphicsPipelineCreateInfo gpci = {
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,

			.pNext               = NULL,
			.flags               = 0,
			.stageCount          = 2,
			.pStages             = pstci,
			.pVertexInputState   = &pvisci,
			.pInputAssemblyState = &piasci,
			.pTessellationState  = NULL,
			.pViewportState      = &pvsci,
			.pRasterizationState = &prsci,
			.pMultisampleState   = &pmsci,
			.pDepthStencilState  = NULL,
			.pColorBlendState    = &pcbsci,
			.pDynamicState       = NULL,
			.layout              = pass->vk.pipeLayout,
			.renderPass          = pass->vk.pass,
			.subpass             = 0,
			.basePipelineHandle  = VK_NULL_HANDLE,
			.basePipelineIndex   = 0
		};

		_GFX_VK_CHECK(
			context->vk.CreateGraphicsPipelines(
				context->vk.device,
				VK_NULL_HANDLE,
				1, &gpci, NULL, &pass->vk.pipeline),
			goto error);
	}

	return 1;


	// Error on failure.
error:
	gfx_log_error("Could not allocate all resources of a pass.");

	return 0;
}

/****************************/
GFXPass* _gfx_create_pass(GFXRenderer* renderer,
                          size_t numDeps, GFXPass** deps)
{
	assert(renderer != NULL);
	assert(numDeps == 0 || deps != NULL);

	// Check if all dependencies use this renderer.
	for (size_t d = 0; d < numDeps; ++d)
		if (deps[d]->renderer != renderer)
		{
			gfx_log_warn(
				"Pass cannot depend on a pass associated "
				"with a different renderer.");

			return NULL;
		}

	// Allocate a new pass.
	GFXPass* pass = malloc(
		sizeof(GFXPass) +
		sizeof(GFXPass*) * numDeps);

	if (pass == NULL)
		return NULL;

	// Initialize things.
	pass->renderer = renderer;
	pass->level = 0;
	pass->numDeps = numDeps;

	if (numDeps) memcpy(
		pass->deps, deps, sizeof(GFXPass*) * numDeps);

	// The level is the highest level of all dependencies + 1.
	for (size_t d = 0; d < numDeps; ++d)
		if (deps[d]->level >= pass->level)
			pass->level = deps[d]->level + 1;

	// Initialize building stuff.
	pass->build.backing = SIZE_MAX;

	pass->vk.pass = VK_NULL_HANDLE;
	pass->vk.setLayout = VK_NULL_HANDLE;
	pass->vk.pool = VK_NULL_HANDLE;
	pass->vk.set = VK_NULL_HANDLE;
	pass->vk.pipeLayout = VK_NULL_HANDLE;
	pass->vk.pipeline = VK_NULL_HANDLE;

	gfx_vec_init(&pass->vk.framebuffers, sizeof(VkFramebuffer));
	gfx_vec_init(&pass->reads, sizeof(size_t));
	gfx_vec_init(&pass->writes, sizeof(size_t));


	// TODO: Super temporary!!
	const char vert[] =
		"#version 450\n"
		"#extension GL_ARB_separate_shader_objects : enable\n"
		"layout(row_major, set = 0, binding = 0) uniform UBO {\n"
		"  mat4 mvp;\n"
		"};\n"
		"layout(location = 0) in vec3 position;\n"
		"layout(location = 1) in vec3 color;\n"
		"layout(location = 0) out vec3 fragColor;\n"
		"out gl_PerVertex {\n"
		"  vec4 gl_Position;\n"
		"};\n"
		"void main() {\n"
		"  gl_Position = mvp * vec4(position, 1.0);\n"
		"  fragColor = color;\n"
		"}\n";

	const char frag[] =
		"#version 450\n"
		"#extension GL_ARB_separate_shader_objects : enable\n"
		"layout(location = 0) in vec3 fragColor;\n"
		"layout(location = 0) out vec4 outColor;\n"
		"void main() {\n"
		"  outColor = vec4(fragColor, 1.0);\n"
		"}\n";

	pass->build.primitive = NULL;
	pass->build.group = NULL;
	pass->build.vertex = gfx_create_shader(GFX_SHADER_VERTEX, NULL);
	pass->build.fragment = gfx_create_shader(GFX_SHADER_FRAGMENT, NULL);

	gfx_shader_compile(pass->build.vertex, GFX_GLSL, vert, 1, NULL);
	gfx_shader_compile(pass->build.fragment, GFX_GLSL, frag, 1, NULL);


	return pass;
}

/****************************/
void _gfx_destroy_pass(GFXPass* pass)
{
	assert(pass != NULL);


	// TODO: Super temporary!!
	gfx_destroy_shader(pass->build.vertex);
	gfx_destroy_shader(pass->build.fragment);


	// Destroy Vulkan object structure.
	_gfx_pass_destruct(pass);

	// Clear all pre-building information.
	gfx_vec_clear(&pass->reads);
	gfx_vec_clear(&pass->writes);

	free(pass);
}

/****************************/
int _gfx_pass_build(GFXPass* pass, _GFXRecreateFlags flags)
{
	assert(pass != NULL);

	// First we destroy the things we want to recreate.
	_gfx_pass_destruct_partial(pass, flags);

	// Pick a backing window if we did not yet.
	if (pass->build.backing == SIZE_MAX)
		pass->build.backing = _gfx_pass_pick_backing(pass);

	// Aaaand then build the entire Vulkan object structure.
	if (!_gfx_pass_build_objects(pass))
		goto clean;

	return 1;


	// Clean on failure.
clean:
	gfx_log_error("Could not (re)build a pass.");
	_gfx_pass_destruct(pass);

	return 0;
}

/****************************/
void _gfx_pass_destruct(GFXPass* pass)
{
	assert(pass != NULL);

	_GFXContext* context = pass->renderer->context;

	// Remove reference to backing window.
	pass->build.backing = SIZE_MAX;

	// Destruct all partial things first.
	_gfx_pass_destruct_partial(pass, _GFX_RECREATE_ALL);

	// Destroy all non-partial things too.
	context->vk.DestroyDescriptorSetLayout(
		context->vk.device, pass->vk.setLayout, NULL);

	context->vk.DestroyDescriptorPool(
		context->vk.device, pass->vk.pool, NULL);

	context->vk.DestroyPipelineLayout(
		context->vk.device, pass->vk.pipeLayout, NULL);

	pass->vk.setLayout = VK_NULL_HANDLE;
	pass->vk.pool = VK_NULL_HANDLE;
	pass->vk.set = VK_NULL_HANDLE;
	pass->vk.pipeLayout = VK_NULL_HANDLE;

	// Clear memory.
	gfx_vec_clear(&pass->vk.framebuffers);
}

/****************************/
GFX_API int gfx_pass_read(GFXPass* pass, size_t index)
{
	assert(pass != NULL);
	assert(pass->renderer->pFrame.vk.done == VK_NULL_HANDLE);

	// Try to find it first.
	// Just a linear search, nothing is sorted, whatever.
	for (size_t i = 0; i < pass->reads.size; ++i)
		if (*(size_t*)gfx_vec_at(&pass->reads, i) == index)
			return 1;

	if (!gfx_vec_push(&pass->reads, 1, &index))
		return 0;

	// Changed a pass, the graph is invalidated.
	_gfx_render_graph_invalidate(pass->renderer);

	return 1;
}

/****************************/
GFX_API int gfx_pass_write(GFXPass* pass, size_t index)
{
	assert(pass != NULL);
	assert(pass->renderer->pFrame.vk.done == VK_NULL_HANDLE);

	// Try to find it first.
	for (size_t i = 0; i < pass->writes.size; ++i)
		if (*(size_t*)gfx_vec_at(&pass->writes, i) == index)
			return 1;

	if (!gfx_vec_push(&pass->writes, 1, &index))
		return 0;

	// Changed a pass, the graph is invalidated.
	_gfx_render_graph_invalidate(pass->renderer);

	return 1;
}

/****************************/
GFX_API void gfx_pass_release(GFXPass* pass, size_t index)
{
	assert(pass != NULL);
	assert(pass->renderer->pFrame.vk.done == VK_NULL_HANDLE);

	// Find and erase.
	for (size_t i = pass->reads.size; i > 0; --i)
		if (*(size_t*)gfx_vec_at(&pass->reads, i-1) == index)
			gfx_vec_erase(&pass->reads, 1, i-1);

	for (size_t i = pass->writes.size; i > 0; --i)
		if (*(size_t*)gfx_vec_at(&pass->writes, i-1) == index)
			gfx_vec_erase(&pass->writes, 1, i-1);

	// Changed a pass, the graph is invalidated.
	_gfx_render_graph_invalidate(pass->renderer);
}

/****************************/
GFX_API size_t gfx_pass_get_num_deps(GFXPass* pass)
{
	assert(pass != NULL);

	return pass->numDeps;
}

/****************************/
GFX_API GFXPass* gfx_pass_get_dep(GFXPass* pass, size_t dep)
{
	assert(pass != NULL);
	assert(dep < pass->numDeps);

	return pass->deps[dep];
}

/****************************/
GFX_API void gfx_pass_use(GFXPass* pass,
                          GFXPrimitive* primitive, GFXGroup* group)
{
	assert(pass != NULL);
	assert(pass->renderer->pFrame.vk.done == VK_NULL_HANDLE);
	assert(primitive != NULL);
	assert(group != NULL);

	// TODO: Should eventually check if they are using the same context.
	pass->build.primitive = (_GFXPrimitive*)primitive;
	pass->build.group = (_GFXGroup*)group;
}
