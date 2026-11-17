/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/drawers/lists.h"
#include <string.h>


// Retrieve the position (into inds OR next item) from an item.
#define GFX_GET_POSITION_(item) (*(size_t*)item)

// Retrieve the element data from an item.
#define GFX_GET_ELEMENT_(item) \
	(void*)((char*)item + \
		GFX_ALIGN_UP(sizeof(size_t), alignof(max_align_t)))

// Helper to directly get element data from position into inds.
#define GFX_GET_ELEMENT_FROM_POS_(list, pos) \
	GFX_GET_ELEMENT_( \
		gfx_vec_at(&(list)->items, *(size_t*)gfx_vec_at(&(list)->inds, pos)))


/****************************
 * Swaps the positions of two items.
 */
static void gfx_cull_list_swap_(GFXCullList* list, size_t lPos, size_t rPos)
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
 * Construct sort implementation.
 */
#define GFX_SORT_LIST_T_ GFXDrawList*
#define GFX_SORT_ELEM_T_ void*

#define GFX_SORT_GET_ELEM_(list, index) \
	GFX_GET_ELEMENT_FROM_POS_(&list->base, index)

#define GFX_SORT_CMP_LIST_(list, lElem, rElem) \
	list->cmp(list, lElem, rElem)

#define GFX_SORT_SWAP_(list, lIndex, rIndex) \
	gfx_cull_list_swap_(&list->base, lIndex, rIndex)

#include "groufix/sort.h"


/****************************
 * GFXRenderList implementation of the cmp function.
 */
