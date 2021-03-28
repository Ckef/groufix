/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CONTAINERS_LIST_H
#define GFX_CONTAINERS_LIST_H

#include "groufix/def.h"
#include <stddef.h>


/**
 * Mix-in list node definition.
 */
typedef struct GFXListNode
{
	struct GFXListNode* next;
	struct GFXListNode* prev;

} GFXListNode;


/**
 * List (doubly linked) definition.
 */
typedef struct GFXList
{
	GFXListNode* head;
	GFXListNode* tail;

} GFXList;


/**
 * Get pointer to a struct (element) from pointer to its GFXListNode member.
 * Defined as follows:
 * struct Type { ... GFXListNode node; ... };
 * ...
 * struct Type elem;
 * assert(&elem == GFX_LIST_ELEM(&elem->node, struct Type, node))
 */
#define GFX_LIST_ELEM(node, type_, member_) \
	((type_*)((char*)(node) - offsetof(type_, member_)))


/**
 * Initializes a list.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_list_init(GFXList* list);

/**
 * Clears a list, erasing all nodes.
 * @param list Cannot be NULL.
 */
GFX_API void gfx_list_clear(GFXList* list);

/**
 * Initializes and inserts a new node after a given list's node.
 * @param list Cannot be NULL.
 * @param node To insert, cannot be NULL, cannot already be in a list.
 * @param kin  To insert after, must be in list, or NULL to append to the list.
 */
GFX_API void gfx_list_insert_after(GFXList* list, GFXListNode* node,
                                   GFXListNode* kin);

/**
 * Initializes and inserts a new node before a given list's node.
 * @see gfx_list_insert_after, but we prepend instead of append.
 */
GFX_API void gfx_list_insert_before(GFXList* list, GFXListNode* node,
                                    GFXListNode* kin);

/**
 * Erases a node from a list.
 * @param list Cannot be NULL.
 * @param node Must be in list.
 */
GFX_API void gfx_list_erase(GFXList* list, GFXListNode* node);


#endif
