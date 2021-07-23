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
static void _gfx_render_pass_destruct_partial(GFXRenderPass* pass,
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
 */
static void _gfx_render_pass_pick_backing(GFXRenderPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;

	// We already have a back-buffer.
	if (pass->build.backing != SIZE_MAX)
		return;

	// Validate that there is exactly 1 window we write to.
	// We don't really have to but we're nice, in case of Vulkan spam...
	for (size_t w = 0; w < pass->writes.size; ++w)
	{
		size_t index = *(size_t*)gfx_vec_at(&pass->writes, w);

		// Validate that the attachment exists and is a window.
		if (index >= rend->backing.attachs.size)
			continue;

		_GFXAttach* at = gfx_vec_at(&rend->backing.attachs, index);
		if (at->type != _GFX_ATTACH_WINDOW)
			continue;

		// If it is, check if we already had a backing window.
		if (pass->build.backing == SIZE_MAX)
			pass->build.backing = index;
		else
		{
			// If so, well we cannot, throw a warning.
			gfx_log_warn(
				"A single render pass can only write to a single "
				"window attachment at a time.");

			break;
		}
	}
}

/****************************
 * Builds all missing resources of the back-buffer.
 * @param pass Cannot be NULL.
 * @return Non-zero on success.
 */
static int _gfx_render_pass_build_backing(GFXRenderPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->context;

	// Nothing to do.
	if (pass->build.backing == SIZE_MAX)
		return 1;

	// Now we allocate more command buffers or free some.
	_GFXAttach* at = gfx_vec_at(&rend->backing.attachs, pass->build.backing);
	size_t currCount = pass->vk.commands.size;
	size_t count = at->window.vk.views.size;

	if (currCount < count)
	{
		// If we have too few, allocate some more.
		// Reserve the exact amount cause it's most likely not gonna change.
		if (!gfx_vec_reserve(&pass->vk.commands, count))
			goto error;

		size_t newCount = count - currCount;
		gfx_vec_push(&pass->vk.commands, newCount, NULL);

		VkCommandBufferAllocateInfo cbai = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,

			.pNext              = NULL,
			.commandPool        = at->window.vk.pool,
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
			at->window.vk.pool,
			(uint32_t)(currCount - count),
			gfx_vec_at(&pass->vk.commands, count));

		gfx_vec_pop(&pass->vk.commands, currCount - count);
	}

	return 1;


	// Error on failure.
error:
	gfx_log_error(
		"Could not allocate resources for a window attachment "
		"written to by a render pass.");

	return 0;
}

/****************************
 * Builds all missing resources of the Vulkan object structure.
 * @return Non-zero on success.
 */
