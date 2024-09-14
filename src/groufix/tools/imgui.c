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
 * Vertex shader SPIR-V bytecode to use for ImGui.
 * Taken from the Dear ImGui Vulkan implementation.
 *
 * #version 450 core
 * layout(location = 0) in vec2 aPos;
 * layout(location = 1) in vec2 aUV;
 * layout(location = 2) in vec4 aColor;
 * layout(push_constant) uniform uPushConstant { vec2 uScale; vec2 uTranslate; } pc;
 *
 * out gl_PerVertex { vec4 gl_Position; };
 * layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;
 *
 * void main()
 * {
 *     Out.Color = aColor;
 *     Out.UV = aUV;
 *     gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0, 1);
 * }
 *
 */
static const uint32_t _gfx_imgui_vert_spv[] =
{
	0x07230203,0x00010000,0x00080001,0x0000002e,0x00000000,0x00020011,0x00000001,0x0006000b,
	0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
	0x000a000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x0000000b,0x0000000f,0x00000015,
	0x0000001b,0x0000001c,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
	0x00000000,0x00030005,0x00000009,0x00000000,0x00050006,0x00000009,0x00000000,0x6f6c6f43,
	0x00000072,0x00040006,0x00000009,0x00000001,0x00005655,0x00030005,0x0000000b,0x0074754f,
	0x00040005,0x0000000f,0x6c6f4361,0x0000726f,0x00030005,0x00000015,0x00565561,0x00060005,
	0x00000019,0x505f6c67,0x65567265,0x78657472,0x00000000,0x00060006,0x00000019,0x00000000,
	0x505f6c67,0x7469736f,0x006e6f69,0x00030005,0x0000001b,0x00000000,0x00040005,0x0000001c,
	0x736f5061,0x00000000,0x00060005,0x0000001e,0x73755075,0x6e6f4368,0x6e617473,0x00000074,
	0x00050006,0x0000001e,0x00000000,0x61635375,0x0000656c,0x00060006,0x0000001e,0x00000001,
	0x61725475,0x616c736e,0x00006574,0x00030005,0x00000020,0x00006370,0x00040047,0x0000000b,
	0x0000001e,0x00000000,0x00040047,0x0000000f,0x0000001e,0x00000002,0x00040047,0x00000015,
	0x0000001e,0x00000001,0x00050048,0x00000019,0x00000000,0x0000000b,0x00000000,0x00030047,
	0x00000019,0x00000002,0x00040047,0x0000001c,0x0000001e,0x00000000,0x00050048,0x0000001e,
	0x00000000,0x00000023,0x00000000,0x00050048,0x0000001e,0x00000001,0x00000023,0x00000008,
	0x00030047,0x0000001e,0x00000002,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,
	0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040017,
	0x00000008,0x00000006,0x00000002,0x0004001e,0x00000009,0x00000007,0x00000008,0x00040020,
	0x0000000a,0x00000003,0x00000009,0x0004003b,0x0000000a,0x0000000b,0x00000003,0x00040015,
	0x0000000c,0x00000020,0x00000001,0x0004002b,0x0000000c,0x0000000d,0x00000000,0x00040020,
	0x0000000e,0x00000001,0x00000007,0x0004003b,0x0000000e,0x0000000f,0x00000001,0x00040020,
	0x00000011,0x00000003,0x00000007,0x0004002b,0x0000000c,0x00000013,0x00000001,0x00040020,
	0x00000014,0x00000001,0x00000008,0x0004003b,0x00000014,0x00000015,0x00000001,0x00040020,
	0x00000017,0x00000003,0x00000008,0x0003001e,0x00000019,0x00000007,0x00040020,0x0000001a,
	0x00000003,0x00000019,0x0004003b,0x0000001a,0x0000001b,0x00000003,0x0004003b,0x00000014,
	0x0000001c,0x00000001,0x0004001e,0x0000001e,0x00000008,0x00000008,0x00040020,0x0000001f,
	0x00000009,0x0000001e,0x0004003b,0x0000001f,0x00000020,0x00000009,0x00040020,0x00000021,
	0x00000009,0x00000008,0x0004002b,0x00000006,0x00000028,0x00000000,0x0004002b,0x00000006,
	0x00000029,0x3f800000,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,
	0x00000005,0x0004003d,0x00000007,0x00000010,0x0000000f,0x00050041,0x00000011,0x00000012,
	0x0000000b,0x0000000d,0x0003003e,0x00000012,0x00000010,0x0004003d,0x00000008,0x00000016,
	0x00000015,0x00050041,0x00000017,0x00000018,0x0000000b,0x00000013,0x0003003e,0x00000018,
	0x00000016,0x0004003d,0x00000008,0x0000001d,0x0000001c,0x00050041,0x00000021,0x00000022,
	0x00000020,0x0000000d,0x0004003d,0x00000008,0x00000023,0x00000022,0x00050085,0x00000008,
	0x00000024,0x0000001d,0x00000023,0x00050041,0x00000021,0x00000025,0x00000020,0x00000013,
	0x0004003d,0x00000008,0x00000026,0x00000025,0x00050081,0x00000008,0x00000027,0x00000024,
	0x00000026,0x00050051,0x00000006,0x0000002a,0x00000027,0x00000000,0x00050051,0x00000006,
	0x0000002b,0x00000027,0x00000001,0x00070050,0x00000007,0x0000002c,0x0000002a,0x0000002b,
	0x00000028,0x00000029,0x00050041,0x00000011,0x0000002d,0x0000001b,0x0000000d,0x0003003e,
	0x0000002d,0x0000002c,0x000100fd,0x00010038
};


