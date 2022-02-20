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
 * Attachment consumption element definition.
 */
typedef struct _GFXConsumeElem
{
	int            viewed; // Zero to ignore view.type.
	GFXAccessMask  mask;
	GFXShaderStage stage;
	GFXView        view;

} _GFXConsumeElem;


/****************************
 * Destructs the Vulkan object structure, non-recursively.
 * @param pass  Cannot be NULL.
 * @param flags What resources should be destroyed (0 to do nothing).
 */
static void _gfx_pass_destruct_partial(GFXPass* pass,
                                       _GFXRecreateFlags flags)
{
	assert(pass != NULL);

	_GFXContext* context = pass->renderer->allocator.context;

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

	// Second, we check if the render pass needs to be reconstructed.
	// All these objects are cached, so no need to destroy anything.
	// This is only the case when the format has changed.
	if (flags & _GFX_REFORMAT)
	{
		pass->vk.pass = VK_NULL_HANDLE;
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
	for (size_t i = 0; i < pass->consumes.size; ++i)
	{
		_GFXConsumeElem* con = gfx_vec_at(&pass->consumes, i);

		// Validate the access mask &
		// that the attachment exists and is a window.
		if (
			con->mask != GFX_ACCESS_ATTACHMENT_WRITE ||
			con->view.index >= rend->backing.attachs.size ||
			((_GFXAttach*)gfx_vec_at(
				&rend->backing.attachs,
				con->view.index))->type != _GFX_ATTACH_WINDOW)
		{
			continue;
		}

		// If it is, check if we already had a backing window.
		if (backing == SIZE_MAX)
			backing = con->view.index;
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
	_GFXContext* context = rend->allocator.context;
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

		_GFXCacheElem* elem =
			_gfx_cache_get(&rend->cache, &rpci.sType, NULL);

		if (elem == NULL) goto error;
		pass->vk.pass = elem->vk.pass;
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
	if (pass->vk.setLayout == VK_NULL_HANDLE)
	{
		VkDescriptorSetLayoutCreateInfo dslci = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,

			.pNext        = NULL,
			.flags        = 0,
			.bindingCount = 2,
			.pBindings = (VkDescriptorSetLayoutBinding[]){
				{
					.binding            = 0,
					.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount    = 1,
					.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT,
					.pImmutableSamplers = NULL
				}, {
					.binding            = 1,
					.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount    = 1,
					.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
					.pImmutableSamplers = NULL
				}
			}
		};

		_GFXCacheElem* elem =
			_gfx_cache_get(&rend->cache, &dslci.sType, NULL);

		if (elem == NULL) goto error;
		pass->build.setLayout = elem;
		pass->vk.setLayout = elem->vk.setLayout;
	}

	// Create sampler.
	if (pass->vk.sampler == VK_NULL_HANDLE)
	{
		VkSamplerCreateInfo sci = {
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,

			.pNext            = NULL,
			.flags            = 0,
			.magFilter        = VK_FILTER_NEAREST,
			.minFilter        = VK_FILTER_NEAREST,
			.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.mipLodBias       = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy    = 1.0f,
			.compareEnable    = VK_FALSE,
			.compareOp        = VK_COMPARE_OP_ALWAYS,
			.minLod           = 0.0f,
			.maxLod           = 1.0f,
			.borderColor      = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,

			.unnormalizedCoordinates = VK_FALSE
		};

		_GFXCacheElem* elem =
			_gfx_cache_get(&rend->cache, &sci.sType, NULL);

		if (elem == NULL) goto error;
		pass->vk.sampler = elem->vk.sampler;
	}

	// Create image view.
	// TODO: Somehow get this from renderables, cached?.
	if (pass->vk.view == VK_NULL_HANDLE)
	{
		// Use the second binding of the group as image lol.
		// TODO: Renderables should define what parts of a group to use.
		_GFXUnpackRef img = _gfx_ref_unpack(
			gfx_ref_group_image(&group->base, 1, 0));

		if (img.obj.image == NULL)
			goto error;

		VkImageViewCreateInfo ivci = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,

			.pNext    = NULL,
			.flags    = 0,
			.image    = img.obj.image->vk.image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format   = img.obj.image->vk.format,

			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY
			},

			.subresourceRange = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel   = 0,
				.levelCount     = 1,
				.baseArrayLayer = 0,
				.layerCount     = 1
			}
		};

		_GFX_VK_CHECK(context->vk.CreateImageView(
			context->vk.device, &ivci, NULL, &pass->vk.view), goto error);
	}

	// Create pipeline layout.
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

		_GFXCacheElem* elem = _gfx_cache_get(
			&rend->cache, &plci.sType, (const void*[]){
				// TODO: Should be the _GFXCacheElem*.
				&pass->vk.setLayout
			});

		if (elem == NULL) goto error;
		pass->vk.pipeLayout = elem->vk.layout;
	}

	// Create pipeline.
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
		VkVertexInputBindingDescription vibd[prim->numBindings];

		for (size_t i = 0; i < prim->numAttribs; ++i)
			viad[i] = (VkVertexInputAttributeDescription){
				.location = (uint32_t)i,
				.binding  = prim->attribs[i].binding,
				.format   = prim->attribs[i].vk.format,
				.offset   = prim->attribs[i].offset
			};

		for (size_t i = 0; i < prim->numBindings; ++i)
			vibd[i] = (VkVertexInputBindingDescription){
				.binding   = (uint32_t)i,
				.stride    = prim->bindings[i].stride,
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			};

		VkPipelineVertexInputStateCreateInfo pvisci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,

			.pNext                           = NULL,
			.flags                           = 0,
			.vertexAttributeDescriptionCount = (uint32_t)prim->numAttribs,
			.pVertexAttributeDescriptions    = viad,
			.vertexBindingDescriptionCount   = (uint32_t)prim->numBindings,
			.pVertexBindingDescriptions      = vibd
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
		VkPipelineViewportStateCreateInfo pvsci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,

			.pNext         = NULL,
			.flags         = 0,
			.viewportCount = 1,
			.pViewports    = NULL,
			.scissorCount  = 1,
			.pScissors     = NULL
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

		// Pipeline dynamic state.
		VkDynamicState dStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo pdsci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,

			.pNext             = NULL,
			.flags             = 0,
			.dynamicStateCount = 2,
			.pDynamicStates    = dStates
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
			.pDynamicState       = &pdsci,
			.layout              = pass->vk.pipeLayout,
			.renderPass          = pass->vk.pass,
			.subpass             = 0,
			.basePipelineHandle  = VK_NULL_HANDLE,
			.basePipelineIndex   = 0
		};

		_GFXCacheElem* elem = _gfx_cache_get(
			&rend->cache, &gpci.sType, (const void*[]){
				pass->build.vertex,
				pass->build.fragment,
				// TODO: Should be the _GFXCacheElem*.
				&pass->vk.pipeLayout,
				&pass->vk.pass
			});

		if (elem == NULL) goto error;
		pass->vk.pipeline = elem->vk.pipeline;
	}

	return 1;


	// Error on failure.
