/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/tools/imgui.h"
#include "groufix/core/log.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"


// Clears the contents of a _GFXDataElem, freeing all memory.
#define _GFX_IMGUI_CLEAR_DATA(elem) \
	do { \
		if (elem->vertices) \
			gfx_unmap(gfx_ref_prim_vertices(elem->primitive, 0)); \
		if (elem->indices) \
			gfx_unmap(gfx_ref_prim_indices(elem->primitive)); \
		gfx_free_prim(elem->primitive); \
	} while (0);


/****************************
 * ImGui drawer data element definition.
 * One such element holds data for all of the renderer's virtual frames!
 */
typedef struct _GFXDataElem
{
	unsigned int frame; // Index of last frame that used this data.

	GFXPrimitive* primitive;
	GFXRenderable renderable;

	void* vertices;
	void* indices;

} _GFXDataElem;


/****************************
 * Vertex shader string to use for ImGui.
 * Taken from the Dear ImGui Vulkan implementation.
 */
static const char* _gfx_imgui_vert_str =
	"#version 450 core\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec4 aColor;\n"
	"layout(push_constant) uniform uPushConstant { vec2 uScale; vec2 uTranslate; } pc;\n"
	"\n"
	"out gl_PerVertex { vec4 gl_Position; };\n"
	"layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;\n"
	"\n"
	"void main()\n"
	"{\n"
	"    Out.Color = aColor;\n"
	"    Out.UV = aUV;\n"
	"    gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0, 1);\n"
	"}\n";


/****************************
 * Fragment shader string to use for ImGui.
 * Taken from the Dear ImGui Vulkan implementation.
 */
static const char* _gfx_imgui_frag_str =
	"#version 450 core\n"
	"layout(location = 0) out vec4 fColor;\n"
	//"layout(set=0, binding=0) uniform sampler2D sTexture;\n"
	"layout(location = 0) in struct { vec4 Color; vec2 UV; } In;\n"
	"\n"
	"void main()\n"
	"{\n"
	//"    fColor = In.Color * texture(sTexture, In.UV.st);\n"
	"    fColor = In.Color;\n"
	"}\n";


/****************************/
GFX_API bool gfx_imgui_init(GFXImguiDrawer* drawer,
                            GFXHeap* heap, GFXDependency* dep,
                            GFXRenderer* renderer, GFXPass* pass)
{
	assert(drawer != NULL);
	assert(dep != NULL);
	assert(renderer != NULL);
	assert(pass != NULL);
	assert(gfx_pass_get_renderer(pass) == renderer);
	assert(gfx_pass_get_type(pass) == GFX_PASS_RENDER);

	GFXDevice* dev = gfx_renderer_get_device(renderer);

	// User the renderer's heap if none is given.
	if (heap == NULL)
		heap = gfx_renderer_get_heap(renderer);

	drawer->heap = heap;
	drawer->dep = dep;
	drawer->renderer = renderer;
	drawer->pass = pass;

	gfx_deque_init(&drawer->data, sizeof(_GFXDataElem));
	// TODO: Define hash and compare functions.
	//gfx_map_init(&drawer->images, sizeof(GFXSet*), _some_hash, _some_cmp);

	// Create shaders.
	GFXShader* shads[] = {
		gfx_create_shader(GFX_STAGE_VERTEX, dev),
		gfx_create_shader(GFX_STAGE_FRAGMENT, dev)
	};

	if (shads[0] == NULL || shads[1] == NULL)
		goto clean;

	// Compile GLSL into shaders.
	GFXStringReader str;

	if (!gfx_shader_compile(shads[0], GFX_GLSL, 1,
		gfx_string_reader(&str, _gfx_imgui_vert_str), NULL, NULL, NULL))
	{
		goto clean;
	}

	if (!gfx_shader_compile(shads[1], GFX_GLSL, 1,
		gfx_string_reader(&str, _gfx_imgui_frag_str), NULL, NULL, NULL))
	{
		goto clean;
	}

	drawer->shaders.vert = shads[0];
	drawer->shaders.frag = shads[1];

	// Create a technique.
	GFXTechnique* tech = gfx_renderer_add_tech(renderer, 2, shads);
	if (tech == NULL)
		goto clean;

	if (!gfx_tech_immutable(tech, 0, 0))
		goto clean_tech;

	if (!gfx_tech_lock(tech))
		goto clean_tech;

	drawer->tech = tech;

	return 1;


	// Cleanup on failure.
clean_tech:
	gfx_erase_tech(tech);
clean:
	gfx_destroy_shader(shads[0]);
	gfx_destroy_shader(shads[1]);
	gfx_deque_clear(&drawer->data);

	gfx_log_error("Could not initialize a new ImGui drawer.");

	return 0;
}

