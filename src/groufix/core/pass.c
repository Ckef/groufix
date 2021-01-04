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

	// Initialize pre-building stuff.
	pass->vk.pass = VK_NULL_HANDLE;
	pass->vk.framebuffer = VK_NULL_HANDLE;

	gfx_vec_init(&pass->reads, sizeof(size_t));
	gfx_vec_init(&pass->writes, sizeof(size_t));

	return pass;
}

/****************************/
void _gfx_destroy_render_pass(GFXRenderPass* pass)
{
	assert(pass != NULL);

	_GFXContext* context = pass->renderer->context;

	// Destroy Vulkan object structure.
	context->vk.DestroyFramebuffer(
		context->vk.device, pass->vk.framebuffer, NULL);
	context->vk.DestroyRenderPass(
		context->vk.device, pass->vk.pass, NULL);

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

	// Destroy old object structure.
	context->vk.DestroyFramebuffer(
		context->vk.device, pass->vk.framebuffer, NULL);
	context->vk.DestroyRenderPass(
		context->vk.device, pass->vk.pass, NULL);

	// TODO: Obviously expand, for now 1 color attachment, the first window.
	// TODO: This does not log anything, as it's super temporary.
	// Pick a window from the renderer attachments.
	if (pass->writes.size == 0)
		goto error;

	size_t index = *(size_t*)gfx_vec_at(&pass->writes, 0);
	_GFXWindowAttach* attach = NULL;

	for (size_t i = 0; i < rend->windows.size; ++i)
	{
		_GFXWindowAttach* at = gfx_vec_at(&rend->windows, i);
		if (at->index == index)
		{
			attach = at;
			break;
		}
	}

	if (attach == NULL)
		goto error;

	// Ok we have all data.
	// Go build a new render pass.
	VkAttachmentDescription ad = {
		.flags          = 0,
		.format         = attach->window->frame.format,
		.samples        = VK_SAMPLE_COUNT_1_BIT,
		.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
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

	return 1;


	// Error on failure.
error:
	gfx_log_error("Could not build a render pass.");

	pass->vk.pass = VK_NULL_HANDLE;
	pass->vk.framebuffer = VK_NULL_HANDLE;

	return 0;
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

	return gfx_vec_push(&pass->reads, 1, &index);
}

/****************************/
GFX_API int gfx_render_pass_write(GFXRenderPass* pass, size_t index)
{
	assert(pass != NULL);

	// Try to find it first.
	for (size_t i = 0; i < pass->writes.size; ++i)
		if (*(size_t*)gfx_vec_at(&pass->writes, i) == index)
			return 1;

	return gfx_vec_push(&pass->writes, 1, &index);
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
