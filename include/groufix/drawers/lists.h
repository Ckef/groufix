/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_DRAWERS_LISTS_H
#define GFX_DRAWERS_LISTS_H

#include "groufix/containers/vec.h"
#include "groufix/core/renderer.h"
#include "groufix/def.h"


/**
 * Cull list index (1-based).
 */
typedef size_t GFXCullInd;


/**
 * Cull (visibility) list definition.
 */
typedef struct GFXCullList
{
	GFXVec inds;  // Stores size_t, 0-based index into items, to sort.
	GFXVec items; // Stores { size_t pos, data... }.
	size_t free;  // First free index into items (or SIZE_MAX).

	size_t numVisible;
	size_t elementSize;
	bool   dirty; // True if visiblity changes occurred, may be overwritten.

} GFXCullList;


/**
 * Draw list (sorted & drawable cull list) definition.
 */
typedef struct GFXDrawList
{
	GFXCullList base;

	// Draw function.
	void (*draw)(GFXRecorder*, struct GFXDrawList*, const void*, void*);

	// Comparison function, may be NULL.
	int (*cmp)(struct GFXDrawList*, const void*, const void*);

} GFXDrawList;


/**
 * Render list (draw list specialization) definition.
 */
typedef struct GFXRenderList
{
	GFXDrawList base;

	size_t   elementSize;
	size_t   numSets;
	size_t   numDynamics;
	uint32_t pushSize;

} GFXRenderList;


/**
 * Retrieves the renderable from a render list element.
 * Undefined behaviour if list is not a render list or
 * elem is not a value returned by gfx_cull_list_(get|at) (or a list->draw arg).
 */
static inline GFXRenderable* gfx_render_list_renderable(GFXDrawList* list, const void* elem)
{
	return (GFXRenderable*)((const char*)elem + GFX_ALIGN_UP(
		((GFXRenderList*)list)->elementSize, alignof(max_align_t)));
}

/**
 * Retrieves the bound sets from a render list element.
 * @see gfx_render_list_renderable.
 */
static inline GFXSet** gfx_render_list_sets(GFXDrawList* list, const void* elem)
{
	return (GFXSet**)((const char*)gfx_render_list_renderable(list, elem) +
		GFX_ALIGN_UP(sizeof(GFXRenderable), alignof(max_align_t)));
}

/**
 * Retrieves the bound dynamic offsets from a render list element.
 * @see gfx_render_list_renderable.
 */
static inline uint32_t* gfx_render_list_dynamics(GFXDrawList* list, const void* elem)
{
	const size_t numSets = ((GFXRenderList*)list)->numSets;

	return (uint32_t*)((const char*)gfx_render_list_sets(list, elem) +
		GFX_ALIGN_UP(sizeof(GFXSet*) * numSets, alignof(max_align_t)));
}

/**
 * Retrieves the push constant range from a render list element.
 * @see gfx_render_list_renderable.
 */
static inline void* gfx_render_list_push(GFXDrawList* list, const void* elem)
{
	const size_t numDynamics = ((GFXRenderList*)list)->numDynamics;

	return (void*)((const char*)gfx_render_list_dynamics(list, elem) +
		GFX_ALIGN_UP(sizeof(uint32_t) * numDynamics, alignof(max_align_t)));
}

/**
 * Initializes a cull list.
 * @param list     Cannot be NULL.
 * @param elemSize Must be > 0.
 */
GFX_API void gfx_cull_list_init(GFXCullList* list, size_t elemSize);

/**
 * Clears the content of a cull list.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_cull_list_clear(GFXCullList* list);

/**
 * Initializes a draw list.
 * @param list     Cannot be NULL.
 * @param elemSize Must be > 0.
 * @param draw     Cannot be NULL.
 * @param cmp      May be NULL to not sort.
 *
 * 'draw' takes a recorder, this draw list,
 * an element pointer and a user pointer.
 *
 * 'cmp' takes this draw list
 * and two element pointers, l and r, it should return:
 *  < 0 if l < r
 *  > 0 if l > r
 *  0 if l == r
 */
GFX_API void gfx_draw_list_init(GFXDrawList* list, size_t elemSize,
                                void (*draw)(GFXRecorder*, GFXDrawList*, const void*, void*),
                                int (*cmp)(GFXDrawList*, const void*, const void*));

/**
 * Clears the content of a draw list.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_draw_list_clear(GFXDrawList* list);

/**
 * Initializes a render list (draw list specialization).
 * Will internally sort all elements based on pipeline switching of sets.
 * @param elemSize    May be 0.
 * @param numSets     Must be > 0.
 * @param numDynamics May be 0.
 * @param pushSize    May be 0, must be a multiple of 4.
 * @see gfx_draw_list_init.
 *
 * Cannot use gfx_cull_list_add on list,
 * MUST use gfx_render_list_add!
 *
 * The render list will sort on all leading non-NULL sets associated with each
 * element. If any set is NULL, all after will be ignored.
 */
