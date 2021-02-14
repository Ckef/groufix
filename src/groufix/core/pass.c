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
 * Partial destruct, assumes any output window attachments still exists.
 * Useful when the swapchain got recreated because of a resize or smth.
 * @param pass Cannot be NULL.
 */
static void _gfx_render_pass_destruct_partial(GFXRenderPass* pass)
{
	assert(pass != NULL);

	_GFXContext* context = pass->renderer->context;

	// Destroy all framebuffers.
	for (size_t i = 0; i < pass->vk.framebuffers.size; ++i)
	{
		VkFramebuffer* frame =
			gfx_vec_at(&pass->vk.framebuffers, i);
		context->vk.DestroyFramebuffer(
			context->vk.device, *frame, NULL);
	}

	// Destroy the other Vulkan objects.
	context->vk.DestroyRenderPass(
		context->vk.device, pass->vk.pass, NULL);
	context->vk.DestroyPipelineLayout(
		context->vk.device, pass->vk.layout, NULL);
	context->vk.DestroyPipeline(
		context->vk.device, pass->vk.pipeline, NULL);

	pass->vk.pass = VK_NULL_HANDLE;
	pass->vk.layout = VK_NULL_HANDLE;
	pass->vk.pipeline = VK_NULL_HANDLE;

	gfx_vec_release(&pass->vk.framebuffers);
}

/****************************
 * Validates and picks a window to use as back-buffer and (re)builds
 * appropriate resources if necessary.
 * @param pass Cannot be NULL.
 * @return Non-zero if successful (zero if multiple windows found).
 */
static int _gfx_render_pass_rebuild_backing(GFXRenderPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->context;

	// Validate that there is exactly 1 window we write to.
	// We don't have but we're nice, otherwise Vulkan would spam the logs.
	size_t backing = SIZE_MAX;

	// Check out all write attachments.
	for (size_t w = 0; w < pass->writes.size; ++w)
	{
		size_t index = *(size_t*)gfx_vec_at(&pass->writes, w);

		// Try to find the write attachment as window.
		size_t b;
		for (b = 0; b < rend->windows.size; ++b)
		{
			_GFXWindowAttach* at = gfx_vec_at(&rend->windows, b);
			if (at->index == index) break;
		}

		if (b >= rend->windows.size)
			continue;

		// If found, check if we already had a window.
		if (backing == SIZE_MAX)
			backing = b;
		else
		{
			// If so, well we cannot so we error.
			gfx_log_error(
				"A single render pass can only write to a single "
				"window attachments at a time.");

			return 0;
		}
	}

	// Now if the current backing window was detached,
	// the renderer is required to call _gfx_render_pass_destruct,
	// meaning there is no current backing or it's the same.
	pass->build.backing = backing;

	// Render pass doesn't write to a window, perfect.
	if (pass->build.backing == SIZE_MAX)
		return 1;

	// Ok so we chose a backing window.
	// Now we allocate more command buffers or free some.
	_GFXWindowAttach* attach = gfx_vec_at(&rend->windows, backing);
	size_t currCount = pass->vk.commands.size;
	size_t count = attach->vk.views.size;

	if (currCount < count)
	{
		// If we have too few, allocate some more.
		// Reserve the exact amount cause it's most likely not gonna change.
		if (!gfx_vec_reserve(&pass->vk.commands, count))
			goto error;

		size_t newCount = count - currCount;
		gfx_vec_push_empty(&pass->vk.commands, newCount);

		VkCommandBufferAllocateInfo cbai = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

			.pNext              = NULL,
			.commandPool        = attach->vk.pool,
			.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = (uint32_t)newCount
		};

		int res = 1;
		_GFX_VK_CHECK(
			context->vk.AllocateCommandBuffers(
				context->vk.device, &cbai,
				gfx_vec_at(&pass->vk.commands, currCount)),
			res = 0);

		// Throw away the items we just tried to insert.
		if (!res)
		{
			gfx_vec_pop(&pass->vk.commands, newCount);
			goto error;
		}
	}

	else if (currCount > count)
	{
		// If we have too many, free some.
		context->vk.FreeCommandBuffers(
			context->vk.device,
			attach->vk.pool,
			(uint32_t)(currCount - count),
			gfx_vec_at(&pass->vk.commands, count));

		gfx_vec_pop(&pass->vk.commands, currCount - count);
	}

	return 1;


	// Error on failure.
