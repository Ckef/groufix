/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/containers/list.h"
#include <assert.h>


/****************************/
GFX_API void gfx_list_init(GFXList* list)
{
	assert(list != NULL);

	list->head = NULL;
	list->tail = NULL;
}

/****************************/
GFX_API void gfx_list_clear(GFXList* list)
{
	assert(list != NULL);

	list->head = NULL;
	list->tail = NULL;

	// No need to touch any nodes, they're invalidated.
}

/****************************/
GFX_API void gfx_list_insert_after(GFXList* list, GFXListNode* node,
                                   GFXListNode* kin)
{
	assert(list != NULL);
	assert(node != NULL);

	// Select tail if no kin to append.
	kin = (kin == NULL) ? list->tail : kin;

	if (kin != NULL)
	{
		node->next = kin->next;
		node->prev = kin;

		if (kin->next != NULL)
			kin->next->prev = node;
		else
			list->tail = node;

		kin->next = node;
	}
	else
	{
		node->next = NULL;
		node->prev = NULL;

		list->head = node;
		list->tail = node;
	}
}

/****************************/
GFX_API void gfx_list_insert_before(GFXList* list, GFXListNode* node,
                                    GFXListNode* kin)
{
	assert(list != NULL);
	assert(node != NULL);

	// Select head if no kin to prepend.
	kin = (kin == NULL) ? list->head : kin;

	if (kin != NULL)
	{
		node->next = kin;
		node->prev = kin->prev;

		if (kin->prev != NULL)
			kin->prev->next = node;
		else
			list->head = node;

		kin->prev = node;
	}
	else
	{
		node->next = NULL;
		node->prev = NULL;

		list->head = node;
		list->tail = node;
	}
}

/****************************/
GFX_API void gfx_list_erase(GFXList* list, GFXListNode* node)
{
	assert(list != NULL);
	assert(node != NULL);

	if (node->prev != NULL)
		node->prev->next = node->next;
	else
		list->head = node->next;

	if (node->next != NULL)
		node->next->prev = node->prev;
	else
		list->tail = node->prev;

	// The node itself is invalidated.
}