/****************************/
GFX_API void gfx_imgui_clear(GFXImguiDrawer* drawer)
{
	assert(drawer != NULL);

	// Erase/free all dynamic data.
	// TODO: Erase all sets.
	//gfx_map_clear(&drawer->images);

	for (size_t d = 0; d < drawer->data.size; ++d)
	{
		_GFXDataElem* elem = gfx_deque_at(&drawer->data, d);
		_GFX_IMGUI_CLEAR_DATA(elem);
	}

	gfx_deque_clear(&drawer->data);

	// Destroy the rest.
	gfx_erase_tech(drawer->tech);
	gfx_destroy_shader(drawer->shaders.vert);
	gfx_destroy_shader(drawer->shaders.frag);

	// Leave all values, drawer is invalidated.
}

/****************************
 * Purges stale data and makes sure the first element of drawer->data
 * is sufficiently large to hold a given number of vertices and indices.
 * @return Zero on failure.
 */
static bool _gfx_imgui_update_data(GFXImguiDrawer* drawer,
                                   unsigned int numFrames, unsigned int frame,
                                   uint32_t vertices, uint32_t indices)
{
	static_assert(
		sizeof(uint16_t) == sizeof(ImDrawIdx),
		"sizeof(ImDrawIdx) must equal sizeof(uint16_t).");

	assert(drawer != NULL);

	// First purge all data that was last used by this frame.
	// Given frames always come in order, all previous frames should have
	// been destroyed, unless the user skips frames for ImGui...
	// In which case it will just take longer to purge.
	while (drawer->data.size > 0)
	{
		_GFXDataElem* elem =
			gfx_deque_at(&drawer->data, drawer->data.size - 1);
		if (elem->frame != frame)
			break;

		_GFX_IMGUI_CLEAR_DATA(elem);
		gfx_deque_pop(&drawer->data, 1);
	}

	// If there is a front-most element, check if it is sufficiently large.
	if (drawer->data.size > 0)
	{
		_GFXDataElem* elem = gfx_deque_at(&drawer->data, 0);
		if (elem->frame != UINT_MAX) // Already marked for purging.
			goto build_new;

		uint32_t numVertices = elem->primitive->numVertices / numFrames;
		uint32_t numIndices = elem->primitive->numIndices / numFrames;

		// Too small!
		if (numVertices < vertices || numIndices < indices)
		{
			// Mark for purging and build new.
			// Get the last submitted frame's index,
			// as clearly this frame won't be using it :)
			elem->frame = (frame + numFrames - 1) % numFrames;

			goto build_new;
		}

		// Ok, evidently the front-most elemt has enough space, done!
		return 1;
	}

	// Jump here to build a new front-most data element.
build_new:
	if (!gfx_deque_push_front(&drawer->data, 1, NULL))
		return 0;

	_GFXDataElem* elem = gfx_deque_at(&drawer->data, 0);
	elem->frame = UINT_MAX; // Not yet purged.
	elem->vertices = NULL;
	elem->indices = NULL;

	// Allocate primitive.
	elem->primitive = gfx_alloc_prim(drawer->heap,
		GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_DEVICE_LOCAL,
		0, GFX_TOPO_TRIANGLE_LIST,
		indices * numFrames, sizeof(uint16_t),
		vertices * numFrames,
		GFX_REF_NULL,
		3, (GFXAttribute[]){
			{
				.format = GFX_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(ImDrawVert, pos),
				.stride = sizeof(ImDrawVert),
				.buffer = GFX_REF_NULL,
			}, {
				.format = GFX_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(ImDrawVert, uv),
				.stride = sizeof(ImDrawVert),
				.buffer = GFX_REF_NULL,
			}, {
				.format = GFX_FORMAT_R8G8B8A8_UNORM,
				.offset = offsetof(ImDrawVert, col),
				.stride = sizeof(ImDrawVert),
				.buffer = GFX_REF_NULL,
			}
		});

	// If successful, initialize renderable.
	if (
		!elem->primitive ||
		!gfx_renderable(&elem->renderable,
			drawer->pass, drawer->tech, elem->primitive, NULL))
	{
		goto clean_new;
	}

	// Lastly, map the data.
	elem->vertices = gfx_map(gfx_ref_prim_vertices(elem->primitive, 0));
	elem->indices = gfx_map(gfx_ref_prim_indices(elem->primitive));

	if (elem->vertices == NULL || elem->indices == NULL)
		goto clean_new;

	return 1;


	// Cleanup on failure (of build new).
clean_new:
	_GFX_IMGUI_CLEAR_DATA(elem);
	gfx_deque_pop_front(&drawer->data, 1);

	return 0;
}

