/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_RENDERER_H
#define GFX_CORE_RENDERER_H

#include "groufix/core/deps.h"
#include "groufix/core/device.h"
#include "groufix/core/formats.h"
#include "groufix/core/heap.h"
#include "groufix/core/refs.h"
#include "groufix/core/shader.h"
#include "groufix/core/window.h"
#include "groufix/def.h"


/**
 * Size class of a resource.
 */
typedef enum GFXSizeClass
{
	GFX_SIZE_ABSOLUTE,
	GFX_SIZE_RELATIVE

} GFXSizeClass;


/**
 * Attachment description.
 */
typedef struct GFXAttachment
{
	GFXImageType   type;
	GFXMemoryFlags flags;
	GFXImageUsage  usage;

	GFXFormat format;
	uint32_t  mipmaps;
	uint32_t  layers;

	// Optionally dynamic size.
	GFXSizeClass size;
	size_t       ref; // Index of the attachment the size is relative to.

	union {
		uint32_t width;
		float xScale;
	};

	union {
		uint32_t height;
		float yScale;
	};

	union {
		uint32_t depth;
		float zScale;
	};

} GFXAttachment;


/**
 * Attachment clear value.
 */
typedef union GFXClear
{
	float    f[4];
	int32_t  i32[4];
	uint32_t u32[4];

	GFX_UNION_ANONYMOUS(
	{
		float    depth;
		uint32_t stencil;

	}, test)

} GFXClear;


/**
 * Image view type (interpreted dimensionality).
 */
typedef enum GFXViewType
{
	GFX_VIEW_1D,
	GFX_VIEW_1D_ARRAY,
	GFX_VIEW_2D,
	GFX_VIEW_2D_ARRAY,
	GFX_VIEW_CUBE,
	GFX_VIEW_CUBE_ARRAY,
	GFX_VIEW_3D

} GFXViewType;


/**
 * Sampler parameter flags.
 */
typedef enum GFXSamplerFlags
{
	GFX_SAMPLER_NONE         = 0x0000,
	GFX_SAMPLER_ANISOTROPY   = 0x0001,
	GFX_SAMPLER_COMPARE      = 0x0002,
	GFX_SAMPLER_UNNORMALIZED = 0x0004

} GFXSamplerFlags;

GFX_BIT_FIELD(GFXSamplerFlags)


/**
 * Primitive face culling mode.
 */
typedef enum GFXCullMode
{
	GFX_CULL_NONE  = 0x0000,
	GFX_CULL_FRONT = 0x0001,
	GFX_CULL_BACK  = 0x0002

} GFXCullMode;

GFX_BIT_FIELD(GFXCullMode)


/**
 * Depth paramater flags.
 */
typedef enum GFXDepthFlags
{
	GFX_DEPTH_NONE    = 0x0000,
	GFX_DEPTH_WRITE   = 0x0001,
	GFX_DEPTH_BOUNDED = 0x0002

} GFXDepthFlags;

GFX_BIT_FIELD(GFXDepthFlags)


/**
 * Polygon rasterization mode.
 */
typedef enum GFXRasterMode
{
	GFX_RASTER_DISCARD,
	GFX_RASTER_POINT,
	GFX_RASTER_LINE,
	GFX_RASTER_FILL

} GFXRasterMode;


/**
 * Front face direction.
 */
typedef enum GFXFrontFace
{
	GFX_FRONT_FACE_CCW,
	GFX_FRONT_FACE_CW

} GFXFrontFace;


/**
 * Texture lookup filtering.
 */
typedef enum GFXFilter
{
	GFX_FILTER_NEAREST,
	GFX_FILTER_LINEAR

} GFXFilter;


/**
 * Texture lookup filter ('reduction') mode.
 */
typedef enum GFXFilterMode
{
	GFX_FILTER_MODE_AVERAGE,
	GFX_FILTER_MODE_MIN,
	GFX_FILTER_MODE_MAX

} GFXFilterMode;


/**
 * Texture lookup wrapping.
 */
typedef enum GFXWrapping
{
	GFX_WRAP_REPEAT,
	GFX_WRAP_REPEAT_MIRROR,
	GFX_WRAP_CLAMP_TO_EDGE,
	GFX_WRAP_CLAMP_TO_EDGE_MIRROR,
	GFX_WRAP_CLAMP_TO_BORDER

} GFXWrapping;


/**
 * Logical 'blending' operation.
 */
