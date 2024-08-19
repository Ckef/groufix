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

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"


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


/****************************
 * ImGui drawer data element definition.
 */
typedef struct _GFXDataElem
{
	unsigned int frame; // Index of last frame that used this data.

	GFXPrimitive* primitive;
	GFXRenderable renderable;

} _GFXDataElem;


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

	gfx_vec_init(&drawer->data, sizeof(_GFXDataElem));
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

	// TODO: Implement further.

	return 1;


	// Cleanup on failure.
clean_tech:
	gfx_erase_tech(tech);
clean:
	gfx_destroy_shader(shads[0]);
	gfx_destroy_shader(shads[1]);

	gfx_log_error("Could not initialize a new ImGui drawer.");

	return 0;
}

/****************************/
GFX_API void gfx_imgui_clear(GFXImguiDrawer* drawer)
{
	assert(drawer != NULL);

	// TODO: Implement further.

	//gfx_map_clear(&drawer->images);
	gfx_vec_clear(&drawer->data);

	gfx_erase_tech(drawer->tech);
	gfx_destroy_shader(drawer->shaders.vert);
	gfx_destroy_shader(drawer->shaders.frag);

	// Leave all values, drawer is invalidated.
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

	// Do nothing when minimized.
	if (drawData->DisplaySize.x <= 0 || drawData->DisplaySize.y <= 0)
		return;

	// Get all the vertex/index data, stick it in a buffer.
	if (drawData->TotalVtxCount > 0)
	{
		for (int l = 0; l < drawData->CmdListsCount; ++l)
		{
			const ImDrawList* drawList = drawData->CmdLists.Data[l];

			// TODO: Upload drawList->(Vtx|Idx)Buffer.Data.
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

			// TODO: Draw.
		}
	}

	// Reset viewport & scissor state.
	gfx_cmd_set_viewport(recorder, oldViewport);
	gfx_cmd_set_scissor(recorder, oldScissor);
}
