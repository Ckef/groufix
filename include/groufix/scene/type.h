/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_SCENE_TYPE_H
#define GFX_SCENE_TYPE_H

#include "groufix/def.h"


/**
 * Node meta-type definition.
 */
typedef struct GFXNodeType
{
	// Name of the meta-type.
	const char* name;

	// Type this inherits from.
	const struct GFXNodeType* inherits;


	// Dynamic allocation.
	struct
	{
		size_t size; // Size to allocate in bytes, 0 to disallow.

		// Initializes a node of this type.
		//  name Must be NULL-terminated or NULL for empty string.
		bool (*init)(void* node, const char* name, const struct GFXNodeType* type);

		// Clears a node of this type.
		void (*clear)(void* node, const struct GFXNodeType *type);

	} alloc;

} GFXNodeType;


/**
 * Initializes a node from meta-type.
 * The object pointed to by node cannot be moved or copied!
 * @param node Cannot be NULL, must be >= type->alloc.size bytes.
 * @param name Must be NULL-terminated or NULL for empty string.
 */
static inline bool gfx_node_tinit(void* node, const char* name, const GFXNodeType* type)
{
	assert(node != NULL);
	assert(type != NULL);

	return (type->alloc.size > 0 && type->alloc.init) ?
		type->alloc.init(node, name, type) : 0;
}

/**
 * Clears a node from meta-type, invalidating the contents of `node`.
 * @param node Cannot be NULL, must be >= type->alloc.size bytes.
 */
static inline void gfx_node_tclear(void* node, const GFXNodeType* type)
{
	assert(node != NULL);
	assert(type != NULL);

	if (type->alloc.size > 0 && type->alloc.clear)
		type->alloc.clear(node, type);
}


#endif