typedef enum GFXLogicOp
{
	GFX_LOGIC_NO_OP, // Keep fragment color.
	GFX_LOGIC_CLEAR, // All 0.
	GFX_LOGIC_SET,   // All 1.
	GFX_LOGIC_KEEP,  // Keep attachment color.
	GFX_LOGIC_KEEP_INVERSE,
	GFX_LOGIC_INVERSE,
	GFX_LOGIC_AND,
	GFX_LOGIC_AND_INVERSE, // !fragment ^ attachment.
	GFX_LOGIC_AND_REVERSE, // fragment ^ !attachment.
	GFX_LOGIC_NAND,
	GFX_LOGIC_OR,
	GFX_LOGIC_OR_INVERSE,
	GFX_LOGIC_OR_REVERSE,
	GFX_LOGIC_XOR,
	GFX_LOGIC_NOR,
	GFX_LOGIC_EQUAL

} GFXLogicOp;


/**
 * Blending operation.
 */
typedef enum GFXBlendOp
{
	GFX_BLEND_NO_OP,
	GFX_BLEND_ADD,
	GFX_BLEND_SUBTRACT,         // source - attachment.
	GFX_BLEND_SUBTRACT_REVERSE, // attachment - fragment.
	GFX_BLEND_MIN,
	GFX_BLEND_MAX

} GFXBlendOp;


/**
 * Blending factor.
 */
typedef enum GFXBlendFactor
{
	GFX_FACTOR_ZERO,
	GFX_FACTOR_ONE,
	GFX_FACTOR_SRC,
	GFX_FACTOR_ONE_MINUS_SRC,
	GFX_FACTOR_DST,
	GFX_FACTOR_ONE_MINUS_DST,
	GFX_FACTOR_SRC_ALPHA,
	GFX_FACTOR_SRC_ALPHA_SATURATE,
	GFX_FACTOR_ONE_MINUS_SRC_ALPHA,
	GFX_FACTOR_DST_ALPHA,
	GFX_FACTOR_ONE_MINUS_DST_ALPHA,
	GFX_FACTOR_CONSTANT,
	GFX_FACTOR_ONE_MINUS_CONSTANT,
	GFX_FACTOR_CONSTANT_ALPHA,
	GFX_FACTOR_ONE_MINUS_CONSTANT_ALPHA

} GFXBlendFactor;


/**
 * Depth/stencil comparison operation.
 */
typedef enum GFXCompareOp
{
	GFX_CMP_NEVER,
	GFX_CMP_LESS,
	GFX_CMP_LESS_EQUAL,
	GFX_CMP_GREATER,
	GFX_CMP_GREATER_EQUAL,
	GFX_CMP_EQUAL,
	GFX_CMP_NOT_EQUAL,
	GFX_CMP_ALWAYS

} GFXCompareOp;


/**
 * Stencil operation.
 */
typedef enum GFXStencilOp
{
	GFX_STENCIL_KEEP,
	GFX_STENCIL_ZERO,
	GFX_STENCIL_REPLACE,
	GFX_STENCIL_INVERT,
	GFX_STENCIL_INCR_CLAMP,
	GFX_STENCIL_INCR_WRAP,
	GFX_STENCIL_DECR_CLAMP,
	GFX_STENCIL_DECR_WRAP

} GFXStencilOp;


/**
 * Resource view description.
 */
typedef struct GFXView
{
	// Both ignored for pass consumptions.
	size_t binding;
	size_t index; // Binding array index.

	union {
		GFXFormat   format; // For texel buffers.
		GFXViewType type;   // For attachments.
	};

	GFXRange range;

} GFXView;


/**
 * Sampler description.
 */
typedef struct GFXSampler
{
	size_t binding;
	size_t index; // Binding array index.

	GFXSamplerFlags flags;
	GFXFilterMode   mode;

	GFXFilter minFilter;
	GFXFilter magFilter;
	GFXFilter mipFilter;

	GFXWrapping wrapU;
	GFXWrapping wrapV;
	GFXWrapping wrapW;

	float mipLodBias;
	float minLod;
	float maxLod;
	float maxAnisotropy;

	GFXCompareOp cmp;

} GFXSampler;


/**
 * Rasterization state description.
 */
typedef struct GFXRasterState
{
	GFXRasterMode mode;
	GFXFrontFace  front;
	GFXCullMode   cull;
	GFXTopology   topo; // Topology when no primitive is given.

} GFXRasterState;


/**
 * Blending operation state.
 */
typedef struct GFXBlendOpState
{
	GFXBlendFactor srcFactor;
	GFXBlendFactor dstFactor;
	GFXBlendOp     op;

} GFXBlendOpState;


/**
 * Blending state description.
 */
typedef struct GFXBlendState
{
	GFXLogicOp      logic;
	GFXBlendOpState color;
	GFXBlendOpState alpha;

	float constants[4]; // { RGBA } blending constants.

} GFXBlendState;


/**
 * Depth state description.
 */
typedef struct GFXDepthState
{
	GFXDepthFlags flags;
	GFXCompareOp  cmp;

	float minDepth;
	float maxDepth;

} GFXDepthState;


/**
 * Stencil operation state.
 */