error:
	gfx_log_error("Could not allocate all resources of a pass.");

	return 0;
}

/****************************/
GFXPass* _gfx_create_pass(GFXRenderer* renderer,
                          size_t numParents, GFXPass** parents)
{
	assert(renderer != NULL);
	assert(numParents == 0 || parents != NULL);

	// Check if all parents use this renderer.
	for (size_t d = 0; d < numParents; ++d)
		if (parents[d]->renderer != renderer)
		{
			gfx_log_error(
				"Pass cannot be the parent of a pass associated "
				"with a different renderer.");

			return NULL;
		}

	// Allocate a new pass.
	GFXPass* pass = malloc(
		sizeof(GFXPass) +
		sizeof(GFXPass*) * numParents);

	if (pass == NULL)
		return NULL;

	// Initialize things.
	pass->renderer = renderer;
	pass->level = 0;
	pass->numParents = numParents;

	if (numParents) memcpy(
		pass->parents, parents, sizeof(GFXPass*) * numParents);

	// The level is the highest level of all parents + 1.
	for (size_t d = 0; d < numParents; ++d)
		if (parents[d]->level >= pass->level)
			pass->level = parents[d]->level + 1;

	// Initialize building stuff.
	pass->build.backing = SIZE_MAX;

	pass->vk.pass = VK_NULL_HANDLE;
	pass->vk.setLayout = VK_NULL_HANDLE;
	pass->vk.sampler = VK_NULL_HANDLE;
	pass->vk.view = VK_NULL_HANDLE;
	pass->vk.pipeLayout = VK_NULL_HANDLE;
	pass->vk.pipeline = VK_NULL_HANDLE;

	gfx_vec_init(&pass->vk.framebuffers, sizeof(VkFramebuffer));
	gfx_vec_init(&pass->consumes, sizeof(_GFXConsumeElem));


	// TODO: Super temporary!!
	_gfx_pool_sub(&renderer->pool, &pass->build.sub);
	pass->build.setLayout = NULL;
	pass->build.primitive = NULL;
	pass->build.group = NULL;
	pass->build.vertex = gfx_create_shader(GFX_STAGE_VERTEX, NULL);
	pass->build.fragment = gfx_create_shader(GFX_STAGE_FRAGMENT, NULL);

	GFXStringReader str;

	gfx_shader_compile(pass->build.vertex, GFX_GLSL, 1,
		gfx_string_reader(&str,
			"#version 450\n"
			"#extension GL_ARB_separate_shader_objects : enable\n"
			"layout(row_major, set = 0, binding = 0) uniform UBO {\n"
			"  mat4 mvp;\n"
			"};\n"
			"layout(location = 0) in vec3 position;\n"
			"layout(location = 1) in vec3 color;\n"
			"layout(location = 2) in vec2 texCoord;\n"
			"layout(location = 0) out vec3 fragColor;\n"
			"layout(location = 1) out vec2 fragTexCoord;\n"
			"out gl_PerVertex {\n"
			"  vec4 gl_Position;\n"
			"};\n"
			"void main() {\n"
			"  gl_Position = mvp * vec4(position, 1.0);\n"
			"  fragColor = color;\n"
			"  fragTexCoord = texCoord;\n"
			"}\n"),
		NULL, NULL);

	gfx_shader_compile(pass->build.fragment, GFX_GLSL, 1,
		gfx_string_reader(&str,
			"#version 450\n"
			"#extension GL_ARB_separate_shader_objects : enable\n"
			"layout(set = 0, binding = 1) uniform sampler2D texSampler;\n"
			"layout(location = 0) in vec3 fragColor;\n"
			"layout(location = 1) in vec2 fragTexCoord;\n"
			"layout(location = 0) out vec4 outColor;\n"
			"void main() {\n"
			"  float tex = texture(texSampler, fragTexCoord).r;\n"
			"  outColor = vec4(fragColor, 1.0) * tex;\n"
			"}\n"),
		NULL, NULL);


	return pass;
}

