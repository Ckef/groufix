/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_DRAWERS_IMGUI_H
#define GFX_DRAWERS_IMGUI_H

#include "groufix/containers/deque.h"
#include "groufix/containers/map.h"
#include "groufix/containers/vec.h"
#include "groufix/core/deps.h"
#include "groufix/core/heap.h"
#include "groufix/core/keys.h"
#include "groufix/core/renderer.h"
#include "groufix/core/shader.h"
#include "groufix/core/window.h"
#include "groufix/def.h"


/**
 * Dear ImGui drawer definition.
 */
typedef struct GFXImguiDrawer
{
	GFXHeap*     heap;
	GFXRenderer* renderer;
	GFXPass*     pass;

	GFXTechnique* tech;
	GFXDeque      data;   // Stores { unsigned int, GFXPrimitive*, GFXRenderable, void* }.
	GFXVec        fonts;  // Stores GFXImage*.
	GFXMap        images; // Stores GFXImage* : GFXSet*.

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
 * @param renderer Renderer to build for, cannot be NULL.
 * @param pass     Render pass to build for, cannot be NULL, must be of renderer.
 * @param heap     Heap to allocate from, NULL to use the heap from renderer.
 * @return Non-zero on success.
 */
GFX_API bool gfx_imgui_init(GFXImguiDrawer* drawer,
                            GFXRenderer* renderer, GFXPass* pass, GFXHeap* heap);

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
 * @param dep         Dependency to inject signal commands in, cannot be NULL.
 * @param igFontAtlas The ImFontAtlas* to allocate an image for, cannot be NULL.
 * @return A valid ImTextureID, NULL on failure.
 *
 * The returned ID is invalidated when this drawer is cleared.
 */
GFX_API void* gfx_imgui_font(GFXImguiDrawer* drawer,
                             GFXDependency* dep, void* igFontAtlas);

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


/****************************
 * Input/Output helpers.
 ****************************/

/**
 * Dear ImGui input forwarder definition.
 */
typedef struct GFXImguiInput
{
	GFXWindow* window;
	void*      data;

} GFXImguiInput;


/**
 * Converts a GFXKey to a ImGuiKey.
 */
GFX_API int gfx_imgui_key(GFXKey key);

/**
 * Converts a GFXMouseButton to a ImGui button.
 */
GFX_API int gfx_imgui_button(GFXMouseButton button);

/**
 * Initializes an ImGui input forwarder.
 * Does not need to be cleared, hence no _init postfix.
 * @param input    Cannot be NULL.
 * @param window   Cannot be NULL.
 * @param igGuiIO  The ImGuiIO* to feed input into, cannot be NULL.
 * @param blocking If true, will block the event stack based on ImGui output.
 * @return Non-zero on success.
 */
GFX_API bool gfx_imgui_input(GFXImguiInput* input,
                             GFXWindow* window, void* igGuiIO, bool blocking);

/**
 * Stops (ends) an ImGui input forwarder, invalidating it.
 * Can only be called once for each call to gfx_imgui_input!
 * @param input Cannot be NULL.
 *
 * This call is optional; when the associated window is destroyed,
 * the forwarder is implicitly invalidated.
 * However, if the associated ImGuiIO* is destroyed before that, this function
 * needs to be called first!
 */
GFX_API void gfx_imgui_end(GFXImguiInput* input);


#endif
