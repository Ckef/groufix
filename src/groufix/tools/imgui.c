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
	"layout(set=0, binding=0) uniform sampler2D sTexture;\n"
	"layout(location = 0) in struct { vec4 Color; vec2 UV; } In;\n"
	"\n"
	"void main()\n"
	"{\n"
	"    fColor = In.Color * texture(sTexture, In.UV.st);\n"
	"}\n";


/****************************
 * ImGui drawer data element definition.
 */
typedef struct _GFXDataElem
{
	unsigned int frame; // Index of last frame that used this data.

	GFXPrimitive* prim;
	GFXRenderable rend;

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

	// Leave all values, result is invalidated.
}

/****************************/
GFX_API void gfx_cmd_draw_imgui(GFXRecorder* recorder,
                                GFXImguiDrawer* drawer, const void* igDrawData)
{
	assert(recorder != NULL);
	assert(drawer != NULL);
	assert(gfx_recorder_get_renderer(recorder) == drawer->renderer);
	assert(igDrawData != NULL);

	// TODO: Implement.
}