GFX_API void gfx_render_list_init(GFXRenderList* list, size_t elemSize,
                                  size_t numSets, size_t numDynamics, uint32_t pushSize,
                                  void (*draw)(GFXRecorder*, GFXDrawList*, const void*, void*));

/**
 * Clears the content of a render list.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_render_list_clear(GFXRenderList* list);

/**
 * Adds a new element to a cull list.
 * @param list Cannot be NULL.
 * @param elem May be NULL to add empty.
 * @return Non-zero on success.
 */
GFX_API GFXCullInd gfx_cull_list_add(GFXCullList* list, const void* elem,
                                     bool visible);

/**
 * Adds a new element to a render list.
 * @param renderable  Renderable to copy, may be NULL to leave undefined.
 * @param numSets     Must be <= list->numSets.
 * @param sets        Cannot be NULL if numSets > 0.
 * @param numDynamics Must be <= list->numDynamics.
 * @param offsets     Cannot be NULL if numDynamics > 0.
 * @param pushSize    Must be <= list->pushSize, must be a multiple of 4.
 * @param push        Cannot be NULL if pushSize > 0.
 * @see gfx_cull_list_add.
 *
 * Note: even when the renderable or sets are not set through this function,
 * they MUST be set before sorting or drawing the list!
 */
GFX_API GFXCullInd gfx_render_list_add(GFXRenderList* list, const void* elem,
                                       const GFXRenderable* renderable,
                                       size_t numSets, GFXSet** sets,
                                       size_t numDynamics, const uint32_t* offsets,
                                       uint32_t pushSize, const void* push,
                                       bool visible);

/**
 * Erases an element from a cull list.
 * @param list Cannot be NULL.
 * @param ind  Must be a non-zero value returned by gfx_(cull|render)_list_add.
 */
GFX_API void gfx_cull_list_erase(GFXCullList* list, GFXCullInd ind);

/**
 * Retrieves the element data from a cull list index.
 * @param list Cannot be NULL.
 * @param ind  Must be a non-zero value returend by gfx_(cull|render)_list_add.
 */
GFX_API void* gfx_cull_list_get(GFXCullList* list, GFXCullInd ind);

/**
 * Retrieves whether an element of a cull list is visible or not.
 * @param list Cannot be NULL.
 * @param ind  Must be a non-zero value returned by gfx_(cull|render)_list_add.
 */
GFX_API bool gfx_cull_list_is_visible(GFXCullList* list, GFXCullInd ind);

/**
 * Sets the visibility of an element of a cull list.
 * @param list Cannot be NULL.
 * @param ind  Must be a non-zero value returend by gfx_(cull|render)_list_add.
 */
GFX_API void gfx_cull_list_set_visible(GFXCullList* list, GFXCullInd ind,
                                       bool visible);

/**
 * Sets the visibility of all elements of a cull list.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_cull_list_reset_visible(GFXCullList* list, bool visible);

/**
 * Retrieves visible element data from a cull list.
 * @param index Must be < list->numVisible.
 */
GFX_API void* gfx_cull_list_at(GFXCullList* list, size_t index);

/**
 * Sort all visible elements of a draw list.
 * Automatically called by gfx_cmd_draw_list.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_draw_list_sort(GFXDrawList* list);

/**
 * Proxy command to draw a draw list.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder Cannot be NULL.
 * @param list     Cannot be NULL.
 * @param ptr      User pointer as last argument of the draw function.
 */
GFX_API void gfx_cmd_draw_list(GFXRecorder* recorder,
                               GFXDrawList* list, void* ptr);

/**
 * State command to bind descriptor sets from a render list element.
 * Can only be called within a callback of gfx_recorder_render!
 * @param recorder    Cannot be NULL.
 * @param list        Cannot be NULL, must be a render list!
 * @param elem        Must be a value returned by gfx_cull_list_(get|at).
 * @param numDynamics May be > 0 to override stored dynamic offsets.
 * @param offsets     Cannot be NULL if numDynamics > 0.
 *
 * 'elem' can be taken from the parameters of the list's draw function.
 */
GFX_API void gfx_cmd_bind_render_list(GFXRecorder* recorder,
                                      GFXDrawList* list, const void* elem,
                                      size_t numDynamics, const uint32_t* offsets);

/**
 * State command to update push constants from a render list element.
 * Can only be called within a callback of gfx_recorder_render!
 * @param pushSize May be > 0 to override stored push constants.
 * @param push     Cannot be NULL if pushSize > 0.
 * @see gfx_cmd_bind_render_list.
 */
GFX_API void gfx_cmd_push_render_list(GFXRecorder* recorder,
                                      GFXDrawList* list, const void* elem,
                                      uint32_t pushSize, const void* push);


#endif
