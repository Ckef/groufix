/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_TOOLS_IMGUI_H
#define GFX_TOOLS_IMGUI_H

#include "groufix/containers/map.h"
#include "groufix/containers/vec.h"
#include "groufix/core/deps.h"
#include "groufix/core/heap.h"
#include "groufix/core/renderer.h"
#include "groufix/core/shader.h"
#include "groufix/def.h"


/**
 * Dear ImGui drawer definition.
 */
typedef struct GFXImguiDrawer
{
	GFXHeap*       heap;
	GFXDependency* dep;
	GFXRenderer*   renderer;
	GFXPass*       pass;

	GFXTechnique* tech;
	GFXVec        data;   // Stores { unsigned int, GFXPrimitive*, GFXRenderable }
	GFXMap        images; // Stores GFXImage* : GFXSet*

	// All shaders.
	struct
	{
		GFXShader* vert;
		GFXShader* frag;

	} shaders;

} GFXImguiDrawer;


/**
 * Initializes an ImGui drawer.
 * @param drawer   Cannot be NULL.
 * @param heap     Heap to allocate from, NULL to use the heap from renderer.
 * @param dep      Dependency to inject signal commands in, cannot be NULL.
 * @param renderer Renderer to build for, cannot be NULL.
 * @param pass     Render pass to build for, cannot be NULL, must be of renderer.
 * @return Non-zero on success.
 */
GFX_API bool gfx_imgui_init(GFXImguiDrawer* drawer,
                            GFXHeap* heap, GFXDependency* dep,
                            GFXRenderer* renderer, GFXPass* pass);

/**
 * Clears an ImGui drawer, invalidating the contents of `drawer`.
 * @param drawer Cannot be NULL.
 */
GFX_API void gfx_imgui_clear(GFXImguiDrawer* drawer);

/**
 * Render command to draw ImGUI data using a drawer.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder   Cannot be NULL.
 * @param drawer     Cannot be NULL.
 * @param igDrawData The ImDrawData* to draw, cannot be NULL.
 */
GFX_API void gfx_cmd_draw_imgui(GFXRecorder* recorder,
                                GFXImguiDrawer* drawer, const void* igDrawData);


#endif