error:
	gfx_log_error(
		"Could not allocate resources for a window attachment written to "
		"by a render pass.");

	return 0;
}

/****************************/
GFXRenderPass* _gfx_create_render_pass(GFXRenderer* renderer,
                                       size_t numDeps, GFXRenderPass** deps)
{
	assert(renderer != NULL);
	assert(numDeps == 0 || deps != NULL);

	// Check if all dependencies use this renderer.
	for (size_t d = 0; d < numDeps; ++d)
		if (deps[d]->renderer != renderer)
		{
			gfx_log_warn(
				"Render pass cannot depend on a pass associated "
				"with a different renderer.");

			return NULL;
		}

	// Allocate a new render pass.
	GFXRenderPass* pass = malloc(
		sizeof(GFXRenderPass) +
		sizeof(GFXRenderPass*) * numDeps);

	if (pass == NULL)
		return NULL;


	// TODO: Super temporary!!
	const char vert[] =
		"#version 450\n"
		"#extension GL_ARB_separate_shader_objects : enable\n"
		"layout(location = 0) out vec3 fragColor;\n"
		"out gl_PerVertex {\n"
		"  vec4 gl_Position;\n"
		"};\n"
		"vec2 positions[3] = vec2[](\n"
		"  vec2(0.0, -0.5),\n"
		"  vec2(0.5, 0.5),\n"
		"  vec2(-0.5, 0.5)\n"
		");\n"
		"vec3 colors[3] = vec3[](\n"
		"  vec3(1.0, 0.0, 0.0),\n"
		"  vec3(0.0, 1.0, 0.0),\n"
		"  vec3(0.0, 0.0, 1.0)\n"
		");\n"
		"void main() {\n"
		"  gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);\n"
		"  fragColor = colors[gl_VertexIndex];\n"
		"}\n";

	const char frag[] =
		"#version 450\n"
		"#extension GL_ARB_separate_shader_objects : enable\n"
		"layout(location = 0) in vec3 fragColor;\n"
		"layout(location = 0) out vec4 outColor;\n"
		"void main() {\n"
		"  outColor = vec4(fragColor, 1.0);\n"
		"}\n";

	pass->vertex = gfx_create_shader(GFX_SHADER_VERTEX, NULL);
	pass->fragment = gfx_create_shader(GFX_SHADER_FRAGMENT, NULL);

	if (pass->vertex == NULL || pass->fragment == NULL)
		return NULL;

	gfx_shader_compile(pass->vertex, GFX_GLSL, vert, 1, NULL);
	gfx_shader_compile(pass->fragment, GFX_GLSL, frag, 1, NULL);


	// Initialize things.
	pass->renderer = renderer;
	pass->level = 0;
	pass->refs = 0;
	pass->numDeps = numDeps;

	if (numDeps) memcpy(
		pass->deps, deps, sizeof(GFXRenderPass*) * numDeps);

	for (size_t d = 0; d < numDeps; ++d)
	{
		// The level is the highest level of all dependencies + 1.
		if (deps[d]->level >= pass->level)
			pass->level = deps[d]->level + 1;

		// Increase the reference count of each dependency.
		// TODO: Maybe we want to filter out duplicates?
		++deps[d]->refs;
	}

	// Initialize building stuff.
	pass->build.backing = SIZE_MAX;

	pass->vk.pass = VK_NULL_HANDLE;
	pass->vk.layout = VK_NULL_HANDLE;
	pass->vk.pipeline = VK_NULL_HANDLE;

	gfx_vec_init(&pass->vk.framebuffers, sizeof(VkFramebuffer));
	gfx_vec_init(&pass->vk.commands, sizeof(VkCommandBuffer));

	gfx_vec_init(&pass->reads, sizeof(size_t));
	gfx_vec_init(&pass->writes, sizeof(size_t));

	return pass;
}