typedef struct GFXStencilOpState
{
	GFXStencilOp fail;
	GFXStencilOp pass;
	GFXStencilOp depthFail;
	GFXCompareOp cmp;

	uint32_t cmpMask;
	uint32_t writeMask;
	uint32_t reference;

} GFXStencilOpState;


/**
 * Stencil state description.
 */
typedef struct GFXStencilState
{
	GFXStencilOpState front; // Front-facing polygons.
	GFXStencilOpState back;  // Back-facing polygons.

} GFXStencilState;


/**
 * Render state description.
 */
typedef struct GFXRenderState
{
	// All are optional.
	const GFXRasterState*  raster;
	const GFXBlendState*   blend;
	const GFXDepthState*   depth;
	const GFXStencilState* stencil;

} GFXRenderState;


/**
 * Renderer definition.
 */
typedef struct GFXRenderer GFXRenderer;


/**
 * Pass (i.e. render/compute pass) definition.
 */
typedef struct GFXPass GFXPass;


/**
 * Virtual frame definition.
 */
typedef struct GFXFrame GFXFrame;


/**
 * Technique (i.e. shader pipeline) definition.
 */
typedef struct GFXTechnique GFXTechnique;


/**
 * Set (i.e. render/descriptor set) definition.
 */
typedef struct GFXSet GFXSet;


/**
 * Recorder definition.
 */
typedef struct GFXRecorder GFXRecorder;


/****************************
 * Primitive renderable/computable.
 ****************************/

/**
 * Renderable definition.
 */
typedef struct GFXRenderable
{
	// All ready-only.
	GFXPass*      pass;
	GFXTechnique* technique;
	GFXPrimitive* primitive;

	const GFXRenderState* state;

	GFX_ATOMIC(bool) lock;

	uintptr_t pipeline;
	uintmax_t gen;

} GFXRenderable;


/**
 * Computable definition.
 */
typedef struct GFXComputable
{
	// All read-only.
	GFXTechnique* technique;

	GFX_ATOMIC(uintptr_t) pipeline;

} GFXComputable;


/**
 * Initializes a renderable.
 * The object pointed to by renderable _CAN_ be moved or copied!
 * Any member of state may be NULL to omit setting the associated state.
 * @param renderable Cannot be NULL.
 * @param pass       Cannot be NULL.
 * @param tech       Cannot be NULL.
 * @param prim       May be NULL!
 * @param state      May be NULL, `blend.color` and `blend.alpha` are ignored.
 * @return Non-zero on success.
 *
 * Can be called from any thread at any time!
 * Does not need to be cleared, hence no _init postfix.
 *
 * The object(s) pointed to by state cannot be moved or copied and must
 * remain constant as long as the renderable is being used in function calls!
 * To update state, call this function again.
 */
GFX_API bool gfx_renderable(GFXRenderable* renderable,
                            GFXPass* pass, GFXTechnique* tech, GFXPrimitive* prim,
                            const GFXRenderState* state);

/**
 * Warms up the internal pipeline cache (technique must be locked).
 * @param renderable Cannot be NULL.
 * @return Non-zero on success.
 *
 * This function is reentrant.
 * However, NOT thread-safe with respect to the associated pass/renderer and
 * CANNOT be called during or inbetween gfx_frame_start and gfx_frame_submit.
 *
 * gfx_renderable only:
 *  This call will internally pre-built a portion of the associated pass.
 *  If the render graph (any pass!) is changed after this call, the warmup data
 *  is invalidated and this call is effectively rendered a waste.
 */
GFX_API bool gfx_renderable_warmup(GFXRenderable* renderable);

/**
 * Initializes a computable.
 * The object pointed to by computable _CAN_ be moved or copied!
 * @param computable Cannot be NULL.
 * @see gfx_renderable.
 */
GFX_API bool gfx_computable(GFXComputable* computable,
                            GFXTechnique* tech);

/**
 * Warms up the internal pipeline cache (technique must be locked).
 * @param computable Cannot be NULL.
 * @see gfx_renderable_warmup.
 */
GFX_API bool gfx_computable_warmup(GFXComputable* computable);


/****************************
 * Renderer handling.
 ****************************/

/**
 * Creates a renderer.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @param frames Number of virtual frames, must be > 0 (preferably > 1).
 * @return NULL on failure.
 */
GFX_API GFXRenderer* gfx_create_renderer(GFXDevice* device, unsigned int frames);

/**
 * Destroys a renderer.
 * This will forcefully submit and block until rendering is done!
 */
GFX_API void gfx_destroy_renderer(GFXRenderer* renderer);

/**
 * Returns the device the renderer was created for.
 * Can be called from any thread.
 */
GFX_API GFXDevice* gfx_renderer_get_device(GFXRenderer* renderer);

/**
 * Retrieves the number of virtual frames of a renderer.
 * @param renderer Cannot be NULL.
 */