static int _gfx_render_pass_build_objects(GFXRenderPass* pass)
{
	assert(pass != NULL);
	assert(pass->build.mesh != NULL); // TODO: Obviously temporary.

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->context;
	_GFXAttach* at = NULL;
	_GFXMesh* mesh = pass->build.mesh;

	// At this point, we should check if we should re-record,
	// as now we know if we're missing resources from a previous record.
	int record =
		(pass->vk.pass == VK_NULL_HANDLE) ||
		(pass->vk.framebuffers.size == 0) ||
		(pass->vk.pipeline == VK_NULL_HANDLE);

	// Get the back-buffer attachment.
	if (pass->build.backing != SIZE_MAX)
		at = gfx_vec_at(&rend->backing.attachs, pass->build.backing);

	// TODO: Future: if no back-buffers, do smth else.
	if (at == NULL || at->window.vk.views.size == 0)
		return 0;

	// Yeah so we need the scissor of that attachment.
	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = {
			(uint32_t)at->window.window->frame.width,
			(uint32_t)at->window.window->frame.height
		}
	};

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
		// TODO: Do we really need multiple framebuffers? Maybe just blit into image?
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
				.width           = (uint32_t)at->window.window->frame.width,
				.height          = (uint32_t)at->window.window->frame.height,
				.layers          = 1
			};

			VkFramebuffer frame;
			_GFX_VK_CHECK(context->vk.CreateFramebuffer(
				context->vk.device, &fci, NULL, &frame), goto error);

			gfx_vec_push(&pass->vk.framebuffers, 1, &frame);
		}
	}

	// Create pipeline layout.
	if (pass->vk.layout == VK_NULL_HANDLE)
	{
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
			context->vk.device, &plci, NULL, &pass->vk.layout), goto error);
	}

	// Create pipeline.
	if (pass->vk.pipeline == VK_NULL_HANDLE)
	{
		// Pipeline shader stages.
		VkPipelineShaderStageCreateInfo pstci[] = { {
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
		VkVertexInputAttributeDescription viad[mesh->numAttribs];

		for (size_t i = 0; i < mesh->numAttribs; ++i)
			viad[i] = (VkVertexInputAttributeDescription){
				.location = (uint32_t)i,
				.binding  = 0,
				.format   = VK_FORMAT_R32G32B32_SFLOAT,
				.offset   = (uint32_t)mesh->offsets[i]
			};

		VkPipelineVertexInputStateCreateInfo pvisci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,

			.pNext                           = NULL,
			.flags                           = 0,
			.vertexAttributeDescriptionCount = (uint32_t)mesh->numAttribs,
			.pVertexAttributeDescriptions    = viad,
			.vertexBindingDescriptionCount   = 1,
			.pVertexBindingDescriptions = (VkVertexInputBindingDescription[]){{
				.binding   = 0,
				.stride    = (uint32_t)mesh->stride,
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			}}
		};

		// Pipeline input assembly state.
		VkPipelineInputAssemblyStateCreateInfo piasci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,

			.pNext                  = NULL,
			.flags                  = 0,
			.topology               = mesh->base.topology,
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
			goto error);
	}

	// Now go record all of the command buffers.
	if (record)
	{
		VkClearValue clear = {
			.color = {{ 0.0f, 0.0f, 0.0f, 0.0f }}
		};

		VkCommandBufferBeginInfo cbbi = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

			.pNext            = NULL,
			.flags            = 0,
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
				goto error);

			// Begin render pass, bind pipeline.
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

			// Bind index buffer.
			if (mesh->base.sizeIndices > 0)
			{
				_GFXUnpackRef index = _gfx_ref_unpack(
					gfx_ref_mesh_indices((GFXMesh*)mesh, 0));

				context->vk.CmdBindIndexBuffer(
					buffer,
					index.obj.buffer->vk.buffer,
					index.value,
					mesh->indexSize == sizeof(uint16_t) ?
						VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
			}

			// Bind vertex buffer.
			_GFXUnpackRef vertex = _gfx_ref_unpack(
				gfx_ref_mesh_vertices((GFXMesh*)mesh, 0));

			context->vk.CmdBindVertexBuffers(
				buffer, 0, 1,
				(VkBuffer[]){ vertex.obj.buffer->vk.buffer },
				(VkDeviceSize[]){ vertex.value });

			// Draw.
			if (mesh->base.sizeIndices > 0)
				context->vk.CmdDrawIndexed(
					buffer,
					(uint32_t)(mesh->base.sizeIndices / mesh->indexSize),
					1, 0, 0, 0);
			else
				context->vk.CmdDraw(
					buffer,
					(uint32_t)(mesh->base.sizeVertices / mesh->stride),
					1, 0, 0);

			context->vk.CmdEndRenderPass(
				buffer);

			// End of all commands.
			_GFX_VK_CHECK(context->vk.EndCommandBuffer(buffer),
				goto error);
		}
	}

	return 1;


	// Error on failure.
