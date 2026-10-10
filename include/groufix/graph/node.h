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

	// Identifier name string.
	GFXStringProperty name;

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

	// Global matrix.
	struct
	{
		GFXValueProperty  prop;
		alignas(32) float values[16];

	} mglobal;

	// Local matrix.
	struct
	{
		GFXValueProperty  prop;
		alignas(32) float values[16];

	} mlocal;

} GFXSpatialNode;


/**
 * Sets a property of a node.
 * @param node Cannot be NULL.
 * @param key  Must be non-NULL and NULL-terminated.
 */
static inline bool gfx_node_set(GFXNode* node, GFXProperty* prop, const char* key)
{
	assert(node != NULL);
	assert(key != NULL);

	return gfx_dict_set(&node->properties, prop, key);
}

/**
 * Gets a property of a node.
 * @param node Cannot be NULL.
 * @param key  Must be non-NULL and NULL-terminated.
 */
static inline GFXProperty* gfx_node_get(GFXNode* node, const char* key)
{
	assert(node != NULL);
	assert(key != NULL);

	return gfx_dict_get(&node->properties, key);
}

/**
 * Sets the name of a node.
 * @param node Cannot be NULL.
 * @param name Must be NULL-terminated or NULL for empty string.
 */
static inline bool gfx_node_set_name(GFXNode* node, const char* name)
{
	assert(node != NULL);

	return gfx_string_prop_set(&node->name, name);
}

/**
 * Retrieves the name of a node.
 * @param node Cannot be NULL.
 * @return Never NULL, always NULL-terminated.
 *
 * Note: the returned pointer is invalidated when the name is changed.
 */
static inline const char* gfx_node_get_name(GFXNode* node)
{
	assert(node != NULL);

	return gfx_string_prop_get(&node->name);
}

/**
 * Sets the parent node of a node.
 * @param node   Cannot be NULL.
 * @param parent Must be a node or NULL.
 */
static inline bool gfx_node_set_parent(GFXNode* node, GFXNode* parent)
{
	assert(node != NULL);

	return gfx_link_prop_set(&node->parent, &parent->prop);
}

/**
 * Retrieves the parent node of a node.
 * @param node Cannot be NULL.
 */
static inline GFXNode* gfx_node_get_parent(GFXNode* node)
{
	assert(node != NULL);

	return (GFXNode*)node->parent.follow;
}

/**
 * Retrieves a child of a node.
 * @param node  Cannot be NULL.
 * @param index Must be < node->children.items.size.
 */
static inline GFXNode* gfx_node_get_child(GFXNode* node, size_t index)
{
	assert(node != NULL);
	assert(index < node->children.items.size);

	return (GFXNode*)gfx_list_prop_at(&node->children, index);
}

/**
 * Sets the update function of a node.
 * @param node Cannot be NULL.
 */
static inline void gfx_node_set_update(GFXNode* node,
                                       int (*fn)(GFXProperty*, const GFXListProperty*))
{
	assert(node != NULL);

	gfx_func_prop(&node->update, &node->prop, fn);
}

/**
 * Calls the update function of a node.
 * @param node Cannot be NULL.
 * @return The function's return, or zero when set to NULL.
 */
static inline int gfx_node_update(GFXNode* node, const GFXListProperty* args)
{
	assert(node != NULL);

	return gfx_func_prop_call(&node->update, args);
}

/**
 * Initializes a node.
 * @param node Cannot be NULL.
 * @param name Must be NULL-terminated or NULL for empty string.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_node_init(GFXNode* node, const char* name);

/**
 * Clears a node, invalidating the contents of `node`.
 * Will unlink itself from its parent and children.
 * @param node Cannot be NULL.
 */
GFX_API void gfx_node_clear(GFXNode* node);

/**
 * Initializes a spatial node.
 * @param node Cannot be NULL.
 * @see gfx_node_init.
 */
GFX_API bool gfx_snode_init(GFXSpatialNode* node, const char* name);

/**
 * Clears a spatial node, invalidating the contents of `node`.
 * @param node Cannot be NULL.
 * @see gfx_node_clear.
 */
GFX_API void gfx_snode_clear(GFXSpatialNode* node);


#endif