GFX_API unsigned int gfx_renderer_get_num_frames(GFXRenderer* renderer);

/**
 * Loads groufix pipeline cache data, merging it into the current cache.
 * @param renderer Cannot be NULL.
 * @param src      Source stream, cannot be NULL.
 * @return Zero on failure.
 *
 * Cannot run concurrently with _ANY_ function of the renderer's descendants!
 */
GFX_API bool gfx_renderer_load_cache(GFXRenderer* renderer,
                                     const GFXReader* src);

/**
 * Stores the current groufix pipeline cache data.
 * @param renderer Cannot be NULL.
 * @param dst      Destination stream, cannot be NULL.
 * @return Zero on failure.
 *
 * Cannot run concurrently with _ANY_ function of the renderer's descendants!
 */
GFX_API bool gfx_renderer_store_cache(GFXRenderer* renderer,
                                      const GFXWriter* dst);

/**
 * Describes the properties of an image attachment of a renderer.
 * If the attachment already exists, it will be detached and overwritten.
 * @param renderer Cannot be NULL.
 * @param index    Attachment index.
 * @return Zero on failure.
 *
 * The GFX_MEMORY_HOST_VISIBLE flag is ignored, images cannot be mapped!
 * If anything needs to be detached, this will block until rendering is done!
 */
GFX_API bool gfx_renderer_attach(GFXRenderer* renderer,
                                 size_t index, GFXAttachment attachment);

/**
 * Attaches a window to an attachment index of a renderer.
 * If the attachment already exists, it will be detached and overwritten.
 * @param renderer Cannot be NULL.
 * @param index    Attachment index.
 * @param window   Cannot be NULL.
 * @return Zero on failure.
 *
 * Thread-safe with respect to window.
 * If anything needs to be detached, this will block until rendering is done!
 * Fails if the window was already attached to a renderer or the window and
 * renderer do not share a compatible device.
 */
GFX_API bool gfx_renderer_attach_window(GFXRenderer* renderer,
                                        size_t index, GFXWindow* window);

/**
 * Retrieves the properties of an image attachment of a renderer.
 * @param renderer Cannot be NULL.
 * @param index    Must be < largest attachment index of renderer.
 * @return Empty attachment if none attached.
 *
 * An empty attachment has 0'd out values and
 * undefined `type`, `flags`, `usage` and `ref` fields.
 */
GFX_API GFXAttachment gfx_renderer_get_attach(GFXRenderer* renderer, size_t index);

/**
 * Retrieves a window at an attachment index of a renderer.
 * @param renderer Cannot be NULL.
 * @param index    Must be < largest attachment index of renderer.
 * @return NULL if no window is attached.
 */
GFX_API GFXWindow* gfx_renderer_get_window(GFXRenderer* renderer, size_t index);

/**
 * Detaches an attachment at a given index of a renderer.
 * Undescribed if not a window, detached if a window.
 * @param renderer Cannot be NULL.
 * @param index    Must be < largest attachment index of renderer.
 *
 * If anything is detached, this will block until rendering is done!
 */
GFX_API void gfx_renderer_detach(GFXRenderer* renderer, size_t index);


/****************************
 * Pass handling.
 ****************************/

/**
 * Adds a new (sink) pass to the renderer given a set of parent.
 * A pass will be after all its parents in submission order.
 * Each element in parents must be associated with the same renderer.
 * @param renderer   Cannot be NULL.
 * @param numParents Number of parents, 0 for none.
 * @param parents    Parent passes, cannot be NULL if numParents > 0.
 * @return NULL on failure.
 */
GFX_API GFXPass* gfx_renderer_add_pass(GFXRenderer* renderer,
                                       size_t numParents, GFXPass** parents);

/**
 * Retrieves the number of sink passes of a renderer.
 * A sink pass is one that is not a parent of any pass (last in the path).
 * @param renderer Cannot be NULL.
 *
 * This number may change when a new pass is added.
 */
GFX_API size_t gfx_renderer_get_num_sinks(GFXRenderer* renderer);

/**
 * Retrieves a sink pass of a renderer.
 * @param renderer Cannot be NULL.
 * @param sink     Sink index, must be < gfx_renderer_get_num_sinks(renderer).
 *
 * The index of each sink may change when a new pass is added, however
 * their relative order remains fixed during the lifetime of the renderer.
 */
GFX_API GFXPass* gfx_renderer_get_sink(GFXRenderer* renderer, size_t sink);

/**
 * Sets the render state of a pass.
 * Any member of state may be NULL to omit setting the associated state.
 * @param pass  Cannot be NULL.
 * @param state No-op if NULL.
 */
GFX_API void gfx_pass_set_state(GFXPass* pass, const GFXRenderState* state);

/**
 * Retrieves the current render state of a pass.
 * @param pass  Cannot be NULL.
 * @param state Cannot be NULL, output state; read-only!
 */