/****************************
 * Fragment shader SPIR-V bytecode to use for ImGui.
 * Taken from the Dear ImGui Vulkan implementation.
 *
 * #version 450 core
 * layout(location = 0) out vec4 fColor;
 * layout(set=0, binding=0) uniform sampler2D sTexture;
 * layout(location = 0) in struct { vec4 Color; vec2 UV; } In;
 *
 * void main()
 * {
 *     fColor = In.Color * texture(sTexture, In.UV.st);
 * }
 *
 */
static const uint32_t _gfx_imgui_frag_spv[] = {
	0x07230203,0x00010000,0x00080001,0x0000001e,0x00000000,0x00020011,0x00000001,0x0006000b,
	0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
	0x0007000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000d,0x00030010,
	0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
	0x00000000,0x00040005,0x00000009,0x6c6f4366,0x0000726f,0x00030005,0x0000000b,0x00000000,
	0x00050006,0x0000000b,0x00000000,0x6f6c6f43,0x00000072,0x00040006,0x0000000b,0x00000001,
	0x00005655,0x00030005,0x0000000d,0x00006e49,0x00050005,0x00000016,0x78655473,0x65727574,
	0x00000000,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,0x0000000d,0x0000001e,
	0x00000000,0x00040047,0x00000016,0x00000022,0x00000000,0x00040047,0x00000016,0x00000021,
	0x00000000,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,
	0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040020,0x00000008,0x00000003,
	0x00000007,0x0004003b,0x00000008,0x00000009,0x00000003,0x00040017,0x0000000a,0x00000006,
	0x00000002,0x0004001e,0x0000000b,0x00000007,0x0000000a,0x00040020,0x0000000c,0x00000001,
	0x0000000b,0x0004003b,0x0000000c,0x0000000d,0x00000001,0x00040015,0x0000000e,0x00000020,
	0x00000001,0x0004002b,0x0000000e,0x0000000f,0x00000000,0x00040020,0x00000010,0x00000001,
	0x00000007,0x00090019,0x00000013,0x00000006,0x00000001,0x00000000,0x00000000,0x00000000,
	0x00000001,0x00000000,0x0003001b,0x00000014,0x00000013,0x00040020,0x00000015,0x00000000,
	0x00000014,0x0004003b,0x00000015,0x00000016,0x00000000,0x0004002b,0x0000000e,0x00000018,
	0x00000001,0x00040020,0x00000019,0x00000001,0x0000000a,0x00050036,0x00000002,0x00000004,
	0x00000000,0x00000003,0x000200f8,0x00000005,0x00050041,0x00000010,0x00000011,0x0000000d,
	0x0000000f,0x0004003d,0x00000007,0x00000012,0x00000011,0x0004003d,0x00000014,0x00000017,
	0x00000016,0x00050041,0x00000019,0x0000001a,0x0000000d,0x00000018,0x0004003d,0x0000000a,
	0x0000001b,0x0000001a,0x00050057,0x00000007,0x0000001c,0x00000017,0x0000001b,0x00050085,
	0x00000007,0x0000001d,0x00000012,0x0000001c,0x0003003e,0x00000009,0x0000001d,0x000100fd,
	0x00010038
};


/****************************
 * 64 bits integer hasing implementation as GFXMap hash function,
 * taken from Wolfgang Brehm at https://stackoverflow.com/q/664014,
 * key is of type GFXImage**.
 */
static uint64_t _gfx_imgui_hash(const void* key)
{
	uint64_t n = (uint64_t)(uintptr_t)(*(const GFXImage**)key);
	n = (n ^ (n >> 32)) * 0x5555555555555555ull; // Alternating 0s and 1s.
	n = (n ^ (n >> 32)) * 17316035218449499591ull; // Random uneven integer.

	return n;
}

/****************************
 * GFXMap key comparison function,
 * l and r are of type GFXImage**.
 */