/****************************
 * Sets up basic rendering state to render ImGui data with.
 * @param recorder   Cannot be NULL.
 * @param drawer     Cannot be NULL.
 * @param igDrawData Cannot be NULL.
 */
static void _gfx_cmd_imgui_state(GFXRecorder* recorder,
                                 GFXImguiDrawer* drawer, const void* igDrawData)
{
	assert(recorder != NULL);
	assert(drawer != NULL);
	assert(igDrawData != NULL);

	const ImDrawData* drawData = igDrawData;

	// Setup viewport.
	GFXViewport viewport = {
		.size = GFX_SIZE_RELATIVE,
		.xOffset = 0.0f,
		.yOffset = 0.0f,
		.xScale = 1.0f,
		.yScale = 1.0f,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	gfx_cmd_set_viewport(recorder, viewport);

	// Setup push constants.
	float scale [] = {
		2.0f / drawData->DisplaySize.x,
		2.0f / drawData->DisplaySize.y
	};

	float translate[] = {
		-1.0f - drawData->DisplayPos.x * scale[0],
		-1.0f - drawData->DisplayPos.y * scale[1]
	};

	gfx_cmd_push(recorder, drawer->tech,
		0, sizeof(scale), scale);
	gfx_cmd_push(recorder, drawer->tech,
		sizeof(scale), sizeof(translate), translate);
}

/****************************/
GFX_API void gfx_cmd_draw_imgui(GFXRecorder* recorder,
                                GFXImguiDrawer* drawer, const void* igDrawData)
{
	assert(recorder != NULL);
	assert(drawer != NULL);
	assert(gfx_recorder_get_pass(recorder) == drawer->pass);
	assert(igDrawData != NULL);

	const ImDrawData* drawData = igDrawData;

	const unsigned int numFrames =
		gfx_renderer_get_num_frames(drawer->renderer);
	const unsigned int frame =
		gfx_recorder_get_frame_index(recorder);

	// Do nothing when minimized.
	if (drawData->DisplaySize.x <= 0 || drawData->DisplaySize.y <= 0)
		return;

	// Make sure all vertex/index data is ready for the GPU.
	_GFXDataElem* elem = NULL;
	uint32_t vertexOffset = 0;
	uint32_t indexOffset = 0;

	if (drawData->TotalVtxCount > 0 && drawData->TotalIdxCount > 0)
	{
		// Try to update the data held by this drawer.
		if (!_gfx_imgui_update_data(drawer,
			numFrames, frame,
			(uint32_t)drawData->TotalVtxCount,
			(uint32_t)drawData->TotalIdxCount))
		{
			gfx_log_error(
				"Could not allocate buffers during ImGui draw command; "
				"command not recorded.");

			return;
		}

		// Now we are sure we have data, set the vertex/index offsets
		// for once we start drawing, as all draws will only use a part
		// of the primitive (it holds data for all virtual frames!)
		// We start the offsets according to the current frame index.
		elem = gfx_deque_at(&drawer->data, 0);
		vertexOffset = frame * (elem->primitive->numVertices / numFrames);
		indexOffset = frame * (elem->primitive->numIndices / numFrames);

		// Upload all the vertex/index data.
		ImDrawVert* vertices = (ImDrawVert*)elem->vertices + vertexOffset;
		ImDrawIdx* indices = (ImDrawIdx*)elem->indices + indexOffset;

		for (int l = 0; l < drawData->CmdListsCount; ++l)
		{
			const ImDrawList* drawList = drawData->CmdLists.Data[l];

			memcpy(vertices,
				drawList->VtxBuffer.Data,
				sizeof(ImDrawVert) * (size_t)drawList->VtxBuffer.Size);

			memcpy(indices,
				drawList->IdxBuffer.Data,
				sizeof(ImDrawIdx) * (size_t)drawList->IdxBuffer.Size);

			vertices += drawList->VtxBuffer.Size;
			indices += drawList->IdxBuffer.Size;
		}
	}

	// Setup some basic recording state.
	// Remember current viewport/scissor state so we can reset it afterwards.
	GFXViewport oldViewport = gfx_recorder_get_viewport(recorder);
	GFXScissor oldScissor = gfx_recorder_get_scissor(recorder);

	_gfx_cmd_imgui_state(recorder, drawer, igDrawData);

	// Loop over all draw commands and draw them.
	for (int l = 0; l < drawData->CmdListsCount; ++l)
	{
		const ImDrawList* drawList = drawData->CmdLists.Data[l];
		for (int c = 0; c < drawList->CmdBuffer.Size; ++c)
		{
			const ImDrawCmd* drawCmd = &drawList->CmdBuffer.Data[c];

			// Handle user callbacks.
			if (drawCmd->UserCallback != NULL)
			{
				if (drawCmd->UserCallback == ImDrawCallback_ResetRenderState)
					_gfx_cmd_imgui_state(recorder, drawer, igDrawData);
				else
					drawCmd->UserCallback(drawList, drawCmd);

				continue;
			}

			// Should not happen, but safety catch.
			if (elem == NULL)
				continue;

			// Convert clipping rectangle to scissor state.
			float clipMinX = drawCmd->ClipRect.x - drawData->DisplayPos.x;
			float clipMinY = drawCmd->ClipRect.y - drawData->DisplayPos.y;
			float clipMaxX = drawCmd->ClipRect.z - drawData->DisplayPos.x;
			float clipMaxY = drawCmd->ClipRect.w - drawData->DisplayPos.y;

			if (clipMinX < 0.0f) clipMinX = 0.0f;
			if (clipMinY < 0.0f) clipMinY = 0.0f;
			if (clipMaxX > drawData->DisplaySize.x) clipMaxX = drawData->DisplaySize.x;
			if (clipMaxY > drawData->DisplaySize.y) clipMaxY = drawData->DisplaySize.y;

			if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
				continue;

			GFXScissor scissor = {
				.size = GFX_SIZE_RELATIVE,
				.xOffset = clipMinX / drawData->DisplaySize.x,
				.yOffset = clipMinY / drawData->DisplaySize.y,
				.xScale = (clipMaxX - clipMinX) / drawData->DisplaySize.x,
				.yScale = (clipMaxY - clipMinY) / drawData->DisplaySize.y
			};

			gfx_cmd_set_scissor(recorder, scissor);

			// Record the draw command.
			gfx_cmd_draw_indexed(recorder, &elem->renderable,
				(uint32_t)drawCmd->ElemCount, 1,
				(uint32_t)drawCmd->IdxOffset + indexOffset,
				(int32_t)((uint32_t)drawCmd->VtxOffset + vertexOffset), 0);
		}

		vertexOffset += (uint32_t)drawList->VtxBuffer.Size;
		indexOffset += (uint32_t)drawList->IdxBuffer.Size;
	}

	// Reset viewport & scissor state.
	gfx_cmd_set_viewport(recorder, oldViewport);
	gfx_cmd_set_scissor(recorder, oldScissor);
}