GFX_API void gfx_pass_get_state(GFXPass* pass, GFXRenderState* state);

/**
 * Retrieves the virtual frame size associated with a pass.
 * @param pass   Cannot be NULL.
 * @param width  Cannot be NULL, output width.
 * @param height Cannot be NULL, output height.
 * @param layers Cannot be NULL, output layers.
 *
 * Only outputs the _actual_ size, meaning this will only return meaningful
 * values when called inbetween gfx_frame_start and gfx_frame_submit.
 * Outputs 0,0,0 if no associated attachments.
 */
GFX_API void gfx_pass_get_size(GFXPass* pass,
                               uint32_t* width, uint32_t* height, uint32_t* layers);

/**
 * Consume an attachment of a renderer.
 * Shader location is in add-order, calling with the same index twice
 * does _not_ change the shader location, should release first.
 * @param pass  Cannot be NULL.
 * @param index Attachment index to consume.
 * @param mask  Access mask to consume the attachment with.
 * @param stage Shader stages with access to the attachment.
 * @return Zero on failure.
 */
GFX_API bool gfx_pass_consume(GFXPass* pass, size_t index,
                              GFXAccessMask mask, GFXShaderStage stage);

/**
 * Consumes a range (area) of an attachment of a renderer.
 * @see gfx_pass_consume.
 */
GFX_API bool gfx_pass_consumea(GFXPass* pass, size_t index,
                               GFXAccessMask mask, GFXShaderStage stage,
                               GFXRange range);

/**
 * Consumes an attachment of a renderer with a specific view.
 * @param view Specifies all properties (and attachment index) to consume with.
 * @see gfx_pass_consume.
 */
GFX_API bool gfx_pass_consumev(GFXPass* pass, size_t index,
                               GFXAccessMask mask, GFXShaderStage stage,
                               GFXView view);

// TODO: Add gfx_pass_copy().
// TODO: Add gfx_pass_blit().
// TODO: Add gfx_pass_resolve()?

/**
 * Clears the contents of a consumed attachment before the pass.
 * No-op if attachment at index is not consumed!
 * @param pass   Cannot be NULL.
 * @param index  Attachment index to clear.
 * @param aspect Cannot contain both color AND depth/stencil!
 */
GFX_API void gfx_pass_clear(GFXPass* pass, size_t index,
                            GFXImageAspect aspect, GFXClear value);

/**
 * Sets the blend state of a consumed attachment independently.
 * The device must support independent blending!
 * @param color (src|dst)Factor are ignored if GFX_BLEND_NO_OP.
 * @param alpha (src|dst)AlphaFactor are ignored if GFX_BLEND_NO_OP.
 * @see gfx_pass_clear.
 */
GFX_API void gfx_pass_blend(GFXPass* pass, size_t index,
                            GFXBlendOpState color, GFXBlendOpState alpha);

/**
 * Release any consumption of an attachment of the renderer.
 * @param pass  Cannot be NULL.
 * @param index Attachment index to release.
 */
GFX_API void gfx_pass_release(GFXPass* pass, size_t index);

/**
 * Retrieves the number of parents of a pass.
 * @param pass Cannot be NULL.
 */
GFX_API size_t gfx_pass_get_num_parents(GFXPass* pass);

/**
 * Retrieves a parent of a pass.
 * @param pass   Cannot be NULL.
 * @param parent Parent index, must be < gfx_pass_get_num_parents(pass).
 */
GFX_API GFXPass* gfx_pass_get_parent(GFXPass* pass, size_t parent);


/****************************
 * Frame operations.
 ****************************/

/**
 * Acquires the next virtual frame of a renderer, blocks until available!
 * Implicitly starts and/or submits if not yet done after the previous call.
 * @param renderer Cannot be NULL.
 * @return Always returns a valid frame.
 */
GFX_API GFXFrame* gfx_renderer_acquire(GFXRenderer* renderer);

/**
 * Retrieves the index of a virtual frame, used to identify the frame.
 * All frame indices are in the range [0, #frames of the renderer].
 * They will be acquired in order, starting at 0.
 * @param frame Cannot be NULL.
 *
 * Can be called from any thread.
 */
GFX_API unsigned int gfx_frame_get_index(GFXFrame* frame);

/**
 * Prepares the acquired virtual frame to start recording,
 * appends all dependency injections if already started.
 * @param frame Cannot be NULL.
 * @param deps  Cannot be NULL if numDeps > 0.
 *
 * The renderer (including its attachments, passes and sets) cannot be
 * modified after this call until gfx_frame_submit has returned!
 *
 * All given dependency objects are referenced until gfx_frame_submit has
 * returned. All signal commands are only made visible to wait commands
 * submitted elsewhere after gfx_frame_submit. However, all wait commands can
 * match visible signal commands submitted elsewhere up until gfx_frame_submit.
 *
 * Failure during starting cannot be recovered from,
 * any such failure is appropriately logged.
 */