static int _gfx_imgui_cmp(const void* l, const void* r)
{
	// Non-zero = inequal.
	return *(const GFXImage**)l != *(const GFXImage**)r;
}

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
	gfx_vec_init(&drawer->fonts, sizeof(GFXImage*));
	gfx_map_init(&drawer->images, sizeof(GFXSet*), _gfx_imgui_hash, _gfx_imgui_cmp);

	// Create shaders.
	GFXShader* shads[] = {
		gfx_create_shader(GFX_STAGE_VERTEX, dev),
		gfx_create_shader(GFX_STAGE_FRAGMENT, dev)
	};

	if (shads[0] == NULL || shads[1] == NULL)
		goto clean;

	// Compile GLSL into shaders.
	GFXBinReader bin;

	if (!gfx_shader_load(shads[0], gfx_bin_reader(
		&bin, sizeof(_gfx_imgui_vert_spv), _gfx_imgui_vert_spv)))
	{
		goto clean;
	}

	if (!gfx_shader_load(shads[1], gfx_bin_reader(
		&bin, sizeof(_gfx_imgui_frag_spv), _gfx_imgui_frag_spv)))
	{
		goto clean;
	}

	drawer->shaders.vert = shads[0];
	drawer->shaders.frag = shads[1];

	// Create a technique.
	GFXTechnique* tech =
		gfx_renderer_add_tech(renderer, 2, shads);
	if (tech == NULL)
		goto clean;

	GFXSampler sampler = {
		.binding = 0,
		.index = 0,

		.flags = GFX_SAMPLER_NONE,
		.mode = GFX_FILTER_MODE_AVERAGE,

		.minFilter = GFX_FILTER_LINEAR,
		.magFilter = GFX_FILTER_LINEAR,
		.mipFilter = GFX_FILTER_LINEAR,

		.wrapU = GFX_WRAP_REPEAT,
		.wrapV = GFX_WRAP_REPEAT,
		.wrapW = GFX_WRAP_REPEAT,

		.mipLodBias = 0.0f,
		.minLod = -1000.0f,
		.maxLod = +1000.0f
	};

	if (!gfx_tech_samplers(tech, 0, 1, &sampler))
		goto clean_tech;

	if (!gfx_tech_lock(tech))
		goto clean_tech;

	drawer->tech = tech;

	// Setup default render state.
	drawer->raster = (GFXRasterState){
		.mode = GFX_RASTER_FILL,
		.front = GFX_FRONT_FACE_CW,
		.cull = GFX_CULL_NONE,
		.topo = GFX_TOPO_TRIANGLE_LIST,
		.samples = 1,
	};

	GFXBlendOpState color = {
		.srcFactor = GFX_FACTOR_SRC_ALPHA,
		.dstFactor = GFX_FACTOR_ONE_MINUS_SRC_ALPHA,
		.op = GFX_BLEND_ADD
	};

	GFXBlendOpState alpha = {
		.srcFactor = GFX_FACTOR_ONE,
		.dstFactor = GFX_FACTOR_ZERO,
		.op = GFX_BLEND_ADD
	};

	drawer->blend = (GFXBlendState){
		.logic = GFX_LOGIC_NO_OP,
		.color = color,
		.alpha = alpha,
		.constants = { 0.0f, 0.0f, 0.0f, 0.0f },
	};

	drawer->state = (GFXRenderState){
		.raster = &drawer->raster,
		.blend = &drawer->blend,
		.depth = NULL,
		.stencil = NULL
	};

	return 1;


	// Cleanup on failure.
clean_tech:
	gfx_erase_tech(tech);
clean:
	gfx_destroy_shader(shads[0]);
	gfx_destroy_shader(shads[1]);

	gfx_deque_clear(&drawer->data);
	gfx_vec_clear(&drawer->fonts);
	gfx_map_clear(&drawer->images);

	gfx_log_error("Could not initialize a new ImGui drawer.");

	return 0;
}