/****************************/
void _gfx_destroy_render_pass(GFXRenderPass* pass)
{
	assert(pass != NULL);


	// TODO: Super temporary!!
	gfx_destroy_shader(pass->vertex);
	gfx_destroy_shader(pass->fragment);


	// Destroy Vulkan object structure.
	_gfx_render_pass_destruct(pass);

	// Clear all pre-building information.
	gfx_vec_clear(&pass->reads);
	gfx_vec_clear(&pass->writes);

	// Decrease the reference count of each dependency.
	// TODO: Maybe we want to filter out duplicates?
	for (size_t d = 0; d < pass->numDeps; ++d)
		--pass->deps[d]->refs;

	free(pass);
}

/****************************/
int _gfx_render_pass_rebuild(GFXRenderPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->context;
	_GFXWindowAttach* attach = NULL;

	// Destruct previous build.
	_gfx_render_pass_destruct_partial(pass);

	// Rebuild all backing related resources.
	if (!_gfx_render_pass_rebuild_backing(pass))
		goto clean;

	if (pass->build.backing != SIZE_MAX)
		attach = gfx_vec_at(&rend->windows, pass->build.backing);

	// TODO: Future: if no back-buffer, do smth else.
	if (attach == NULL)
		goto clean;

	// Go build a new render pass.
	VkAttachmentDescription ad = {
		.flags          = 0,
		.format         = attach->window->frame.format,
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
		context->vk.device, &rpci, NULL, &pass->vk.pass), goto clean);

	// Create framebuffers.
	// Reserve the exact amount, it's probably not gonna change.
	// TODO: Do we really need multiple framebuffers? Maybe just blit into image?
	if (!gfx_vec_reserve(&pass->vk.framebuffers, attach->vk.views.size))
		goto clean;

	for (size_t i = 0; i < attach->vk.views.size; ++i)
	{
		VkFramebufferCreateInfo fci = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,

			.pNext           = NULL,
			.flags           = 0,
			.renderPass      = pass->vk.pass,
			.attachmentCount = 1,
			.pAttachments    = gfx_vec_at(&attach->vk.views, i),
			.width           = (uint32_t)attach->window->frame.width,
			.height          = (uint32_t)attach->window->frame.height,
			.layers          = 1
		};

		VkFramebuffer frame;
		_GFX_VK_CHECK(context->vk.CreateFramebuffer(
			context->vk.device, &fci, NULL, &frame), goto clean);

		gfx_vec_push(&pass->vk.framebuffers, 1, &frame);
	}

	// Pipeline shader stages.
	VkPipelineShaderStageCreateInfo pstci[] = { {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,

			.pNext               = NULL,
			.flags               = 0,
			.stage               = VK_SHADER_STAGE_VERTEX_BIT,
			.module              = pass->vertex->vk.module,
			.pName               = "main",
			.pSpecializationInfo = NULL

		}, {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,

			.pNext               = NULL,
			.flags               = 0,
			.stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module              = pass->fragment->vk.module,
			.pName               = "main",
			.pSpecializationInfo = NULL
		}
	};

	// Pipeline vertex input state.
	VkPipelineVertexInputStateCreateInfo pvisci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,

		.pNext                           = NULL,
		.flags                           = 0,
		.vertexBindingDescriptionCount   = 0,
		.pVertexBindingDescriptions      = NULL,
		.vertexAttributeDescriptionCount = 0,
		.pVertexAttributeDescriptions    = NULL
	};

	// Pipeline input assembly state.
	VkPipelineInputAssemblyStateCreateInfo piasci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,

		.pNext                  = NULL,
		.flags                  = 0,
		.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE
	};

	// Pipeline viewport state.
	VkViewport viewport = {
		.x        = 0.0f,
		.y        = 0.0f,
		.width    = (float)attach->window->frame.width,
		.height   = (float)attach->window->frame.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = {
			(uint32_t)attach->window->frame.width,
			(uint32_t)attach->window->frame.height
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

	// Create a pipeline layout.
	VkPipelineLayoutCreateInfo plci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,

		.pNext                  = NULL,
		.flags                  = 0,
		.setLayoutCount         = 0,
		.pSetLayouts            = NULL,
		.pushConstantRangeCount = 0,
		.pPushConstantRanges    = NULL
	};

	_GFX_VK_CHECK(context->vk.CreatePipelineLayout(
		context->vk.device, &plci, NULL, &pass->vk.layout), goto clean);

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
		.layout              = pass->vk.layout,
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
		goto clean);

	// Now go record all of the command buffers.
	VkClearValue clear = {
		.color = {{ 0.0f, 0.0f, 0.0f, 0.0f }}
	};

	VkCommandBufferBeginInfo cbbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
		.pInheritanceInfo = NULL
	};

	for (size_t i = 0; i < pass->vk.commands.size; ++i)
	{
		VkFramebuffer frame =
			*(VkFramebuffer*)gfx_vec_at(&pass->vk.framebuffers, i);
		VkCommandBuffer buffer =
			*(VkCommandBuffer*)gfx_vec_at(&pass->vk.commands, i);

		// Start of all commands.
		_GFX_VK_CHECK(context->vk.BeginCommandBuffer(buffer, &cbbi),
			goto clean);

		// Begin render pass, bind pipeline, draw, and end pass.
		VkRenderPassBeginInfo rpbi = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,

			.pNext           = NULL,
			.renderPass      = pass->vk.pass,
			.framebuffer     = frame,
			.renderArea      = scissor,
			.clearValueCount = 1,
			.pClearValues    = &clear
		};

		context->vk.CmdBeginRenderPass(
			buffer, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

		context->vk.CmdBindPipeline(
			buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pass->vk.pipeline);

		context->vk.CmdDraw(
			buffer, 3, 1, 0, 0);

		context->vk.CmdEndRenderPass(
			buffer);

		// End of all commands.
		_GFX_VK_CHECK(context->vk.EndCommandBuffer(buffer),
			goto clean);
	}

	return 1;


	// Clean on failure.
