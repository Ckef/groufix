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
	// Name of the type.
	const char* name;

	// Type this inherits from.
	const struct GFXNodeType* inherits;


	// Dynamic allocation.
	struct
	{
		size_t size; // Size to allocate in bytes, 0 to disallow.

		// Initializes a node of this type, all args must be non-NULL.
		bool (*init)(void* node, const char* name);

		// Clears a node of this type, all args must be non-NULL.
		void (*clear)(void* node);

	} alloc;

} GFXNodeType;


#endif
