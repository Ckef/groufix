/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_TOOLS_IMGUI_H
#define GFX_TOOLS_IMGUI_H

#include "groufix/containers/deque.h"
#include "groufix/containers/map.h"
#include "groufix/containers/vec.h"
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
	GFXDeque      data;   // Stores { unsigned int, GFXPrimitive*, GFXRenderable, void*, void* }
	GFXVec        fonts;  // Stores GFXImage*
	GFXMap        images; // Stores GFXImage* : GFXSet*

	GFXRasterState raster;
	GFXBlendState  blend;
	GFXRenderState state;


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
 *
 * Cannot be called until all frames that used this drawer are done rendering!
 */
GFX_API void gfx_imgui_clear(GFXImguiDrawer* drawer);

/**
 * Allocates an image for an ImFontAtlas and sets its ImTextureID.
 * @param drawer      Cannot be NULL.
 * @param igFontAtlas The ImFontAtlas* to allocate an image for, cannot be NULL.
 * @return A valid ImTextureID, NULL on failure.
 *
 * The returned ID is invalidated when this drawer is cleared.
 */
GFX_API void* gfx_imgui_font(GFXImguiDrawer* drawer, void* igFontAtlas);

/**
 * Builds an ImTextureID from a GFXImage*.
 * @param drawer Cannot be NULL.
 * @param image  Cannot be NULL.
 * @return A valid ImTextureID, NULL on failure.
 *
 * The returned ID is invalidated when this drawer is cleared.
 */
GFX_API void* gfx_imgui_image(GFXImguiDrawer* drawer, GFXImage* image);

/**
 * Render command to draw ImGui data using a drawer.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder   Cannot be NULL, must use the pass given in gfx_imgui_init!
 * @param drawer     Cannot be NULL.
 * @param igDrawData The ImDrawData* to draw, cannot be NULL.
 */
GFX_API void gfx_cmd_draw_imgui(GFXRecorder* recorder,
                                GFXImguiDrawer* drawer, const void* igDrawData);


#endif
