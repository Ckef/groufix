/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_GRAPH_NODE_H
#define GFX_GRAPH_NODE_H

#include "groufix/containers/dict.h"
#include "groufix/graph/props.h"
#include "groufix/def.h"


/**
 * Node property definition.
 */
typedef struct GFXNode
{
	GFXProperty prop;       // Base-type.
	GFXDict     properties; // Stores string : GFXProperty*.

	GFXLinkProperty parent;
	GFXListProperty children; // Stores GFXNode*.
	GFXFuncProperty update;

} GFXNode;


/**
 * Spatial node property definition.
 */
typedef struct GFXSpatialNode
{
	GFXNode node; // Base-type.

	// Spatial matrix.
	struct
	{
		GFXValueProperty prop;
		float            value[16]; // TODO: Prolly use cglm or sm?

	} matrix;

} GFXSpatialNode;


/**
 * Initializes a node.
 * @param node Cannot be NULL.
 */
GFX_API void gfx_node_init(GFXNode* node);

/**
 * Initializes a spatial node.
 * @param node Cannot be NULL.
 */
GFX_API void gfx_snode_init(GFXSpatialNode* node);

/**
 * Clears a node, invalidating the contents of `node`.
 * @param node Cannot be NULL.
 */
GFX_API void gfx_node_clear(GFXNode* node);

/**
 * Clears a spatial node, invalidating the contents of `node`.
 * @param node Cannot be NULL.
 */
GFX_API void gfx_snode_clear(GFXSpatialNode* node);

/**
 * Sets the property of a node.
 * @param node Cannot be NULL.
 * @param prop May be NULL to erase.
 * @param key  Cannot be NULL, must be NULL-terminated.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_node_set(GFXNode* node, GFXProperty* prop, const char* key);

/**
 * Gets a property of a node.
 * @param node Cannot be NULL.
 * @param key  Cannot be NULL, must be NULL-terminated.
 */
GFX_API GFXProperty* gfx_node_get(GFXNode* node, const char* key);


#endif
