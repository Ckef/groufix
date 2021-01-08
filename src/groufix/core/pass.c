/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/****************************
 * Destructs the Vulkan object structure, non-recursively.
 * Only destructs swapchain-dependent stuff, suitable for e.g. resizing.
 * Call _gfx_render_pass_destruct (without partial) to destruct _everything_.
 * @param pass Cannot be NULL.
 */
void _gfx_render_pass_destruct_partial(GFXRenderPass* pass)
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

	// Destroy the render pass.
	context->vk.DestroyRenderPass(
		context->vk.device, pass->vk.pass, NULL);

	pass->vk.pass = VK_NULL_HANDLE;
	gfx_vec_clear(&pass->vk.framebuffers);
}

/****************************
 * Picks a window to use as back-buffer and (re)builds appropriate resources.
 * @param pass Cannot be NULL.
 * @return Non-zero if successful (zero if multiple windows found).
 */
static int _gfx_render_pass_rebuild_backing(GFXRenderPass* pass)
{
	assert(pass != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->context;

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

	// Nothing errored, so we can safely ignore the previous backing value.
	// We cannot have a different back-buffer window from the current:
	// - If a window detached or another attached, the renderer destructed.
	// - If we added another reference to a window, we error above.
	// - And lastly we cannot remove a current reference.
	// So no leakage :)
	// But we cannot set this value any earlier, if we error above there can
	// still be resources that we did not free yet, don't tamper with those...
	pass->build.backing = backing;

	// None found, don't error as this isn't necessarily an error.
	// Could just be a pass that does not write to a window.
	if (pass->build.backing == SIZE_MAX)
		return 1;

	// TODO: In the future, allocate different command buffers?
	// Ok so we chose a backing window.
	// Now we allocate more command buffers or free some.
	// We assume destruction already happened if necessary, so all existing
	// buffers must come from the same command pool.
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
	// We keep the picked backing though, as we might still want to free
	// those command buffers :)
	gfx_log_error(
		"Could not allocate resources for a window attachment a render pass "
		"writes to.");

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
		context->vk.device, &rpci, NULL, &pass->vk.pass), goto clean);

	// TODO: Create framebuffers.

	// Now go record all of the command buffers.
	// We simply clear the entire associated image to a single color.
	// Obviously for testing purposes :)
	VkClearColorValue clear = {
		{ 1.0f, 0.8f, 0.4f, 0.0f }
	};

	VkImageSubresourceRange range = {
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel   = 0,
		.levelCount     = 1,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};

	VkCommandBufferBeginInfo cbbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,

		.pNext            = NULL,
		.flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
		.pInheritanceInfo = NULL
	};

	for (size_t i = 0; i < pass->vk.commands.size; ++i)
	{
		// TODO: Using render passes we'd use the VkImageView, not VkImage.
		VkImage image =
			*(VkImage*)gfx_vec_at(&attach->window->frame.images, i);
		VkCommandBuffer buffer =
			*(VkCommandBuffer*)gfx_vec_at(&pass->vk.commands, i);

		// Define memory barriers.
		VkImageMemoryBarrier imb_clear = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT,
			.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = image,
			.subresourceRange    = range
		};

		VkImageMemoryBarrier imb_present = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

			.pNext               = NULL,
			.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT,
			.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = image,
			.subresourceRange    = range
		};

		// Start of all commands.
		_GFX_VK_CHECK(context->vk.BeginCommandBuffer(buffer, &cbbi),
			goto clean);

		// Switch to transfer layout, clear, switch back to present layout.
		context->vk.CmdPipelineBarrier(
			buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, 1, &imb_clear);

		context->vk.CmdClearColorImage(
			buffer,
			image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			&clear,
			1, &range);

		context->vk.CmdPipelineBarrier(
			buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, NULL, 0, NULL, 1, &imb_present);

		// End of all commands.
		_GFX_VK_CHECK(context->vk.EndCommandBuffer(buffer),
			goto clean);
	}

	return 1;


	// Clean on failure.
clean:
	gfx_log_error("Could not build a render pass.");
	_gfx_render_pass_destruct(pass);

	return 0;
}

/****************************/
void _gfx_render_pass_destruct(GFXRenderPass* pass)
{
	assert(pass != NULL);

	_GFXContext* context = pass->renderer->context;

	// Destruct things we'd also destroy during e.g. a resize.
	_gfx_render_pass_destruct_partial(pass);

	// If we use a window as back-buffer, destroy those resources.
	if (pass->build.backing != SIZE_MAX)
	{
		// Because it is required to call this before detaching any window
		// attachments, pass->build.backing must still be valid.
		_GFXWindowAttach* attach =
			gfx_vec_at(&pass->renderer->windows, pass->build.backing);

		// Destroy all command buffers.
		if (pass->vk.commands.size > 0)
			context->vk.FreeCommandBuffers(
				context->vk.device,
				attach->vk.pool,
				(uint32_t)pass->vk.commands.size,
				pass->vk.commands.data);

		pass->build.backing = SIZE_MAX;
		gfx_vec_clear(&pass->vk.commands);
	}
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