GFX_API void gfx_frame_start(GFXFrame* frame,
                             size_t numDeps, const GFXInject* deps);

/**
 * Submits the acquired virtual frame of a renderer.
 * Implicitly starts if not yet done so.
 * @param frame Cannot be NULL, invalidated after this call!
 *
 * All memory resources used to render a frame cannot be freed until the next
 * time this frame is acquired. The frames can be identified by their index.
 *
 * Failure during submission cannot be recovered from,
 * any such failure is appropriately logged.
 */
GFX_API void gfx_frame_submit(GFXFrame* frame);


/****************************
 * Technique creation.
 ****************************/

/**
 * Specialization constant value.
 */
typedef union GFXConstant
{
	int32_t  i32;
	uint32_t u32;
	float    f;

} GFXConstant;


/**
 * Adds a new technique to the renderer.
 * @param renderer   Cannot be NULL.
 * @param numShaders Must be > 0.
 * @param shaders    Cannot be NULL, all must store valid SPIR-V bytecode.
 * @return NULL on failure.
 *
 * Thread-safe with respect to renderer,
 * as are all other functions related to this technique.
 *
 * For each shader stage, the last element in shaders will be taken.
 * Compute shaders cannot be passed in combination with other stages.
 */
GFX_API GFXTechnique* gfx_renderer_add_tech(GFXRenderer* renderer,
                                            size_t numShaders, GFXShader** shaders);

/**
 * Erases (destroys) a technique, removing it from its renderer.
 * @param technique Cannot be NULL.
 */
GFX_API void gfx_erase_tech(GFXTechnique* technique);

/**
 * Retrieves the number of descriptor sets of a technique.
 * @param technique Cannot be NULL.
 */
GFX_API size_t gfx_tech_get_num_sets(GFXTechnique* technique);

/**
 * Retrieves the push constant range's size of a technique.
 * @param technique Cannot be NULL.
 */
GFX_API uint32_t gfx_tech_get_push_size(GFXTechnique* technique);

/**
 * Sets specialization constant of the technique.
 * @param technique Cannot be NULL.
 * @param id        ID of the specialization constant in SPIR-V.
 * @param stage     Shader stages to set the specialization constant of.
 * @param size      Must be sizeof(value.(i32|u32|f)) of the correct data-type.
 * @return Zero if failed to set the constant in one or more shaders.
 *
 * Fails if the technique is already locked.
 * Shaders that do not have the specialization constant are ignored.
 */
GFX_API bool gfx_tech_constant(GFXTechnique* technique,
                               uint32_t id, GFXShaderStage stage,
                               size_t size, GFXConstant value);

/**
 * Sets immutable samplers of the technique.
 * @param technique   Cannot be NULL.
 * @param set         Must be < gfx_tech_get_num_sets(technique).
 * @param numSamplers Must be > 0.
 * @param samplers    Cannot be NULL.
 * @return Zero if failed to set one or more samplers.
 *
 * Fails if the technique is already locked.
 * Warns about samplers that do not match the shader input type.
 */
GFX_API bool gfx_tech_samplers(GFXTechnique* technique,
                               size_t set,
                               size_t numSamplers, const GFXSampler* samplers);

/**
 * Sets a sampler binding of the technique to be immutable.
 * @param technique Cannot be NULL.
 * @param set       Must be < gfx_tech_get_num_sets(technique).
 * @param binding   Descriptor binding number.
 * @return Non-zero if the binding can be made immutable.
 *
 * Fails if the technique is already locked.
 * Warns if the shader input type does not match.
 */
GFX_API bool gfx_tech_immutable(GFXTechnique* technique,
                                size_t set, size_t binding);

/**
 * Sets a buffer binding of the technique to be dynamic.
 * @see gfx_tech_immutable.
 * @return Non-zero if the binding can be made dynamic.
 */
GFX_API bool gfx_tech_dynamic(GFXTechnique* technique,
                              size_t set, size_t binding);

/**
 * Locks the technique, preparing it for rendering & making it immutable.
 * Creating sets from a technique automatically locks the technique.
 * @param technique Cannot be NULL.
 * @return Non-zero on success.
 *
 * After this call has succesfully returned it is thread-safe to call
 * gfx_renderer_add_set from multiple threads with this technique.
 */
GFX_API bool gfx_tech_lock(GFXTechnique* technique);


/****************************
 * Set creation and modification.
 ****************************/

/**
 * Set resource description.
 */
typedef struct GFXSetResource
{
	size_t binding;
	size_t index; // Binding array index.

	GFXReference ref;

} GFXSetResource;


/**
 * Set group (i.e. multiple resources) description.
 */
