/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include "groufix/drawers/list.h"
#include <string.h>


// Retrieve the position (into inds OR next item) from a draw item.
#define GFX_GET_POSITION_(item) (*(size_t*)item)

// Retrieve the element data from a draw item.
#define GFX_GET_ELEMENT_(item) \
	(void*)((char*)item + \
		GFX_ALIGN_UP(sizeof(size_t), alignof(max_align_t)))

// Helper to directly get element data from position into inds.
#define GFX_GET_ELEMENT_FROM_POS_(list, pos) \
	GFX_GET_ELEMENT_( \
		gfx_vec_at(&list->items, *(size_t*)gfx_vec_at(&list->inds, pos)))


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

/****************************
 * Quicksort indices with fat partitioning.
 * Assumes list->cmp != NULL.
 */
static void gfx_draw_list_qsort_(GFXDrawList* list, size_t l, size_t r)
{
	// Use the middle as pivot.
	const void* pivElem =
		GFX_GET_ELEMENT_FROM_POS_(list, l + ((r - l) >> 1));

	// Perform partition.
	size_t lt = l;
	size_t eq = l;
	size_t gt = r;

	while (eq < gt)
	{
		const void* eqElem =
			GFX_GET_ELEMENT_FROM_POS_(list, eq);

		if (eqElem == pivElem)
			eq = eq + 1; // Pivot element, skip comparison function.
		else
		{
			const int cmp = list->cmp(list, eqElem, pivElem);

			if (cmp < 0)
			{
				if (eq != lt)
					gfx_draw_list_swap_(list, eq, lt);

				lt = lt + 1;
				eq = eq + 1;
			}
			else if (cmp > 0)
			{
				gt = gt - 1;

				if (eq != gt)
					gfx_draw_list_swap_(list, eq, gt);
			}
			else
			{
				eq = eq + 1;
			}
		}
	}

	// Recurse, skip if only 1 element.
	if (l + 1 < lt)
		gfx_draw_list_qsort_(list, l, lt);
	if (gt + 1 < r)
		gfx_draw_list_qsort_(list, gt, r);
}

/****************************
 * GFXRenderList implementation of the cmp function.
 */
static int gfx_rdraw_list_cmp_(GFXDrawList* list, const void* l, const void* r)
{
	const size_t numSets = ((GFXRenderList*)list)->numSets;
	GFXSet** lSets = gfx_rdraw_list_sets(list, l);
	GFXSet** rSets = gfx_rdraw_list_sets(list, r);

	for (size_t s = 0; s < numSets; ++s)
	{
		// Use integer pointer values to compare sets.
		// We can do so as the order does not matter;
		// it only matters the values uniquely identify the sets.

		// Note: Assumes (uintptr_t)p == (uintptr_t)p, where p is a void*.
		// This holds true on most 32 and 64 bit architectures!
		const uintptr_t vL = (uintptr_t)(void*)lSets[s];
		const uintptr_t vR = (uintptr_t)(void*)rSets[s];

		if ((s+1) == numSets || lSets[s+1] == NULL || rSets[s+1] == NULL)
		{
			// Unless this is the last index to compare,
			// then we first sort on set layout, for minimal pipeline swaps.
			// Still sort on set afterwards, in case they're equal.

			// Note: we rely on the internal core object of a set
			// to get a pointer to its layout (!).
			const uintptr_t lL = (uintptr_t)(void*)lSets[s]->setLayout;
			const uintptr_t lR = (uintptr_t)(void*)rSets[s]->setLayout;

			if (lL < lR) return -1;
			if (lL > lR) return 1;

			if (vL < vR) return -1;
			if (vL > vR) return 1;

			// If equal so far, put fewer sets used first.
			return (s+1) == numSets ? 0 :
				lSets[s+1] == NULL ? (rSets[s+1] == NULL ? 0 : -1) : 1;
		}

		if (vL < vR) return -1;
		if (vL > vR) return 1;
	}

	return 0;
}

/****************************/
GFX_API void gfx_draw_list_init(GFXDrawList* list, size_t elemSize,
                                void (*draw)(GFXRecorder*, GFXDrawList*, const void*, void*),
                                int (*cmp)(GFXDrawList*, const void*, const void*))
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
GFX_API void gfx_rdraw_list_init(GFXRenderList* list,
                                 size_t elemSize, size_t numSets,
                                 void (*draw)(GFXRecorder*, GFXDrawList*, const void*, void*))
{
	assert(list != NULL);
	assert(elemSize > 0);
	assert(numSets > 0);
	assert(draw != NULL);

	const size_t eSize =
		GFX_ALIGN_UP(elemSize, alignof(GFXSet*)) +
		sizeof(GFXSet*) * numSets;

	gfx_draw_list_init(
		&list->list, eSize, draw, gfx_rdraw_list_cmp_);

	list->elementSize = elemSize;
	list->numSets = numSets;
}