clean:
	gfx_log_error("Could not (re)build a render pass.");
	_gfx_render_pass_destruct(pass);

	return 0;
}

/****************************/
void _gfx_render_pass_destruct(GFXRenderPass* pass)
{
	assert(pass != NULL);

	_GFXContext* context = pass->renderer->context;

	// Destruct things we'd also destroy during a rebuild.
	_gfx_render_pass_destruct_partial(pass);

	// If we use a window as back-buffer, destroy those resources.
	if (pass->build.backing != SIZE_MAX)
	{
		// Because it is required to call this before detaching any window
		// attachments, pass->build.backing must still be valid.
		_GFXWindowAttach* attach =
			gfx_vec_at(&pass->renderer->windows, pass->build.backing);

		// Free all command buffers.
		if (pass->vk.commands.size > 0)
			context->vk.FreeCommandBuffers(
				context->vk.device,
				attach->vk.pool,
				(uint32_t)pass->vk.commands.size,
				pass->vk.commands.data);

		pass->build.backing = SIZE_MAX;
	}

	gfx_vec_clear(&pass->vk.framebuffers);
	gfx_vec_clear(&pass->vk.commands);
}

/****************************/
GFX_API int gfx_render_pass_read(GFXRenderPass* pass, size_t index)
{
	assert(pass != NULL);

	// Try to find it first.
	// Just a linear search, nothing is sorted, whatever.
	for (size_t i = 0; i < pass->reads.size; ++i)
		if (*(size_t*)gfx_vec_at(&pass->reads, i) == index)
			return 1;

	if (!gfx_vec_push(&pass->reads, 1, &index))
		return 0;

	// Changed a pass, the renderer must rebuild.
	pass->renderer->built = 0;

	return 1;
}

/****************************/
GFX_API int gfx_render_pass_write(GFXRenderPass* pass, size_t index)
{
	assert(pass != NULL);

	// Try to find it first.
	for (size_t i = 0; i < pass->writes.size; ++i)
		if (*(size_t*)gfx_vec_at(&pass->writes, i) == index)
			return 1;

	if (!gfx_vec_push(&pass->writes, 1, &index))
		return 0;

	// Changed a pass, the renderer must rebuild.
	pass->renderer->built = 0;

	return 1;
}

/****************************/
GFX_API size_t gfx_render_pass_get_num_deps(GFXRenderPass* pass)
{
	assert(pass != NULL);

	return pass->numDeps;
}

/****************************/
GFX_API GFXRenderPass* gfx_render_pass_get_dep(GFXRenderPass* pass, size_t dep)
{
	assert(pass != NULL);
	assert(dep < pass->numDeps);

	return pass->deps[dep];
}