typedef struct GFXSetGroup
{
	size_t binding;
	size_t offset;      // Binding offset in the group.
	size_t numBindings; // 0 for all remaining bindings.

	GFXGroup* group;

} GFXSetGroup;


/**
 * Adds a new set to the renderer, locking the used technique.
 * @param renderer  Cannot be NULL.
 * @param technique Cannot be NULL, must be from renderer.
 * @param set       Must be < gfx_tech_get_num_sets(technique).
 * @param resources Cannot be NULL if numResources > 0.
 * @param groups    Cannot be NULL if numGroups > 0.
 * @param views     Cannot be NULL if numViews > 0.
 * @param samplers  Cannot be NULL if numSamplers > 0.
 * @return NULL on failure.
 *
 * Thread-safe with respect to renderer,
 * as are all other functions related to this set.
 *
 * However, all but this function CANNOT run during gfx_renderer_acquire or
 * during or inbetween gfx_frame_start and gfx_frame_submit.
 *
 * Thread-safe with respect to technique ONLY IF gfx_tech_lock has
 * succesfully returned (or one call to gfx_renderer_add_set has).
 *
 * If any descriptor binding is assigned multiple resources or samplers,
 * the last matching element in their respective input arrays will be taken.
 * Individual set resources and views will always overwrite group bindings.
 * All views MUST match the shader input type!
 *
 * The returned set will not reference the technique anymore,
 * meaning the technique can be erased while the set still exists!
 */
GFX_API GFXSet* gfx_renderer_add_set(GFXRenderer* renderer,
                                     GFXTechnique* technique, size_t set,
                                     size_t numResources, size_t numGroups,
                                     size_t numViews, size_t numSamplers,
                                     const GFXSetResource* resources,
                                     const GFXSetGroup* groups,
                                     const GFXView* views,
                                     const GFXSampler* samplers);

/**
 * Erases (destroys) a set, removing it from its renderer.
 * @param set Cannot be NULL.
 */
GFX_API void gfx_erase_set(GFXSet* set);

/**
 * Sets descriptor binding resources of the set.
 * @param set          Cannot be NULL.
 * @param numResources Must be > 0.
 * @param resources    Cannot be NULL.
 * @return Zero if failed to set one or more resources.
 *
 * If any descriptor binding is assigned multiple times, the last is taken.
 * Warns about resources that do not match the shader input type.
 */
GFX_API bool gfx_set_resources(GFXSet* set,
                               size_t numResources, const GFXSetResource* resources);

/**
 * Sets descriptor binding resources of the set from groups.
 * @param set       Cannot be NULL.
 * @param numGroups Must be > 0.
 * @param groups    Cannot be NULL.
 * @return Zero if failed to set one or more resources.
 *
 * If any descriptor binding is assigned multiple times, the last is taken.
 * Warns about resources that do not match the shader input type.
 */
GFX_API bool gfx_set_groups(GFXSet* set,
                            size_t numGroups, const GFXSetGroup* groups);

/**
 * Sets resource views of the set.
 * @param set      Cannot be NULL.
 * @param numViews Must be > 0.
 * @param views    Cannot be NULL.
 * @return Zero if failed to set one or more views.
 *
 * If any descriptor binding is assigned multiple views, the last is taken.
 * All views MUST match the shader input type!
 */
GFX_API bool gfx_set_views(GFXSet* set,
                           size_t numViews, const GFXView* views);

/**
 * Sets immutable samplers of the set.
 * @param set         Cannot be NULL.
 * @param numSamplers Must be > 0.
 * @param samplers    Cannot be NULL.
 * @return Zero if failed to set one or more samplers.
 *
 * If any descriptor binding is assigned multiple samplers, the last is taken.
 * Warns about samplers that do not match the shader input type.
 */
GFX_API bool gfx_set_samplers(GFXSet* set,
                              size_t numSamplers, const GFXSampler* samplers);


/****************************
 * Recorder & recording commands.
 ****************************/

/**
 * Compute command flags.
 */
typedef enum GFXComputeFlags
{
	GFX_COMPUTE_NONE   = 0x0000,
	GFX_COMPUTE_ASYNC  = 0x0001,
	GFX_COMPUTE_BEFORE = 0x0002,
	GFX_COMPUTE_AFTER  = 0x0004 // Overrules GFX_COMPUTE_BEFORE.

} GFXComputeFlags;

GFX_BIT_FIELD(GFXComputeFlags)


/**
 * Adds a new recorder to the renderer.
 * @param renderer Cannot be NULL.
 * @return NULL on failure.
 *
 * Thread-safe with respect to renderer,
 * as are all other functions related to this recorder.
 *
 * However, this function and gfx_erase_recorder CANNOT
 * run during gfx_renderer_acquire or
 * during or inbetween gfx_frame_start and gfx_frame_submit.
 */
GFX_API GFXRecorder* gfx_renderer_add_recorder(GFXRenderer* renderer);