/****************************/
GFX_API void gfx_rdraw_list_clear(GFXRenderList* list)
{
	assert(list != NULL);

	gfx_draw_list_clear(&list->list);
}

/****************************/
GFX_API GFXDrawInd gfx_draw_list_add(GFXDrawList* list, const void* elem,
                                     bool visible)
{
	assert(list != NULL);

	// First try to push a new index.
	if (!gfx_vec_push(&list->inds, 1, NULL))
		return 0;

	const size_t pos = list->inds.size - 1;
	size_t* ind = gfx_vec_at(&list->inds, pos);

	// See if there are free items.
	*ind = list->free;

	if (*ind != SIZE_MAX)
		// If there are, claim the first.
		// The next free item's index is stored in the first's position.
		list->free = GFX_GET_POSITION_(gfx_vec_at(&list->items, *ind));
	else
	{
		// If none free, push a new one.
		*ind = list->items.size;

		if (!gfx_vec_push(&list->items, 1, NULL))
		{
			gfx_vec_pop(&list->inds, 1);
			return 0;
		}
	}

	// We have an item, set its position.
	void* item = gfx_vec_at(&list->items, *ind);
	GFX_GET_POSITION_(item) = pos;

	// Copy the element data.
	if (elem != NULL) memcpy(
		GFX_GET_ELEMENT_(item), elem, list->elementSize);

	// Set visibility & return.
	// We return the 1-based index, 0 is considered an error.
	gfx_draw_list_set_visible(list, *ind + 1, visible);

	return *ind + 1;
}

/****************************/
GFX_API GFXDrawInd gfx_rdraw_list_add(GFXRenderList* list, const void* elem,
                                      size_t numSets, GFXSet** sets,
                                      bool visible)
{
	assert(list != NULL);
	assert(numSets <= list->numSets);
	assert(numSets == 0 || sets != NULL);

	// Insert empty element.
	GFXDrawInd ind = gfx_draw_list_add(&list->list, NULL, visible);
	if (ind == 0) return 0;

	// Copy the element data & all given sets.
	void* iElem = gfx_draw_list_get(&list->list, ind);
	GFXSet** iSets = gfx_rdraw_list_sets(&list->list, iElem);

	if (elem != NULL) memcpy(iElem, elem, list->elementSize);
	if (numSets > 0) memcpy(iSets, sets, sizeof(GFXSet*) * numSets);

	// Set all remaining sets to NULL.
	while (numSets < list->numSets)
		iSets[numSets++] = NULL;

	return ind;
}

/****************************/
GFX_API void gfx_draw_list_erase(GFXDrawList* list, GFXDrawInd ind)
{
	assert(list != NULL);
	assert(ind != 0);

	// First move its index to the end of inds with swaps,
	// this way we don't have to fix a lot of item positions.
	// Make sure it's invisible.
	gfx_draw_list_set_visible(list, ind, 0);

	// Now it's invisible, move it to the end of all invisibles.
	void* item = gfx_vec_at(&list->items, ind-1);
	size_t pos = GFX_GET_POSITION_(item);

	if (pos != list->inds.size - 1)
		gfx_draw_list_swap_(list, pos, list->inds.size - 1);

	// Now we can pop the index.
	gfx_vec_pop(&list->inds, 1);

	// And add the item to the free chain.
	// To do so, use its position to point to the next free item index.
	GFX_GET_POSITION_(item) = list->free;
	list->free = ind-1;
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

	// Only flag as dirty if there are visible items.
	// Also, ignore if we have no comparison function.
	list->dirty = (list->cmp && list->numVisible > 0) ? 1 : 0;
}

/****************************/
GFX_API void gfx_draw_list_sort(GFXDrawList* list)
{
	assert(list != NULL);

	if (list->dirty)
	{
		// We choose to strictly only use quicksort, without a fallback
		// to e.g. insertion sort when the input length is small.
		// This because we expect many elements that compare equal.
		// In the case of lots of equal items, quicksort swaps way less items.

		// We can assume list->cmp != NULL, otherwise dirty was not set!
		gfx_draw_list_qsort_(list, 0, list->numVisible);

		list->dirty = 0;
	}
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
		const void* elem = GFX_GET_ELEMENT_FROM_POS_(list, p);
		list->draw(recorder, list, elem, ptr);
	}
}
