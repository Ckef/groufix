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
	GFXListProperty children;
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
		float            values[16]; // TODO: Prolly use cglm or sm?

	} matrix;

} GFXSpatialNode;


/**
 * Sets a property of a node.
 */
static inline bool gfx_node_set(GFXNode* node, GFXProperty* prop, const char* key)
{
	return gfx_dict_set(&node->properties, prop, key);
}

/**
 * Gets a property of a node.
 */
static inline GFXProperty* gfx_node_get(GFXNode* node, const char* key)
{
	return gfx_dict_get(&node->properties, key);
}

/**
 * Initializes a node.
 * @param node Cannot be NULL.
 */
GFX_API void gfx_node_init(GFXNode* node);

/**
 * Clears a node, invalidating the contents of `node`.
 * @param node Cannot be NULL.
 */
GFX_API void gfx_node_clear(GFXNode* node);

/**
 * Initializes a spatial node.
 * @param node Cannot be NULL.
 */
GFX_API void gfx_snode_init(GFXSpatialNode* node);

/**
 * Clears a spatial node, invalidating the contents of `node`.
 * @param node Cannot be NULL.
 */
GFX_API void gfx_snode_clear(GFXSpatialNode* node);


#endif
