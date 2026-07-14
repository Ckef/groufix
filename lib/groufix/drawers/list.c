/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/drawers/list.h"


// Retrieve the position (into inds OR next item) from a draw item.
#define GFX_GET_POSITION_(item) (*(size_t*)item)

// Retrieve the element data from a draw item.
#define GFX_GET_ELEMENT_(item) \
	(void*)((char*)item + \
		GFX_ALIGN_UP(sizeof(size_t), alignof(max_align_t)))


/****************************
 * Swaps the positions of two draw items.
 */
static void gfx_draw_list_swap_(GFXDrawList* list, size_t lPos, size_t rPos)
{
	size_t* lInd = gfx_vec_at(&list->inds, lPos);
	size_t* rInd = gfx_vec_at(&list->inds, rPos);

	// Swap item positions.
	GFX_GET_POSITION_(gfx_vec_at(&list->items, *lInd)) = rPos;
	GFX_GET_POSITION_(gfx_vec_at(&list->items, *rInd)) = lPos;

	// Swap indices.
	const size_t tInd = *lInd;
	*lInd = *rInd;
	*rInd = tInd;
}

/****************************/
GFX_API void gfx_draw_list_init(GFXDrawList* list, size_t elemSize,
                                void (*draw)(GFXRecorder*, const void*, void*),
                                int (*cmp)(const void*, const void*))
{
	assert(list != NULL);
	assert(elemSize > 0);
	assert(draw != NULL);

	gfx_vec_init(&list->inds, sizeof(size_t));
	gfx_vec_init(&list->items,
		GFX_ALIGN_UP(sizeof(size_t), alignof(max_align_t)) +
		GFX_ALIGN_UP(elemSize, alignof(max_align_t)));

	list->free = SIZE_MAX;

	list->numVisible = 0;
	list->elementSize = elemSize;
	list->dirty = 0;

	list->draw = draw;
	list->cmp = cmp;
}

/****************************/
GFX_API void gfx_draw_list_clear(GFXDrawList* list)
{
	assert(list != NULL);

	gfx_vec_clear(&list->inds);
	gfx_vec_clear(&list->items);

	list->free = SIZE_MAX;
	list->numVisible = 0;
	list->dirty = 0;
}

/****************************/
GFX_API GFXDrawInd gfx_draw_list_add(GFXDrawList* list, const void* elem,
                                     bool visible)
{
	assert(list != NULL);

	// TODO: Implement.

	return 0;
}

/****************************/
GFX_API void gfx_draw_list_erase(GFXDrawList* list, GFXDrawInd ind)
{
	assert(list != NULL);
	assert(ind != 0);

	// TODO: Implement.
}

/****************************/
GFX_API void* gfx_draw_list_get(GFXDrawList* list, GFXDrawInd ind)
{
	assert(list != NULL);
	assert(ind != 0);

	return GFX_GET_ELEMENT_(gfx_vec_at(&list->items, ind-1));
}

/****************************/
GFX_API bool gfx_draw_list_is_visible(GFXDrawList* list, GFXDrawInd ind)
{
	assert(list != NULL);
	assert(ind != 0);

	size_t pos = GFX_GET_POSITION_(gfx_vec_at(&list->items, ind-1));
	return pos < list->numVisible;
}

/****************************/
GFX_API void gfx_draw_list_set_visible(GFXDrawList* list, GFXDrawInd ind,
                                       bool visible)
{
	assert(list != NULL);
	assert(ind != 0);

	size_t pos = GFX_GET_POSITION_(gfx_vec_at(&list->items, ind-1));
	bool isVisible = pos < list->numVisible;

	// No change, done.
	if (isVisible == visible)
		return;

	// Reverse visibility of this item,
	// to do so, find a suitable item to swap positions with.
	size_t newPos;

	if (isVisible)
	{
		// Make it invisible, swap with the last visible item.
		newPos = list->numVisible - 1;
		--list->numVisible;
	}
	else
	{
		// Make it visible, swap with the first invisible item.
		newPos = list->numVisible;
		++list->numVisible;
	}

	// Only swap if it is not already at that position.
	if (pos != newPos)
		gfx_draw_list_swap_(list, pos, newPos);

	gfx_draw_list_dirty(list);
}

/****************************/
GFX_API void gfx_draw_list_reset_visible(GFXDrawList* list, bool visible)
{
	assert(list != NULL);

	list->numVisible = visible ? list->inds.size : 0;

	gfx_draw_list_dirty(list);
}

/****************************/
GFX_API void gfx_draw_list_dirty(GFXDrawList* list)
{
	assert(list != NULL);

	// Only flag as dirty if there are visible elements.
	// Also, ignore if we have no comparison function.
	list->dirty = (list->cmp && list->numVisible > 0) ? 1 : 0;
}

/****************************/
GFX_API void gfx_draw_list_sort(GFXDrawList* list)
{
	assert(list != NULL);

	// No sorting needed.
	if (!list->dirty) return;

	// TODO: Implement.

	list->dirty = 0;
}

/****************************/
GFX_API void gfx_cmd_draw_list(GFXRecorder* recorder,
                               GFXDrawList* list, void* ptr)
{
	assert(list != NULL);

	// First sort all indices.
	gfx_draw_list_sort(list);

	// Loop over all sorted indices & draw each element.
	for (size_t p = 0; p < list->numVisible; ++p)
	{
		const size_t ind = *(size_t*)gfx_vec_at(&list->inds, p);
		const void* elem = GFX_GET_ELEMENT_(gfx_vec_at(&list->items, ind));

		list->draw(recorder, elem, ptr);
	}
}
