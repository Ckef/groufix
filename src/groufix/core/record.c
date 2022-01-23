/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


/****************************/
void _gfx_pass_record(GFXPass* pass, GFXFrame* frame)
{
	assert(pass != NULL);
	assert(frame != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->allocator.context;
	_GFXPrimitive* prim = pass->build.primitive;

	// Can't be recording if resources are missing.
	// Window could be minimized or smth.
	if (
		pass->vk.pass == VK_NULL_HANDLE ||
		pass->vk.framebuffers.size == 0 ||
		pass->vk.set == VK_NULL_HANDLE ||
		pass->vk.pipeLayout == VK_NULL_HANDLE ||
		pass->vk.pipeline == VK_NULL_HANDLE)
	{
		return;
	}

	// TODO: Future: if no backing window, do smth else.
	if (pass->build.backing == SIZE_MAX)
		return;

	// Query the synchronization object associated with this
	// swapchain as backing. This should only be queried once!
	// Once we have the sync object, we know the swapchain image index
	// and can select the framebuffer to use for recording.
	_GFXFrameSync* sync = gfx_vec_at(
		&frame->syncs,
		*(size_t*)gfx_vec_at(&frame->refs, pass->build.backing));

	VkFramebuffer framebuffer =
		*(VkFramebuffer*)gfx_vec_at(&pass->vk.framebuffers, sync->image);

	// Gather all necessary render pass info to record.
	// This assumes the buffer is already in the recording state!
	// TODO: Define public GFXRenderArea with a GFXSizeClass?
	VkViewport viewport = {
		.x        = 0.0f,
		.y        = 0.0f,
		.width    = (float)sync->window->frame.width,
		.height   = (float)sync->window->frame.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = {
			sync->window->frame.width,
			sync->window->frame.height
		}
	};

	VkClearValue clear = {
		.color = {{ 0.0f, 0.0f, 0.0f, 0.0f }}
	};

	VkRenderPassBeginInfo rpbi = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,

		.pNext           = NULL,
		.renderPass      = pass->vk.pass,
		.framebuffer     = framebuffer,
		.clearValueCount = 1,
		.pClearValues    = &clear,
		.renderArea      = scissor
	};

	// Set viewport & scissor.
	context->vk.CmdSetViewport(frame->vk.cmd, 0, 1, &viewport);
	context->vk.CmdSetScissor(frame->vk.cmd, 0, 1, &scissor);

	// Begin render pass, bind pipeline & descriptor sets.
	context->vk.CmdBeginRenderPass(
		frame->vk.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

	context->vk.CmdBindPipeline(
		frame->vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass->vk.pipeline);

	context->vk.CmdBindDescriptorSets(
		frame->vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pass->vk.pipeLayout, 0, 1, &pass->vk.set, 0, NULL);

	// Bind index buffer.
	if (prim->base.numIndices > 0)
	{
		_GFXUnpackRef index = _gfx_ref_unpack(
			gfx_ref_prim_indices(&prim->base, 0));

		context->vk.CmdBindIndexBuffer(
			frame->vk.cmd,
			index.obj.buffer->vk.buffer,
			index.value,
			prim->base.indexSize == sizeof(uint16_t) ?
				VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
	}

	// Bind vertex buffers.
	VkBuffer vertexBuffs[prim->numBindings];
	VkDeviceSize vertexOffsets[prim->numBindings];

	for (size_t i = 0; i < prim->numBindings; ++i)
		vertexBuffs[i] = prim->bindings[i].buffer->vk.buffer,
		vertexOffsets[i] = prim->bindings[i].offset;

	context->vk.CmdBindVertexBuffers(
		frame->vk.cmd,
		0, (uint32_t)prim->numBindings,
		vertexBuffs, vertexOffsets);

	// Draw.
	// TODO: Renderable objects should define what range of the primitive to draw.
	// Relevant when some simple primitives share a simple attribute layout.
	if (prim->base.numIndices > 0)
		context->vk.CmdDrawIndexed(
			frame->vk.cmd,
			prim->base.numIndices,
			1, 0, 0, 0);
	else
		context->vk.CmdDraw(
			frame->vk.cmd,
			prim->base.numVertices,
			1, 0, 0);

	// End render pass.
	context->vk.CmdEndRenderPass(frame->vk.cmd);
}
