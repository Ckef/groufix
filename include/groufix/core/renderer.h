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
 * Viewport description.
 */
typedef struct GFXViewport
{
	GFXSizeClass size;

	union {
		float x;
		float xOffset;
	};

	union {
		float y;
		float yOffset;
	};

	union {
		float width;
		float xScale;
	};

	union {
		float height;
		float yScale;
	};

	float minDepth;
	float maxDepth;

} GFXViewport;


/**
 * Scissor description.
 */
typedef struct GFXScissor
{
	GFXSizeClass size;

	union {
		int32_t x;
		float xOffset;
	};

	union {
		int32_t y;
		float yOffset;
	};

	union {
		uint32_t width;
		float xScale;
	};

	union {
		uint32_t height;
		float yScale;
	};

} GFXScissor;


/**
 * Attachment description.
 */
typedef struct GFXAttachment
{
	GFXImageType   type;
	GFXMemoryFlags flags;
	GFXImageUsage  usage;

	GFXFormat     format;
	unsigned char samples; // 1 <= 2^n <= 64.
	uint32_t      mipmaps;
	uint32_t      layers;

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
 * Image clear value.
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
 * Sampling filter ('reduction') mode.
 */
typedef enum GFXFilterMode
{
	GFX_FILTER_MODE_AVERAGE,
	GFX_FILTER_MODE_MIN,
	GFX_FILTER_MODE_MAX

} GFXFilterMode;


/**
 * Sampling wrap behaviour.
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

	GFXRange      range;
	GFXSwizzleMap swizzle;

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
	GFXTopology   topo;    // Topology when no primitive is given.
	unsigned char samples; // 1 <= 2^n <= 64.

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
 * Virtual frame definition.
 */
typedef struct GFXFrame GFXFrame;


/**
 * Pass (i.e. render/compute pass) definition.
 */
typedef struct GFXPass GFXPass;


/**
 * Technique (i.e. shader pipeline) definition.
 */
typedef struct GFXTechnique GFXTechnique;


/**
 * Set (i.e. descriptor set) definition.
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
	// All read-only.
	GFXPass*      pass;
	GFXTechnique* technique;
	GFXPrimitive* primitive;

	const GFXRenderState* state;

	GFX_ATOMIC(bool) lock;

	uintptr_t pipeline;
	uint32_t  gen;

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
 * @param pass       Cannot be NULL, must be a render pass.
 * @param tech       Cannot be NULL.
 * @param prim       May be NULL!
 * @param state      May be NULL, overrides pass state.
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
 *  If any pass or attachment of the renderer is changed after this call, the
 *  warmup data is invalidated and this call is effectively rendered a waste.
 */
GFX_API bool gfx_renderable_warmup(GFXRenderable* renderable);

/**
 * Initializes a computable.
 * The object pointed to by computable _CAN_ be moved or copied!
 * @param computable Cannot be NULL.
 * @see gfx_renderable.
 *
 * No need for a pass, computables can be dispatched in any compute pass!
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
 * @param heap   Cannot be NULL, heap to allocate attachments from.
 * @param frames Number of virtual frames, must be > 0 (preferably > 1).
 * @return NULL on failure.
 */
GFX_API GFXRenderer* gfx_create_renderer(GFXHeap* heap, unsigned int frames);

/**
 * Destroys a renderer.
 * This will forcefully submit and block until rendering is done!
 */
GFX_API void gfx_destroy_renderer(GFXRenderer* renderer);

/**
 * Returns the heap the renderer was created with.
 * Can be called from any thread.
 */
GFX_API GFXHeap* gfx_renderer_get_heap(GFXRenderer* renderer);

/**
 * Returns the device the renderer was created for.
 * Can be called from any thread.
 */
GFX_API GFXDevice* gfx_renderer_get_device(GFXRenderer* renderer);

/**
 * Retrieves the number of virtual frames of a renderer.
 * @param renderer Cannot be NULL.
 *
 * Can be called from any thread.
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
GFX_API bool gfx_renderer_load_cache(GFXRenderer* renderer, const GFXReader* src);

/**
 * Stores the current groufix pipeline cache data.
 * @param renderer Cannot be NULL.
 * @param dst      Destination stream, cannot be NULL.
 * @return Zero on failure.
 *
 * Cannot run concurrently with _ANY_ function of the renderer's descendants!
 */
GFX_API bool gfx_renderer_store_cache(GFXRenderer* renderer, const GFXWriter* dst);


/****************************
 * Frame operations.
 ****************************/

/**
 * Acquires the next virtual frame and prepares it to start recording.
 * Calls gfx_renderer_acquire and gfx_frame_start back to back.
 * @see gfx_renderer_acquire.
 * @see gfx_frame_start.
 */
GFX_API GFXFrame* gfx_renderer_start(GFXRenderer* renderer);

/**
 * Acquires the next virtual frame of a renderer, blocks until available!
 * Implicitly starts and/or submits if not yet done after the previous call.
 * @param renderer Cannot be NULL.
 * @return Always returns a valid frame.
 */
GFX_API GFXFrame* gfx_renderer_acquire(GFXRenderer* renderer);

/**
 * Retrieves the index of a virtual frame, used to identify the frame.
 * All frame indices are in the range [0, #frames of the renderer - 1].
 * They will be acquired in order, starting at 0.
 * @param frame Cannot be NULL.
 *
 * Can be called from any thread.
 */
GFX_API unsigned int gfx_frame_get_index(GFXFrame* frame);

/**
 * Prepares the acquired virtual frame to start recording.
 * Can only be called inbetween gfx_renderer_acquire and gfx_frame_submit!
 * @param frame Cannot be NULL.
 *
 * The renderer (including its attachments, passes and sets) cannot be
 * modified after this call until gfx_frame_submit has returned!
 * Nor can attachments be described, windows be attached or
 * passes be added during that period.
 *
 * Failure during starting cannot be recovered from,
 * any such failure is appropriately logged.
 */
GFX_API void gfx_frame_start(GFXFrame* frame);

/**
 * Submits the acquired virtual frame of a renderer.
 * Can only be called once after gfx_frame_acquire.
 * Implicitly starts if not yet done so.
 * @param frame Cannot be NULL.
 *
 * All asynchronous passes are after all others in submission order.
 *
 * All memory resources used to render a frame cannot be freed until the next
 * time this frame is acquired. The frames can be identified by their index.
 *
 * Will call gfx_heap_purge() on the associated heap when done.
 *
 * Failure during submission cannot be recovered from,
 * any such failure is appropriately logged.
 */
GFX_API void gfx_frame_submit(GFXFrame* frame);

/**
 * Injects dependency commands in a given pass.
 * @param pass Cannot be NULL.
 * @param injs Cannot be NULL if numInjs > 0.
 *
 * NOT thread-safe with respect to pass!
 * Cannot be called during gfx_frame_submit!
 *
 * All dependency objects are referenced until the first call to
 * gfx_frame_submit during which the pass is unculled (ergo used in the frame)
 * has returned.
 *
 * All signal commands are made visible to wait commands injected in passes
 * later in submission order. They are made visible to wait commands submitted
 * anywhere else as soon as gfx_frame_submit returns.
 *
 * All wait commands only see signal commands injected in passes earlier in
 * submission order. However, all wait commands can match any visible signal
 * commands submitted elsewhere up until gfx_frame_submit is called.
 *
 * It is undefined behaviour to use this call to inject dependencies between
 * two render passes in the same frame, gfx_pass_depend should be used.
 */
GFX_API void gfx_pass_inject(GFXPass* pass,
                             size_t numInjs, const GFXInject* injs);

/**
 * Appends dependency commands to given passes, effectively 'injecting' the
 * given commands before every frame the passes are used in.
 * @param pass Cannot be NULL.
 * @param wait Cannot be NULL, pass to implicitly wait, must not be pass.
 * @param injs Cannot be NULL if numInjs > 0.
 *
 * NOT thread-safe with respect to pass or wait!
 * Cannot be called during or inbetween gfx_frame_start and gfx_frame_submit!
 *
 * This is the only call that takes the gfx_sig* macro family as injections!
 * To inject between two render passes, use the gfx_sig* macro family.
 * All dependency objects are referenced until gfx_renderer_undepend is called.
 *
 * Any signal command is implicitly waited upon by the wait pass.
 * Meaning there is no need to inject a matching wait command anywhere.
 *
 * For each dependency object that is implicitly being waited upon by a pass,
 * only a single wait command will be injected in that pass,
 * even if gfx_pass_depend is called multiple times.
 *
 * It is undefined behaviour to use this call to inject a dependency object
 * between two render passes in the same frame.
 */
GFX_API void gfx_pass_depend(GFXPass* pass, GFXPass* wait,
                             size_t numInjs, const GFXInject* injs);

/**
 * Removes ALL dependency commands appended to any pass
 * within renderer via a call to gfx_pass_depend.
 * @param renderer Cannot be NULL.
 *
 * NOT thread-safe with respect to renderer or any of its passes!
 * Cannot be called during or inbetween gfx_frame_start and gfx_frame_submit!
 */
GFX_API void gfx_renderer_undepend(GFXRenderer* renderer);

/**
 * Blocks until all virtual frames are done rendering.
 * Does not block if a frame is not submitted.
 * @param renderer Cannot be NULL.
 *
 * Failure during blocking cannot be recovered from,
 * any such failure is appropriately logged.
 */
GFX_API void gfx_renderer_block(GFXRenderer* renderer);

/**
 * Blocks until a virtual frame is done rendering.
 * No-op if the frame is not submitted.
 * @param frame Cannot be NULL.
 *
 * Failure during blocking cannot be recovered from,
 * any such failure is appropriately logged.
 */
GFX_API void gfx_frame_block(GFXFrame* frame);


/****************************
 * Attachment handling.
 ****************************/

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
 * Render/compute pass type.
 */
typedef enum GFXPassType
{
	GFX_PASS_RENDER,
	GFX_PASS_COMPUTE_INLINE,
	GFX_PASS_COMPUTE_ASYNC

} GFXPassType;


/**
 * Adds a new (sink) pass to the renderer given a set of parent.
 * A pass will be after all its parents in submission order.
 * Each element in parents must be associated with the same renderer.
 * @param renderer   Cannot be NULL.
 * @param group      The cull group this pass is in.
 * @param numParents Number of parents, 0 for none.
 * @param parents    Parent passes, cannot be NULL if numParents > 0.
 * @return NULL on failure.
 *
 * Asynchronous compute passes cannot be the parent of any render or inline
 * compute passes and vice versa. They are separate graphs to allow for
 * asynchronous execution.
 *
 * All asynchronous passes are after all others in submission order.
 */
GFX_API GFXPass* gfx_renderer_add_pass(GFXRenderer* renderer, GFXPassType type,
                                       unsigned int group,
                                       size_t numParents, GFXPass** parents);
/**
 * Returns the renderer the pass was added to.
 * Can be called from any thread.
 * @param pass Cannot be NULL.
 */
GFX_API GFXRenderer* gfx_pass_get_renderer(GFXPass* pass);

/**
 * Retrieves the type of a pass.
 * Can be called from any thread.
 * @param pass Cannot be NULL.
 * @return Type of the pass, render, inline compute or async compute.
 */
GFX_API GFXPassType gfx_pass_get_type(GFXPass* pass);

/**
 * Retrieves the cull group this pass is in.
 * Can be called from any thread.
 * @param pass Cannot be NULL.
 */
GFX_API unsigned int gfx_pass_get_group(GFXPass* pass);

/**
 * Sets all passes in a cull group to NOT be rendered.
 * @param renderer Cannot be NULL.
 * @param group    Cull group to cull.
 */
GFX_API void gfx_renderer_cull(GFXRenderer* renderer, unsigned int group);

/**
 * Sets all passes in a cull group to be rendered.
 * @param renderer Cannot be NULL.
 * @param group    Cull group to uncull.
 */
GFX_API void gfx_renderer_uncull(GFXRenderer* renderer, unsigned int group);

/**
 * Retrieves whether the pass is currently culled or not.
 * @param pass Cannot be NULL.
 * @return Non-zero if the pass is culled.
 */
GFX_API bool gfx_pass_is_culled(GFXPass* pass);

/**
 * Consume an attachment of a renderer.
 * @param pass  Cannot be NULL.
 * @param index Attachment index to consume.
 * @param mask  Access mask to consume the attachment with.
 * @param stage Shader stages with access to the attachment.
 * @return Zero on failure.
 *
 * Only has effect if called on a render or _inline_ compute pass.
 * Will fail if called on _async_ compute passes.
 *
 * This call should be used to specify 'dependencies' between
 * passes that use the same attachments.
 * Never use gfx_pass_depend for this (except if it is an async compute pass)!
 *
 * For synchronization purposes it is still necessary to consume an attachment
 * when said attachment is only used in bound sets while recording.
 *
 * For render passes:
 *  Shader location is in add-order, calling with the same index twice
 *  does _not_ change the shader location, should release first.
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

/**
 * Clears the contents of a consumed attachment before the pass.
 * @param pass   Cannot be NULL.
 * @param index  Attachment index to clear.
 * @param aspect Cannot contain both color AND depth/stencil!
 *
 * No-op if attachment at index is not consumed.
 * Only has effect if consumed by a render pass, with attachment access.
 */
GFX_API void gfx_pass_clear(GFXPass* pass, size_t index,
                            GFXImageAspect aspect, GFXClear value);

/**
 * Sets the blend state of a consumed attachment independently.
 * The device must support independent blending!
 * This overrides blend state of renderables used in this pass!
 * @param color (src|dst)Factor are ignored if GFX_BLEND_NO_OP.
 * @param alpha (src|dst)AlphaFactor are ignored if GFX_BLEND_NO_OP.
 * @see gfx_pass_clear.
 */
GFX_API void gfx_pass_blend(GFXPass* pass, size_t index,
                            GFXBlendOpState color, GFXBlendOpState alpha);

/**
 * Resolves the contents of a consumed attachment to another after the pass.
 * @param resolve Attachment index to resolve to.
 * @see gfx_pass_clear.
 *
 * No-op if either attachment at index or resolve is not consumed!
 * Will be unset if the attachment at either index or resolve is released!
 */
GFX_API void gfx_pass_resolve(GFXPass* pass, size_t index, size_t resolve);

/**
 * Release any consumption of an attachment of the renderer.
 * This will reset all state once the attachment is consumed again.
 * @param pass  Cannot be NULL.
 * @param index Attachment index to release.
 */
GFX_API void gfx_pass_release(GFXPass* pass, size_t index);

/**
 * Sets the render state of a render pass.
 * @param pass  Cannot be NULL.
 * @param state Any member may be NULL to omit setting the associated state.
 *
 * No-op if not a render pass.
 */
GFX_API void gfx_pass_set_state(GFXPass* pass, GFXRenderState state);

/**
 * Retrieves the current render state of a render pass.
 * @param pass Cannot be NULL.
 * @return Output state, read-only!
 *
 * Returns all NULL's if not a render pass.
 */
GFX_API GFXRenderState gfx_pass_get_state(GFXPass* pass);

/**
 * Sets the viewport state of a render pass.
 * @param pass Cannot be NULL.
 *
 * No-op if not a render pass.
 */
GFX_API void gfx_pass_set_viewport(GFXPass* pass, GFXViewport viewport);

/**
 * Sets the scissor state of a render pass.
 * @see gfx_pass_set_viewport.
 */
GFX_API void gfx_pass_set_scissor(GFXPass* pass, GFXScissor scissor);

/**
 * Retrieves the current viewport state of a render pass.
 * @param pass Cannot be NULL.
 *
 * Returns an absolute size of all 0's if not a render pass.
 */
GFX_API GFXViewport gfx_pass_get_viewport(GFXPass* pass);

/**
 * Retrieves the current scissor state of a render pass.
 * @see gfx_pass_get_viewport.
 */
GFX_API GFXScissor gfx_pass_get_scissor(GFXPass* pass);

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
 * Returns the renderer the technique was added to.
 * Can be called from any thread.
 * @param technique Cannot be NULL.
 */
GFX_API GFXRenderer* gfx_tech_get_renderer(GFXTechnique* technique);

/**
 * Retrieves a shader of a technique.
 * Can be called from any thread.
 * @param technique Cannot be NULL.
 * @param stage     Shader stage, exactly 1 stage must be set.
 * @return May be NULL if no shader was given.
 */
GFX_API GFXShader* gfx_tech_get_shader(GFXTechnique* technique, GFXShaderStage stage);

/**
 * Retrieves the number of descriptor sets of a technique.
 * Can be called from any thread.
 * @param technique Cannot be NULL.
 */
GFX_API size_t gfx_tech_get_num_sets(GFXTechnique* technique);

/**
 * Retrieves the maximum number of bindings any set of a technique has.
 * Can be called from any thread.
 * @param technique Cannot be NULL.
 */
GFX_API size_t gfx_tech_get_max_bindings(GFXTechnique* technique);

/**
 * Retrieves the resource type of a descriptor binding of a set.
 * NOTE: Inefficient! Should prefer gfx_set_get_resource_type if possible!
 * Can be called from any thread.
 * @param technique Cannot be NULL.
 */
GFX_API GFXShaderResourceType gfx_tech_get_resource_type(GFXTechnique* technique,
                                                         size_t set, size_t binding);

/**
 * Retrieves the size (i.e. shader array size) of a descriptor binding of a set.
 * NOTE: Inefficient! Should prefer gfx_set_get_binding_size if possible!
 * Can be called from any thread.
 * @param technique Cannot be NULL.
 */
GFX_API size_t gfx_tech_get_binding_size(GFXTechnique* technique,
                                         size_t set, size_t binding);

/**
 * Retrieves the block byte size of a buffer descriptor binding of a set.
 * NOTE: Inefficient! Should prefer gfx_set_get_binding_block_size if possible!
 * Can be called from any thread.
 * @param technique Cannot be NULL.
 * @return Zero if the binding is not a block, or unknown size.
 */
GFX_API size_t gfx_tech_get_binding_block_size(GFXTechnique* technique,
                                               size_t set, size_t binding);

/**
 * Retrieves the push constant range's size of a technique.
 * Can be called from any thread.
 * @param technique Cannot be NULL.
 */
GFX_API uint32_t gfx_tech_get_push_size(GFXTechnique* technique);

/**
 * Retrieves shader stages that access the push constant range of a technique.
 * Can be called from any thread.
 * @param technique Cannot be NULL.
 */
GFX_API GFXShaderStage gfx_tech_get_push_stages(GFXTechnique* technique);

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
 * However, all but this function (and all getters) CANNOT run during
 * gfx_renderer_acquire or during or inbetween gfx_frame_start and gfx_frame_submit.
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
 * Returns the renderer the set was added to.
 * Can be called from any thread.
 * @param set Cannot be NULL.
 */
GFX_API GFXRenderer* gfx_set_get_renderer(GFXSet* set);

/**
 * Retrieves the number of descriptor bindings of a set.
 * Can be called from any thread.
 * @param set Cannot be NULL.
 */
GFX_API size_t gfx_set_get_num_bindings(GFXSet* set);

/**
 * Retrieves the resource type of a descriptor binding of a set.
 * Can be called from any thread.
 * @param set Cannot be NULL.
 */
GFX_API GFXShaderResourceType gfx_set_get_resource_type(GFXSet* set, size_t binding);

/**
 * Retrieves the size (i.e. shader array size) of a descriptor binding of a set.
 * Can be called from any thread.
 * @param set Cannot be NULL.
 */
GFX_API size_t gfx_set_get_binding_size(GFXSet* set, size_t binding);

/**
 * Retrieves the block byte size of a buffer descriptor binding of a set.
 * Can be called from any thread.
 * @param set Cannot be NULL.
 * @return Zero if the binding is not a block, or unknown size.
 */
GFX_API size_t gfx_set_get_binding_block_size(GFXSet* set, size_t binding);

/**
 * Retrieves whether a descriptor binding is immutable.
 * Can be called from any thread.
 * @param set Cannot be NULL.
 * @return Always zero if this binding is not a sampler.
 */
GFX_API bool gfx_set_is_binding_immutable(GFXSet* set, size_t binding);

/**
 * Retrieves whether a descriptor binding is dynamic.
 * Can be called from any thread.
 * @param set Cannot be NULL.
 */
GFX_API bool gfx_set_is_binding_dynamic(GFXSet* set, size_t binding);

/**
 * Retrieves the number of total dynamic descriptors of all bindings.
 * Can be called from any thread.
 * @param set Cannot be NULL.
 */
GFX_API size_t gfx_set_get_num_dynamics(GFXSet* set);

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
 * Indirect draw command parameters.
 */
typedef struct GFXDrawCmd
{
	uint32_t vertices;  // Must be > 0.
	uint32_t instances; // Must be > 0.
	uint32_t firstVertex;
	uint32_t firstInstance;

} GFXDrawCmd;


/**
 * Indirect indexed draw command parameters.
 */
typedef struct GFXDrawIndexedCmd
{
	uint32_t indices;   // Must be > 0.
	uint32_t instances; // Must be > 0.
	uint32_t firstIndex;
	int32_t  vertexOffset;
	uint32_t firstInstance;

} GFXDrawIndexedCmd;


/**
 * Indirect dispatch command parameters.
 */
typedef struct GFXDispatchCmd
{
	uint32_t xCount;
	uint32_t yCount;
	uint32_t zCount;

} GFXDispatchCmd;


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
 * Returns the renderer the recorder was added to.
 * Can be called from any thread.
 * @param recorder Cannot be NULL.
 */
GFX_API GFXRenderer* gfx_recorder_get_renderer(GFXRecorder* recorder);

/**
 * Records render commands within a given render pass.
 * @param recorder Cannot be NULL.
 * @param pass     Cannot be NULL, must be a render pass.
 * @param cb       Callback, cannot be NULL.
 * @param ptr      User pointer as second argument of cb.
 *
 * The callback takes this recorder and a user pointer as arguments.
 * If pass is culled, this call becomes a no-op.
 *
 * Must be called inbetween gfx_frame_start and gfx_frame_submit!
 * Different recorders can always call gfx_recorder_(render|compute)
 * concurrently, with any arguments!
 */
GFX_API void gfx_recorder_render(GFXRecorder* recorder, GFXPass* pass,
                                 void (*cb)(GFXRecorder*, void*),
                                 void* ptr);

/**
 * Records compute commands within a given compute pass.
 * @param pass Cannot be NULL, must be a compute pass.
 * @see gfx_recorder_render.
 */
GFX_API void gfx_recorder_compute(GFXRecorder* recorder, GFXPass* pass,
                                  void (*cb)(GFXRecorder*, void*),
                                  void* ptr);

/**
 * Retrieves the current virtual frame index.
 * @param recorder Cannot be NULL.
 *
 * Gets updated during gfx_renderer_acquire.
 */
GFX_API unsigned int gfx_recorder_get_frame_index(GFXRecorder* recorder);

/**
 * Retrieves the current pass of a recorder.
 * @param recorder Cannot be NULL.
 *
 * Returns NULL if not called
 * within a callback of gfx_recorder_(render|compute).
 */
GFX_API GFXPass* gfx_recorder_get_pass(GFXRecorder* recorder);

/**
 * Retrieves the virtual frame size associated with the current pass.
 * @param recorder Cannot be NULL.
 * @param width    Cannot be NULL, output width.
 * @param height   Cannot be NULL, output height.
 * @param layers   Cannot be NULL, output layers.
 *
 * Only outputs the _actual_ size, meaning this will only return meaningful
 * values when called within a callback of gfx_recorder_(render|compute).
 * Outputs 0,0,0 if no associated attachments or not a render pass.
 */
GFX_API void gfx_recorder_get_size(GFXRecorder* recorder,
                                   uint32_t* width, uint32_t* height, uint32_t* layers);

/**
 * Retrieves the virtual frame size associated with a render pass.
 * @see gfx_recorder_get_size.
 *
 * Returns meaningful values differently from gfx_recorder_get_size:
 * it does so when called inbetween gfx_frame_start and gfx_frame_submit.
 */
GFX_API void gfx_pass_get_size(GFXPass* pass,
                               uint32_t* width, uint32_t* height, uint32_t* layers);

/**
 * Retrieves the current viewport state of a recorder.
 * @param recorder Cannot be NULL.
 *
 * Returns an absolute size of all 0's if not called
 * within a callback of gfx_recorder_render.
 */
GFX_API GFXViewport gfx_recorder_get_viewport(GFXRecorder* recorder);

/**
 * Retrieves the current scissor state of a recorder.
 * @see gfx_recorder_get_viewport.
 */
GFX_API GFXScissor gfx_recorder_get_scissor(GFXRecorder* recorder);

/**
 * Retrieves the line width state of a recorder.
 * @param recorder Cannot be NULL.
 *
 * Returns 0.0f if not called within a callback of gfx_recorder_render.
 */
GFX_API float gfx_recorder_get_line_width(GFXRecorder* recorder);

/**
 * State command to bind a descriptor set.
 * Can only be called within a callback of gfx_recorder_(render|compute)!
 * @param recorder    Cannot be NULL.
 * @param technique   Cannot be NULL.
 * @param numSets     Must be > 0.
 * @param numDynamics Number of dynamic offsets, missing offsets will be 0.
 * @param sets        Cannot be NULL.
 * @param offsets     Cannot be NULL if numDynamics > 0.
 *
 * All offsets must be properly aligned for uniform or storage buffer usages.
 */
GFX_API void gfx_cmd_bind(GFXRecorder* recorder, GFXTechnique* technique,
                          size_t firstSet,
                          size_t numSets, size_t numDynamics,
                          GFXSet** sets,
                          const uint32_t* offsets);

/**
 * State command to update push constants.
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
                          uint32_t vertices, uint32_t instances,
                          uint32_t firstVertex, uint32_t firstInstance);

/**
 * Render command to record an indexed draw.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder   Cannot be NULL.
 * @param renderable Cannot be NULL.
 * @param indices    Zero for the entire primitive.
 * @param instances  Must be > 0.
 */
GFX_API void gfx_cmd_draw_indexed(GFXRecorder* recorder, GFXRenderable* renderable,
                                  uint32_t indices, uint32_t instances,
                                  uint32_t firstIndex, int32_t vertexOffset,
                                  uint32_t firstInstance);

/**
 * Render command to record a draw call for an entire primitive.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder   Cannot be NULL.
 * @param renderable Cannot be NULL, must have a primitive!
 * @param instances  Must be > 0.
 */
GFX_API void gfx_cmd_draw_prim(GFXRecorder* recorder, GFXRenderable* renderable,
                               uint32_t instances, uint32_t firstInstance);

/**
 * Render command to indirectly (from buffer) record a non-indexed draw.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder   Cannot be NULL.
 * @param renderable Cannot be NULL.
 * @param count      Number of draws to execute, can be zero.
 * @param stride     Must be a multiple of 4, zero for tight packing.
 * @param ref        Cannot be GFX_REF_NULL.
 *
 * The buffer must contain count GFXDrawCmd structures
 * with stride bytes inbetween successive structures.
 */
GFX_API void gfx_cmd_draw_from(GFXRecorder* recorder, GFXRenderable* renderable,
                               uint32_t count,
                               uint32_t stride, GFXBufferRef ref);

/**
 * Render command to indirectly (from buffer) record an indexed draw.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder   Cannot be NULL.
 * @param renderable Cannot be NULL.
 * @param count      Number of draws to execute, can be zero.
 * @param stride     Must be a multiple of 4, zero for tight packing.
 * @param ref        Cannot be GFX_REF_NULL.
 *
 * The buffer must contain count GFXDrawIndexedCmd structures
 * with stride bytes inbetween successive structures.
 */
GFX_API void gfx_cmd_draw_indexed_from(GFXRecorder* recorder, GFXRenderable* renderable,
                                       uint32_t count,
                                       uint32_t stride, GFXBufferRef ref);

/**
 * Compute command to record a compute dispatch.
 * Can only be called within a callback of gfx_recorder_compute!
 * @param recorder   Cannot be NULL.
 * @param computable Cannot be NULL.
 * @param xCount     Must be > 0.
 * @param yCount     Must be > 0.
 * @param zCount     Must be > 0.
 */
GFX_API void gfx_cmd_dispatch(GFXRecorder* recorder, GFXComputable* computable,
                              uint32_t xCount, uint32_t yCount, uint32_t zCount);

/**
 * Compute command to record a compute dispatch with non-zero base workgroups.
 * Can only be called within a callback of gfx_recorder_compute!
 * @see gfx_cmd_dispatch.
 */
GFX_API void gfx_cmd_dispatch_base(GFXRecorder* recorder, GFXComputable * computable,
                                   uint32_t xBase, uint32_t yBase, uint32_t zBase,
                                   uint32_t xCount, uint32_t yCount, uint32_t zCount);

/**
 * Compute command to indirectly (from buffer) record a compute dispatch.
 * Can only be called within a callback of gfx_recorder_compute!
 * @param recorder   Cannot be NULL.
 * @param computable Cannot be NULL.
 * @param ref        Cannot be GFX_REF_NULL.
 *
 * The buffer must contain a GFXDispatchCmd structure.
 */
GFX_API void gfx_cmd_dispatch_from(GFXRecorder* recorder, GFXComputable* computable,
                                   GFXBufferRef ref);

/**
 * State command to set the viewport state of a recorder.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder Cannot be NULL.
 *
 * Default value is determined by the pass.
 * Current value can be retrieved with gfx_recorder_get_viewport.
 */
GFX_API void gfx_cmd_set_viewport(GFXRecorder* recorder, GFXViewport viewport);

/**
 * State command to set the scissor state of a recorder.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder Cannot be NULL.
 *
 * Default value is determined by the pass.
 * Current value can be retrieved with gfx_recorder_get_scissor.
 */
GFX_API void gfx_cmd_set_scissor(GFXRecorder* recorder, GFXScissor scissor);

/**
 * State command to set the line width state of a recorder.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder Cannot be NULL.
 *
 * Default value is 1.0f.
 */
GFX_API void gfx_cmd_set_line_width(GFXRecorder* recorder, float lineWidth);


#endif
