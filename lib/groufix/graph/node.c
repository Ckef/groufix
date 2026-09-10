/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/graph/node.h"


/****************************
 * GFXNode.parent setter implementation.
 */
static bool gfx_node_parent_set_(GFXLinkProperty* link, GFXProperty* follow)
{
	GFXNode* node = GFX_PROP_OBJ(link, GFXNode, parent);

	// Only set if it's a node.
	if (follow == NULL || follow->type == GFX_PROP_NODE)
		return gfx_node_set_parent(node, (GFXNode*)follow);

	return 0;
}

/****************************/
GFX_API void gfx_node_init(GFXNode* node)
{
	assert(node != NULL);

	node->prop.type = GFX_PROP_NODE;
	gfx_sdict_init(&node->properties);

	gfx_link_prop(&node->parent, NULL, gfx_node_parent_set_);
	gfx_list_prop_init(&node->children);
	gfx_func_prop(&node->update, &node->prop, NULL);

	// Set all properties.
	gfx_node_set(node, &node->parent.prop, "parent");
	gfx_node_set(node, &node->children.prop, "children");
	gfx_node_set(node, &node->update.prop, "update");
}

/****************************/
GFX_API void gfx_node_clear(GFXNode* node)
{
	assert(node != NULL);

	// Unlink from parent & children.
	gfx_node_set_parent(node, NULL);

	for (size_t i = 0; i < node->children.items.size;)
	{
		GFXProperty* child =
			gfx_list_prop_at(&node->children, i);

		// Check if child is a node with this as parent.
		if (
			child->type == GFX_PROP_NODE &&
			((GFXNode*)child)->parent.follow == &node->prop)
		{
			// Set its parent to NULL, will erase self from children.
			gfx_node_set_parent((GFXNode*)child, NULL);
		}
		else
		{
			// Skip if different parent.
			++i;
		}
	}

	// Clear all other things.
	gfx_dict_clear(&node->properties);
	gfx_list_prop_clear(&node->children);

	// Leave all values, node is invalidated.
}

/****************************/
GFX_API void gfx_snode_init(GFXSpatialNode* node)
{
	assert(node != NULL);

	gfx_node_init(&node->node);

	// Initialize matrix value property.
	const size_t numFloats =
		sizeof(node->matrix.values) / sizeof(float);

	gfx_float_prop(
		&node->matrix.prop, numFloats, node->matrix.values);

	for (size_t i = 0; i < numFloats; ++i)
		node->matrix.values[i] = 0.0f;

	// Set all properties.
	gfx_node_set(&node->node, &node->matrix.prop.prop, "matrix");
}

/****************************/
GFX_API void gfx_snode_clear(GFXSpatialNode* node)
{
	assert(node != NULL);

	gfx_node_clear(&node->node);

	// Leave all values, node is invalidated.
}

/****************************/
GFX_API bool gfx_node_set_parent(GFXNode* node, GFXNode* parent)
{
	assert(node != NULL);
	assert(parent == NULL || parent->prop.type == GFX_PROP_NODE);

	// Add it as a child to its new parent.
	if (parent != NULL)
	{
		// Parent already set.
		if (node->parent.follow == &parent->prop)
			return 1;

		if (!gfx_list_prop_add(&parent->children, &node->prop))
			return 0;
	}

	// Remove it as child from its current parent.
	if (
		node->parent.follow != NULL &&
		node->parent.follow->type == GFX_PROP_NODE)
	{
		GFXNode* currParent = (GFXNode*)node->parent.follow;

		for (size_t i = currParent->children.items.size; i > 0; --i)
		{
			GFXProperty* child =
				gfx_list_prop_at(&currParent->children, i-1);

			if (child == &node->prop)
				gfx_list_prop_erase(&currParent->children, i-1);
		}
	}

	// Set new parent.
	gfx_link_prop(&node->parent, (GFXProperty*)parent, gfx_node_parent_set_);

	return 1;
}

/****************************/
GFX_API GFXNode* gfx_node_get_parent(GFXNode* node)
{
	assert(node != NULL);

	GFXProperty* parent = node->parent.follow;

	return (parent != NULL && parent->type == GFX_PROP_NODE) ?
		(GFXNode*)parent : NULL;
}