/****************************/
GFX_API void gfx_imgui_clear(GFXImguiDrawer* drawer)
{
	assert(drawer != NULL);

	// Erase all sets for used images.
	for (
		GFXSet** elem = gfx_map_first(&drawer->images);
		elem != NULL;
		elem = gfx_map_next(&drawer->images, elem))
	{
		gfx_erase_set(*elem);
	}

	gfx_map_clear(&drawer->images);

	// Free all allocated font images.
	for (size_t f = 0; f < drawer->fonts.size; ++f)
		gfx_free_image(*(GFXImage**)gfx_vec_at(&drawer->fonts, f));

	gfx_vec_clear(&drawer->fonts);

	// Free all uploaded vertex/index data.
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

/****************************/
GFX_API void* gfx_imgui_font(GFXImguiDrawer* drawer, void* igFontAtlas)
{
	assert(drawer != NULL);
	assert(igFontAtlas != NULL);

	ImFontAtlas* fontAtlas = igFontAtlas;

	// Get texture data from the font atlas.
	unsigned char* pixels;
	int width;
	int height;
	ImFontAtlas_GetTexDataAsRGBA32(fontAtlas, &pixels, &width, &height, NULL);

	// Allocate image.
	GFXImage* image = gfx_alloc_image(drawer->heap,
		GFX_IMAGE_2D, GFX_MEMORY_WRITE,
		GFX_IMAGE_SAMPLED | GFX_IMAGE_SAMPLED_LINEAR,
		GFX_FORMAT_R8G8B8A8_UNORM,
		1, 1, (uint32_t)width, (uint32_t)height, 1);

	if (image == NULL)
		goto error;

	// Write data.
	const GFXRegion srcRegion = {
		.offset = 0,
		.rowSize = 0,
		.numRows = 0
	};

	const GFXRegion dstRegion = {
		.aspect = GFX_IMAGE_COLOR,
		.mipmap = 0,
		.layer = 0,
		.numLayers = 1,
		.x = 0,
		.y = 0,
		.z = 0,
		.width = (uint32_t)width,
		.height = (uint32_t)height,
		.depth = 1
	};

	const GFXInject inject =
		gfx_dep_sig(drawer->dep, GFX_ACCESS_SAMPLED_READ, GFX_STAGE_FRAGMENT);

	if (!gfx_write(pixels, gfx_ref_image(image),
		GFX_TRANSFER_ASYNC,
		1, 1, &srcRegion, &dstRegion, &inject))
	{
		goto clean;
	}

	// Add the image to the drawer.
	if (!gfx_vec_push(&drawer->fonts, 1, &image))
		goto clean;

	// Then build an ImTextureID.
	void* texId = gfx_imgui_image(drawer, image);

	if (texId == NULL)
		goto clean_fonts;

	// And set it at the font atlas.
	ImFontAtlas_SetTexID(fontAtlas, texId);

	return texId;


	// Cleanup on failure.
clean_fonts:
	gfx_vec_pop(&drawer->fonts, 1);
clean:
	gfx_free_image(image);
error:
	gfx_log_error("Failed to allocate an image for an ImFontAtlas.");

	return NULL;

}

/****************************/
GFX_API void* gfx_imgui_image(GFXImguiDrawer* drawer, GFXImage* image)
{
	assert(drawer != NULL);
	assert(image != NULL);

	const uint64_t hash = drawer->images.hash(&image);

	// Check if we already know the image.
	GFXSet** elem = gfx_map_hsearch(&drawer->images, &image, hash);
	if (elem != NULL)
		return *elem;

	// Add a new set for this image.
	GFXSet* set = gfx_renderer_add_set(drawer->renderer,
		drawer->tech, 0,
		1, 0, 0, 0,
		(GFXSetResource[]){{
			.binding = 0,
			.index = 0,
			.ref = gfx_ref_image(image)
		}},
		NULL, NULL, NULL);

	if (set == NULL)
		goto error;

	// And add the new set to the drawer.
	elem = gfx_map_hinsert(
		&drawer->images, &set, sizeof(GFXImage*), &image, hash);

	if (elem == NULL)
		goto clean;

	return *elem;


	// Cleanup on failure.
clean:
	gfx_erase_set(set);
error:
	gfx_log_error("Failed to build an ImTextureID for an image");

	return NULL;
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

	if (elem->primitive == NULL)
		goto clean_new;

	// If successful, map the data.
	elem->vertices = gfx_map(gfx_ref_prim_vertices(elem->primitive, 0));
	elem->indices = gfx_map(gfx_ref_prim_indices(elem->primitive));

	if (elem->vertices == NULL || elem->indices == NULL)
		goto clean_new;

	// And lastly, initalize the renderable.
	if (!gfx_renderable(&elem->renderable,
		drawer->pass, drawer->tech, elem->primitive, &drawer->state))
	{
		goto clean_new;
	}

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
	static_assert(
		sizeof(uint16_t) == sizeof(ImDrawIdx),
		"sizeof(ImDrawIdx) must equal sizeof(uint16_t).");

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
	// And keep track of the currently bound set to reduce bind calls.
	GFXViewport oldViewport = gfx_recorder_get_viewport(recorder);
	GFXScissor oldScissor = gfx_recorder_get_scissor(recorder);
	GFXSet* currentSet = NULL;

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

			// Bind the set given as texture ID.
			GFXSet* set = drawCmd->TextureId;
			if (currentSet != set)
			{
				gfx_cmd_bind(recorder, drawer->tech, 0, 1, 0, &set, NULL);
				currentSet = set;
			}

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