/****************************/
void _gfx_destroy_pass(GFXPass* pass)
{
	assert(pass != NULL);


	// TODO: Super temporary!!
	_gfx_pool_unsub(&pass->renderer->pool, &pass->build.sub);
	gfx_destroy_shader(pass->build.vertex);
	gfx_destroy_shader(pass->build.fragment);


	// Destroy Vulkan object structure.
	_gfx_pass_destruct(pass);

	// Clear all pre-building information.
	gfx_vec_clear(&pass->consumes);

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


	// Cleanup on failure.
clean:
	gfx_log_error("Could not (re)build a pass.");
	_gfx_pass_destruct(pass);

	return 0;
}

/****************************/
void _gfx_pass_destruct(GFXPass* pass)
{
	assert(pass != NULL);

	_GFXContext* context = pass->renderer->allocator.context;

	// Remove reference to backing window.
	pass->build.backing = SIZE_MAX;

	// Destruct all partial things first.
	_gfx_pass_destruct_partial(pass, _GFX_RECREATE_ALL);

	// Destroy all non-partial things too.
	context->vk.DestroyImageView(
		context->vk.device, pass->vk.view, NULL);

	pass->build.setLayout = NULL;
	pass->vk.setLayout = VK_NULL_HANDLE;
	pass->vk.sampler = VK_NULL_HANDLE;
	pass->vk.view = VK_NULL_HANDLE;
	pass->vk.pipeLayout = VK_NULL_HANDLE;

	// Clear memory.
	gfx_vec_clear(&pass->vk.framebuffers);
}

