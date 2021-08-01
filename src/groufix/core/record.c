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
void _gfx_render_pass_record(GFXRenderPass* pass, _GFXFrame* frame)
{
	assert(pass != NULL);
	assert(frame != NULL);

	GFXRenderer* rend = pass->renderer;
	_GFXContext* context = rend->context;
	_GFXMesh* mesh = pass->build.mesh;

	// Can't be recording if resources are missing.
	// Window could be minimized or smth.
	if (
		pass->vk.pass == VK_NULL_HANDLE ||
		pass->vk.framebuffers.size == 0 ||
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
		.renderArea      = {
			.offset = { 0, 0 },
			.extent = {
				(uint32_t)sync->window->frame.width,
				(uint32_t)sync->window->frame.height
			}
		}
	};

	// Begin render pass, bind pipeline.
	context->vk.CmdBeginRenderPass(
		frame->vk.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

	context->vk.CmdBindPipeline(
		frame->vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass->vk.pipeline);

	// Bind index buffer.
	if (mesh->base.sizeIndices > 0)
	{
		_GFXUnpackRef index = _gfx_ref_unpack(
			gfx_ref_mesh_indices((GFXMesh*)mesh, 0));

		context->vk.CmdBindIndexBuffer(
			frame->vk.cmd,
			index.obj.buffer->vk.buffer,
			index.value,
			mesh->indexSize == sizeof(uint16_t) ?
				VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
	}

	// Bind vertex buffer.
	_GFXUnpackRef vertex = _gfx_ref_unpack(
		gfx_ref_mesh_vertices((GFXMesh*)mesh, 0));

	context->vk.CmdBindVertexBuffers(
		frame->vk.cmd, 0, 1,
		(VkBuffer[]){ vertex.obj.buffer->vk.buffer },
		(VkDeviceSize[]){ vertex.value });

	// Draw.
	if (mesh->base.sizeIndices > 0)
		context->vk.CmdDrawIndexed(
			frame->vk.cmd,
			(uint32_t)(mesh->base.sizeIndices / mesh->indexSize),
			1, 0, 0, 0);
	else
		context->vk.CmdDraw(
			frame->vk.cmd,
			(uint32_t)(mesh->base.sizeVertices / mesh->stride),
			1, 0, 0);

	// End render pass.
	context->vk.CmdEndRenderPass(frame->vk.cmd);
}