error:
	gfx_log_error(
		"Could not allocate or record all resources of a render pass.");

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

	// Initialize things.
	pass->renderer = renderer;
	pass->level = 0;
	pass->numDeps = numDeps;

	if (numDeps) memcpy(
		pass->deps, deps, sizeof(GFXRenderPass*) * numDeps);

	// The level is the highest level of all dependencies + 1.
	for (size_t d = 0; d < numDeps; ++d)
		if (deps[d]->level >= pass->level)
			pass->level = deps[d]->level + 1;

	// Initialize building stuff.
	pass->build.backing = SIZE_MAX;

	pass->vk.pass = VK_NULL_HANDLE;
	pass->vk.layout = VK_NULL_HANDLE;
	pass->vk.pipeline = VK_NULL_HANDLE;

	gfx_vec_init(&pass->vk.framebuffers, sizeof(VkFramebuffer));
	gfx_vec_init(&pass->vk.commands, sizeof(VkCommandBuffer));

	gfx_vec_init(&pass->reads, sizeof(size_t));
	gfx_vec_init(&pass->writes, sizeof(size_t));


	// TODO: Super temporary!!
	const char vert[] =
		"#version 450\n"
		"#extension GL_ARB_separate_shader_objects : enable\n"
		"layout(location = 0) in vec3 position;\n"
		"layout(location = 1) in vec3 color;\n"
		"layout(location = 0) out vec3 fragColor;\n"
		"out gl_PerVertex {\n"
		"  vec4 gl_Position;\n"
		"};\n"
		"void main() {\n"
		"  gl_Position = vec4(position, 1.0);\n"
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

	pass->build.mesh = NULL;
	pass->build.vertex = gfx_create_shader(GFX_SHADER_VERTEX, NULL);
	pass->build.fragment = gfx_create_shader(GFX_SHADER_FRAGMENT, NULL);

	gfx_shader_compile(pass->build.vertex, GFX_GLSL, vert, 1, NULL);
	gfx_shader_compile(pass->build.fragment, GFX_GLSL, frag, 1, NULL);


	return pass;
}

/****************************/
void _gfx_destroy_render_pass(GFXRenderPass* pass)
{
	assert(pass != NULL);


	// TODO: Super temporary!!
	gfx_destroy_shader(pass->build.vertex);
	gfx_destroy_shader(pass->build.fragment);


	// Destroy Vulkan object structure.
	_gfx_render_pass_destruct(pass);

	// Clear all pre-building information.
	gfx_vec_clear(&pass->reads);
	gfx_vec_clear(&pass->writes);

	free(pass);
}

/****************************/
int _gfx_render_pass_build(GFXRenderPass* pass,
                           _GFXRecreateFlags flags)
{
	assert(pass != NULL);

	// Augment flags with the 'window.flags' member of the associated backing.
	// Because calls to _gfx_render_graph_rebuild may be postponed to another
	// submission through 'window.flags', we use them in case built was set to
	// 0 or the graph got invalidated and the rebuild call didn't do anything.
	if (pass->build.backing != SIZE_MAX) flags |=
		((_GFXAttach*)gfx_vec_at(
			&pass->renderer->backing.attachs,
			pass->build.backing))->window.flags;

	// First we destroy the things we want to recreate.
	_gfx_render_pass_destruct_partial(pass, flags);

	// Pick a backing window if we did not yet.
	_gfx_render_pass_pick_backing(pass);

	// And then build all back-buffer related resources.
	if (!_gfx_render_pass_build_backing(pass))
		goto clean;

	// Aaaand then build the entire Vulkan object structure.
	// TODO: This also records, prolly want to split it up somehow.
	if (!_gfx_render_pass_build_objects(pass))
		goto clean;

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

	// Destruct all partial things first.
	_gfx_render_pass_destruct_partial(pass, _GFX_RECREATE_ALL);

	// Destroy all non-partial things too.
	context->vk.DestroyPipelineLayout(
		context->vk.device, pass->vk.layout, NULL);

	pass->vk.layout = VK_NULL_HANDLE;

	// If we use a window as back-buffer, destroy those resources.
	if (pass->build.backing != SIZE_MAX)
	{
		// This will have been called before detaching any window attachments,
		// by requirements, meaning pass->build.backing must still be valid.
		_GFXAttach* at =
			gfx_vec_at(&pass->renderer->backing.attachs, pass->build.backing);

		// Free all command buffers.
		if (pass->vk.commands.size > 0)
			context->vk.FreeCommandBuffers(
				context->vk.device,
				at->window.vk.pool,
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

	// Changed a pass, the graph is invalidated.
	_gfx_render_graph_invalidate(pass->renderer);

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

	// Changed a pass, the graph is invalidated.
	_gfx_render_graph_invalidate(pass->renderer);

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

/****************************/
GFX_API void gfx_render_pass_use(GFXRenderPass* pass, GFXMesh* mesh)
{
	assert(pass != NULL);
	assert(mesh != NULL);

	// TODO: Should eventually check if they are using the same context.
	pass->build.mesh = (_GFXMesh*)mesh;
}