/****************************/
GFX_API int gfx_pass_consume(GFXPass* pass, size_t index,
                             GFXAccessMask mask, GFXShaderStage stage)
{
	// Just call gfx_pass_consumea with the entire resource.
	return gfx_pass_consumea(pass, index, mask, stage,
		(GFXRange){
			// Specify all aspect flags, will be filtered later on.
			.aspect = GFX_IMAGE_COLOR | GFX_IMAGE_DEPTH | GFX_IMAGE_STENCIL,
			.mipmap = 0,
			.numMipmaps = 0,
			.layer = 0,
			.numLayers = 0
		});
}

/****************************/
GFX_API int gfx_pass_consumea(GFXPass* pass, size_t index,
                              GFXAccessMask mask, GFXShaderStage stage,
                              GFXRange range)
{
	assert(pass != NULL);
	assert(pass->renderer->pFrame.vk.done == VK_NULL_HANDLE);

	_GFXConsumeElem elem = {
		.viewed = 0,
		.mask = mask,
		.stage = stage,
		.view = {
			.index = index,
			.range = range
		}
	};

	// Try to find it first.
	for (size_t i = 0; i < pass->consumes.size; ++i)
	{
		_GFXConsumeElem* con = gfx_vec_at(&pass->consumes, i);
		if (con->view.index == index)
		{
			*con = elem;
			return 1;
		}
	}

	// Insert anew.
	if (!gfx_vec_push(&pass->consumes, 1, &elem))
		return 0;

	// Changed a pass, the graph is invalidated.
	_gfx_render_graph_invalidate(pass->renderer);

	return 1;
}

/****************************/
GFX_API int gfx_pass_consumev(GFXPass* pass,
                              GFXAccessMask mask, GFXShaderStage stage,
                              GFXView view)
{
	assert(pass != NULL);
	assert(pass->renderer->pFrame.vk.done == VK_NULL_HANDLE);

	_GFXConsumeElem elem = {
		.viewed = 1,
		.mask = mask,
		.stage = stage,
		.view = view
	};

	// Try to find it first.
	for (size_t i = 0; i < pass->consumes.size; ++i)
	{
		_GFXConsumeElem* con = gfx_vec_at(&pass->consumes, i);
		if (con->view.index == view.index)
		{
			*con = elem;
			return 1;
		}
	}

	// Insert anew.
	if (!gfx_vec_push(&pass->consumes, 1, &elem))
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

	// FInd and erase.
	for (size_t i = pass->consumes.size; i > 0; --i)
		if (((_GFXConsumeElem*)gfx_vec_at(&pass->consumes, i))->view.index == index)
			gfx_vec_erase(&pass->consumes, 1, i-1);

	// Changed a pass, the graph is invalidated.
	_gfx_render_graph_invalidate(pass->renderer);
}

/****************************/
GFX_API size_t gfx_pass_get_num_parents(GFXPass* pass)
{
	assert(pass != NULL);

	return pass->numParents;
}

/****************************/
GFX_API GFXPass* gfx_pass_get_parent(GFXPass* pass, size_t parent)
{
	assert(pass != NULL);
	assert(parent < pass->numParents);

	return pass->parents[parent];
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