static int gfx_render_list_cmp_(GFXDrawList* list, const void* l, const void* r)
{
	const size_t numSets = ((GFXRenderList*)list)->numSets;
	GFXSet** lSets = gfx_render_list_sets(list, l);
	GFXSet** rSets = gfx_render_list_sets(list, r);

	// numSets is guaranted to be > 0 as per gfx_render_list_init.
	if (lSets[0] == NULL || rSets[0] == NULL)
	{
		// First index is missing sets, put fewer sets used first.
		return lSets[0] == NULL ? (rSets[0] == NULL ? 0 : -1) : 1;
	}

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
			const uintptr_t lL = (uintptr_t)gfx_set_layout(lSets[s]);
			const uintptr_t lR = (uintptr_t)gfx_set_layout(rSets[s]);

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
GFX_API void gfx_cull_list_init(GFXCullList* list, size_t elemSize)
{
	assert(list != NULL);
	assert(elemSize > 0);

	gfx_vec_init(&list->inds, sizeof(size_t));
	gfx_vec_init(&list->items,
		GFX_ALIGN_UP(sizeof(size_t), alignof(max_align_t)) +
		GFX_ALIGN_UP(elemSize, alignof(max_align_t)));

	list->free = SIZE_MAX;

	list->numVisible = 0;
	list->elementSize = elemSize;
	list->dirty = 0;
}

/****************************/
GFX_API void gfx_cull_list_clear(GFXCullList* list)
{
	assert(list != NULL);

	gfx_vec_clear(&list->inds);
	gfx_vec_clear(&list->items);

	list->free = SIZE_MAX;
	list->numVisible = 0;
	list->dirty = 0;
}

/****************************/
GFX_API void gfx_draw_list_init(GFXDrawList* list, size_t elemSize,
                                void (*draw)(GFXRecorder*, GFXDrawList*, const void*, void*),
                                int (*cmp)(GFXDrawList*, const void*, const void*))
{
	assert(list != NULL);
	assert(elemSize > 0);
	assert(draw != NULL);

	gfx_cull_list_init(&list->base, elemSize);

	list->draw = draw;
	list->cmp = cmp;
}

/****************************/
GFX_API void gfx_draw_list_clear(GFXDrawList* list)
{
	assert(list != NULL);

	gfx_cull_list_clear(&list->base);
}

/****************************/
GFX_API void gfx_render_list_init(GFXRenderList* list, size_t elemSize,
                                  size_t numSets, size_t numDynamics, uint32_t pushSize,
                                  void (*draw)(GFXRecorder*, GFXDrawList*, const void*, void*))
{
	assert(list != NULL);
	assert(numSets > 0);
	assert(pushSize % 4 == 0);
	assert(draw != NULL);

	const size_t eSize =
		GFX_ALIGN_UP(elemSize, alignof(max_align_t)) +
		GFX_ALIGN_UP(sizeof(GFXRenderable), alignof(max_align_t)) +
		GFX_ALIGN_UP(sizeof(GFXSet*) * numSets, alignof(max_align_t)) +
		GFX_ALIGN_UP(sizeof(uint32_t) * numDynamics, alignof(max_align_t)) +
		pushSize;

	gfx_draw_list_init(
		&list->base, eSize, draw, gfx_render_list_cmp_);

	list->elementSize = elemSize;
	list->numSets = numSets;
	list->numDynamics = numDynamics;
	list->pushSize = pushSize;
}

/****************************/
GFX_API void gfx_render_list_clear(GFXRenderList* list)
{
	assert(list != NULL);

	gfx_draw_list_clear(&list->base);
}

/****************************/
GFX_API GFXCullInd gfx_cull_list_add(GFXCullList* list, const void* elem,
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
	gfx_cull_list_set_visible(list, *ind + 1, visible);

	return *ind + 1;
}

/****************************/
GFX_API GFXCullInd gfx_render_list_add(GFXRenderList* list, const void* elem,
                                       const GFXRenderable* renderable,
                                       size_t numSets, GFXSet** sets,
                                       size_t numDynamics, const uint32_t* offsets,
                                       size_t pushSize, const void* push,
                                       bool visible)
{
	assert(list != NULL);
	assert(numSets <= list->numSets);
	assert(numSets == 0 || sets != NULL);
	assert(numDynamics <= list->numDynamics);
	assert(numDynamics == 0 || offsets != NULL);
	assert(pushSize % 4 == 0);
	assert(pushSize <= list->pushSize);
	assert(pushSize == 0 || push != NULL);

	// Insert empty element.
	GFXCullInd ind = gfx_cull_list_add(&list->base.base, NULL, visible);
	if (ind == 0) return 0;

	// Copy the element data & all given values.
	void* iElem = gfx_cull_list_get(&list->base.base, ind);
	GFXSet** iSets = gfx_render_list_sets(&list->base, iElem);
	uint32_t* iDyns = gfx_render_list_dynamics(&list->base, iElem);
	void* iPush = gfx_render_list_push(&list->base, iElem);

	if (renderable != NULL)
		*gfx_render_list_renderable(&list->base, iElem) = *renderable;

	if (elem != NULL && list->elementSize > 0)
		memcpy(iElem, elem, list->elementSize);

	if (numSets > 0)
		memcpy(iSets, sets, sizeof(GFXSet*) * numSets);
	if (numDynamics > 0)
		memcpy(iDyns, offsets, sizeof(uint32_t) * numDynamics);
	if (pushSize > 0)
		memcpy(iPush, push, pushSize);

	// Set all remaining values.
	while (numSets < list->numSets)
		iSets[numSets++] = NULL;

	while (numDynamics < list->numDynamics)
		iDyns[numDynamics++] = 0;

	if (pushSize < list->pushSize)
		memset((char*)iPush + pushSize, 0, list->pushSize - pushSize);

	return ind;
}

/****************************/
GFX_API void gfx_cull_list_erase(GFXCullList* list, GFXCullInd ind)
{
	assert(list != NULL);
	assert(ind != 0);

	// First move its index to the end of inds with swaps,
	// this way we don't have to fix a lot of item positions.
	// Make sure it's invisible.
	gfx_cull_list_set_visible(list, ind, 0);

	// Now it's invisible, move it to the end of all invisibles.
	void* item = gfx_vec_at(&list->items, ind-1);
	size_t pos = GFX_GET_POSITION_(item);

	if (pos != list->inds.size - 1)
		gfx_cull_list_swap_(list, pos, list->inds.size - 1);

	// Now we can pop the index.
	gfx_vec_pop(&list->inds, 1);

	// And add the item to the free chain.
	// To do so, use its position to point to the next free item index.
	GFX_GET_POSITION_(item) = list->free;
	list->free = ind-1;
}

/****************************/
GFX_API void* gfx_cull_list_get(GFXCullList* list, GFXCullInd ind)
{
	assert(list != NULL);
	assert(ind != 0);

	return GFX_GET_ELEMENT_(gfx_vec_at(&list->items, ind-1));
}

/****************************/
GFX_API bool gfx_cull_list_is_visible(GFXCullList* list, GFXCullInd ind)
{
	assert(list != NULL);
	assert(ind != 0);

	size_t pos = GFX_GET_POSITION_(gfx_vec_at(&list->items, ind-1));
	return pos < list->numVisible;
}

/****************************/
GFX_API void gfx_cull_list_set_visible(GFXCullList* list, GFXCullInd ind,
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
		gfx_cull_list_swap_(list, pos, newPos);

	list->dirty = 1;
}

/****************************/
GFX_API void gfx_cull_list_reset_visible(GFXCullList* list, bool visible)
{
	assert(list != NULL);

	const size_t numVisible = list->numVisible;

	list->numVisible = visible ? list->inds.size : 0;
	if (numVisible != list->numVisible) list->dirty = 1;
}

/****************************/
GFX_API void* gfx_cull_list_at(GFXCullList* list, size_t index)
{
	assert(list != NULL);
	assert(index < list->numVisible);

	return GFX_GET_ELEMENT_FROM_POS_(list, index);
}

/****************************/
GFX_API void gfx_draw_list_sort(GFXDrawList* list)
{
	assert(list != NULL);

	// Ignore if we have no comparison function, skip if not dirty.
	if (list->cmp && list->base.dirty)
		gfx_sort_(list, 0, list->base.numVisible);

	list->base.dirty = 0;
}

/****************************/
GFX_API void gfx_cmd_draw_list(GFXRecorder* recorder,
                               GFXDrawList* list, void* ptr)
{
	assert(recorder != NULL);
	assert(list != NULL);

	// First sort all indices.
	gfx_draw_list_sort(list);

	// Loop over all sorted indices & draw each element.
	for (size_t p = 0; p < list->base.numVisible; ++p)
	{
		const void* elem = GFX_GET_ELEMENT_FROM_POS_(&list->base, p);
		list->draw(recorder, list, elem, ptr);
	}
}

/****************************/
GFX_API void gfx_cmd_bind_render_list(GFXRecorder* recorder,
                                      GFXDrawList* list, const void* elem)
{
	assert(recorder != NULL);
	assert(list != NULL);
	assert(elem != NULL);

	GFXRenderList* rlist = (GFXRenderList*)list;

	// numSets is guaranted to be > 0 as per gfx_render_list_init.
	gfx_cmd_bind(recorder,
		gfx_render_list_renderable(list, elem)->technique,
		0, rlist->numSets, rlist->numDynamics,
		gfx_render_list_sets(list, elem),
		gfx_render_list_dynamics(list, elem));
}

/****************************/
GFX_API void gfx_cmd_push_render_list(GFXRecorder* recorder,
                                      GFXDrawList* list, const void* elem)
{
	assert(recorder != NULL);
	assert(list != NULL);
	assert(elem != NULL);

	GFXRenderList* rlist = (GFXRenderList*)list;

	if (rlist->pushSize > 0)
		gfx_cmd_push(recorder,
			gfx_render_list_renderable(list, elem)->technique,
			0, rlist->pushSize,
			gfx_render_list_push(list, elem));
}
