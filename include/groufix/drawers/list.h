/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_DRAWERS_LIST_H
#define GFX_DRAWERS_LIST_H

#include "groufix/containers/vec.h"
#include "groufix/core/renderer.h"
#include "groufix/def.h"


/**
 * Draw list index (1-based).
 */
typedef size_t GFXDrawInd;


/**
 * Sorted draw list definition.
 */
typedef struct GFXDrawList
{
	GFXVec inds;  // Stores size_t, 0-based index into items, to sort.
	GFXVec items; // Stores { size_t pos, data... }.
	size_t free;  // First free index into items (or SIZE_MAX).

	size_t numVisible;
	size_t elementSize;
	bool   dirty; // True if sorting needed.

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
	GFXDrawList list; // Base-type.

	size_t elementSize;
	size_t numSets;

} GFXRenderList;


/**
 * Retrieves the bound sets from a render list element.
 * Undefined behaviour if list is not a render list or
 * elem is not a value returned by gfx_draw_list_get (or a list->draw arg).
 */
static inline GFXSet** gfx_rdraw_list_sets(GFXDrawList* list, const void* elem)
{
	return (GFXSet**)((const char*)elem + GFX_ALIGN_UP(
		((GFXRenderList*)list)->elementSize, alignof(GFXSet*)));
}

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
 * @param numSets Must be > 0.
 * @see gfx_draw_list_init.
 *
 * Cannot use gfx_draw_list_add on list,
 * MUST use gfx_rdraw_list_add!
 *
 * The render list will sort on all leading non-NULL sets associated with each
 * element. If any set is NULL, all after will be ignored.
 */
GFX_API void gfx_rdraw_list_init(GFXRenderList* list,
                                 size_t elemSize, size_t numSets,
                                 void (*draw)(GFXRecorder*, GFXDrawList*, const void*, void*));

/**
 * Clears the content of a render list.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_rdraw_list_clear(GFXRenderList* list);

/**
 * Adds a new element to a draw list.
 * @param list Cannot be NULL.
 * @param elem May be NULL to add empty.
 * @return Non-zero on success.
 */
GFX_API GFXDrawInd gfx_draw_list_add(GFXDrawList* list, const void* elem,
                                     bool visible);

/**
 * Adds a new element to a render list.
 * @param numSets Must be <= list->numSets.
 * @param sets    Cannot be NULL if numSets > 0.
 * @see gfx_draw_list_add.
 */
GFX_API GFXDrawInd gfx_rdraw_list_add(GFXRenderList* list, const void* elem,
                                      size_t numSets, GFXSet** sets,
                                      bool visible);

/**
 * Erases an element from a draw list.
 * @param list Cannot be NULL.
 * @param ind  Must be a non-zero value returned by gfx_draw_list_add.
 */
GFX_API void gfx_draw_list_erase(GFXDrawList* list, GFXDrawInd ind);

/**
 * Retrieves the element data from a draw list index.
 * @param list Cannot be NULL.
 * @param ind  Must be a non-zero value returend by gfx_draw_list_add.
 */
GFX_API void* gfx_draw_list_get(GFXDrawList* list, GFXDrawInd ind);

/**
 * Retrieves whether an element of a draw list is visible or not.
 * @param list Cannot be NULL.
 * @param ind  Must be a non-zero value returned by gfx_draw_list_add.
 */
GFX_API bool gfx_draw_list_is_visible(GFXDrawList* list, GFXDrawInd ind);

/**
 * Sets the visibility of an element of a draw list.
 * @param list Cannot be NULL.
 * @param ind  Must be a non-zero value returend by gfx_draw_list_add.
 */
GFX_API void gfx_draw_list_set_visible(GFXDrawList* list, GFXDrawInd ind,
                                       bool visible);

/**
 * Sets the visibility of all elements of a draw list.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_draw_list_reset_visible(GFXDrawList* list, bool visible);

/**
 * Flags a draw list as dirty, forcing a re-sort.
 * Useful when the order of elements changes when element data is modified.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_draw_list_dirty(GFXDrawList* list);

/**
 * Sort all visible elements of a draw list.
 * Automatically called by gfx_cmd_draw_list.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_draw_list_sort(GFXDrawList* list);

/**
 * Proxy command to draw a draw list.
 * @param recorder May be NULL if no actual recording commands are issued.
 * @param list     Cannot be NULL.
 * @param ptr      User pointer as last argument of the draw function.
 *
 * Note: passing NULL as recorder is undefined behaviour if any call
 * to the draw function does issue actual recording commands.
 */
GFX_API void gfx_cmd_draw_list(GFXRecorder* recorder,
                               GFXDrawList* list, void* ptr);


#endif