/**
 * Erases (destroys) a recorder, removing it from its renderer.
 * @param recorder Cannot be NULL.
 */
GFX_API void gfx_erase_recorder(GFXRecorder* recorder);

/**
 * Retrieves the virtual frame size associated with the current pass.
 * @param recorder Cannot be NULL.
 * @param width    Cannot be NULL, output width.
 * @param height   Cannot be NULL, output height.
 * @param layers   Cannot be NULL, output layers.
 *
 * Only outputs the _actual_ size, meaning this will only return meaningful
 * values when called within a callback of gfx_recorder_(render|compute).
 * Outputs 0,0,0 if no associated attachments.
 */
GFX_API void gfx_recorder_get_size(GFXRecorder* recorder,
                                   uint32_t* width, uint32_t* height, uint32_t* layers);

/**
 * Records render commands within a given pass.
 * The callback takes this recorder and the current virtual frame index.
 * @param recorder Cannot be NULL.
 * @param pass     Cannot be NULL.
 * @param cb       Callback, cannot be NULL.
 * @param ptr      User pointer as third argument of cb.
 *
 * Must be called inbetween gfx_frame_start and gfx_frame_submit!
 * Different recorders can always call gfx_recorder_(render|compute)
 * concurrently, with any arguments!
 */
GFX_API void gfx_recorder_render(GFXRecorder* recorder, GFXPass* pass,
                                 void (*cb)(GFXRecorder*, unsigned int, void*),
                                 void* ptr);

/**
 * TODO: Draft; probably want to make explicit compute passes (or a pass type).
 * Records compute commands within a given pass.
 * The callback takes this recorder and the current virtual frame index.
 * @see gfx_recorder_render.
 *
 * If GFX_COMPUTE_ASYNC is set, pass is ignored and the submission order
 * is before/after all passes as defined by GFX_COMPUTE_(BEFORE|AFTER).
 */
GFX_API void gfx_recorder_compute(GFXRecorder* recorder, GFXComputeFlags flags,
                                  GFXPass* pass,
                                  void (*cb)(GFXRecorder*, unsigned int, void*),
                                  void* ptr);

/**
 * Render command to bind a render/descriptor set.
 * Can only be called within a callback of gfx_recorder_(render|compute)!
 * @param recorder    Cannot be NULL.
 * @param technique   Cannot be NULL.
 * @param numSets     Must be > 0.
 * @param numDynamics Number of dynamic offsets, missing offsets will be 0.
 * @param sets        Cannot be NULL.
 * @param offsets     Cannot be NULL if numDynamics > 0.
 */
GFX_API void gfx_cmd_bind(GFXRecorder* recorder, GFXTechnique* technique,
                          size_t firstSet,
                          size_t numSets, size_t numDynamics,
                          GFXSet** sets,
                          const uint32_t* offsets);

/**
 * Render command to update push constants.
 * Can only be called within a callback of gfx_recorder_(render|compute)!
 * @param recorder  Cannot be NULL.
 * @param technique Cannot be NULL.
 * @param offset    Must be a multiple of 4.
 * @param size      Must be a multiple of 4, zero for all remaining bytes.
 * @param data      An array of size bytes.
 */
GFX_API void gfx_cmd_push(GFXRecorder* recorder, GFXTechnique* technique,
                          uint32_t offset,
                          uint32_t size, const void* data);

/**
 * Render command to record a non-indexed draw.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder   Cannot be NULL.
 * @param renderable Cannot be NULL.
 * @param vertices   Zero for the entire primitive.
 * @param instances  Must be > 0.
 */
GFX_API void gfx_cmd_draw(GFXRecorder* recorder, GFXRenderable* renderable,
                          uint32_t firstVertex, uint32_t vertices,
                          uint32_t firstInstance, uint32_t instances);

/**
 * Render command to record an indexed draw.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder   Cannot be NULL.
 * @param renderable Cannot be NULL.
 * @param indices    Zero for the entire primitive.
 * @param instances  Must be > 0.
 */
GFX_API void gfx_cmd_draw_indexed(GFXRecorder* recorder, GFXRenderable* renderable,
                                  uint32_t firstIndex, uint32_t indices,
                                  int32_t vertexOffset,
                                  uint32_t firstInstance, uint32_t instances);

/**
 * Compute command to record a compute dispatch.
 * Can only be called within a callback of gfx_recorder_compute!
 * @param recorder   Cannot be NULL.
 * @param computable Cannot be NULL.
 * @param groupX     Must be > 0.
 * @param groupY     Must be > 0.
 * @param groupZ     Must be > 0.
 */
GFX_API void gfx_cmd_dispatch(GFXRecorder* recorder, GFXComputable* computable,
                              uint32_t groupX, uint32_t groupY, uint32_t groupZ);


#endif
