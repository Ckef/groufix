/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/graph/node.h"
#include <string.h>


/****************************
 * Recursively set the dirty flag of a node's children.
 * Will stop traversing children once it cannot find a dirty flag.
 */
static void gfx_node_children_set_dirty_(GFXNode* node)
{
	// Loop over all its children.
	for (size_t i = 0; i < node->children.items.size; ++i)
	{
		GFXNode* child =
			gfx_node_get_child(node, i);
		GFXValueProperty* dirty =
			(GFXValueProperty*)gfx_node_get(child, "dirty");

		// Check if its dirty property exists and
		// is a boolean value property of count 1.
		if (
			dirty != NULL &&
			dirty->prop.type == GFX_PROP_BOOL && dirty->count == 1)
		{
			// If so, try to set dirty.
			bool flag = 1;
			if (gfx_value_prop_set(dirty, &flag))
			{
				// If successful, recurse to its children.
				gfx_node_children_set_dirty_(child);
			}
		}
	}
}

/****************************
 * GFXNode.parent setter implementation.
 */
static bool gfx_node_parent_set_(GFXLinkProperty* prop, GFXProperty* follow)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, parent);
	GFXNode* parent = (GFXNode*)follow;

	// Validate follow is a node.
	if (follow != NULL && follow->type != GFX_PROP_NODE)
		return 0;

	// Add it as a child to its new parent.
	if (parent != NULL)
	{
		// Parent already set.
		if (node->parent.follow == follow)
			return 1;

		if (!gfx_list_prop_add(&parent->children, &node->prop))
			return 0;
	}

	// Remove it as child from its current parent.
	if (node->parent.follow != NULL)
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
	node->parent.follow = follow;

	return 1;
}

/****************************
 * GFXNode.children setter implementation.
 */
static bool gfx_node_children_set_(GFXListProperty* prop, GFXProperty* item, size_t index)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, children);
	GFXNode* child = (GFXNode*)item;

	// Validate item is a node.
	if (item != NULL && item->type != GFX_PROP_NODE)
		return 0;

	// Add a child.
	if (child != NULL && index == node->children.items.size)
	{
		return gfx_node_set_parent(child, node);
	}

	// Remove a child.
	if (child == NULL && index < node->children.items.size)
	{
		GFXNode* currChild = gfx_node_get_child(node, index);

		// Set its parent to NULL, will erase self from children.
		return gfx_node_set_parent(currChild, NULL);
	}

	// No reassigning childs!
	return 0;
}

/****************************
 * GFXSpatialNode.node.parent setter implementation.
 */
static bool gfx_snode_parent_set_(GFXLinkProperty* prop, GFXProperty* follow)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, node.parent);
	GFXProperty* prevParent = node->node.parent.follow;

	// First call the base setter.
	if (!gfx_node_parent_set_(prop, follow))
		return 0;

	// Flag this node as dirty & propagate if parent changed.
	if (follow != prevParent)
	{
		node->dirty.value = 1;
		gfx_node_children_set_dirty_(&node->node);
	}

	return 1;
}

/****************************
 * GFXSpatialNode.dirty.prop setter implementation.
 */
static bool gfx_snode_dirty_set_(GFXValueProperty* prop, const void* values)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, dirty.prop);

	node->dirty.value = *(const bool*)values;
	return 1;
}

/****************************
 * GFXSpatialNode.dirty.prop getter implementation.
 */
static const void* gfx_snode_dirty_get_(GFXValueProperty* prop)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, mlocal.prop);

	return &node->dirty.value;
}

/****************************
 * GFXSpatialNode.mglobal.prop getter implementation.
 */
static const void* gfx_snode_mglobal_get_(GFXValueProperty* prop)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, mlocal.prop);

	// TODO: Calculate global matrix if dirty flag is set.
	// Do so by checking node->dirty.value directly.
	// Then get the "mglobal" property of the parent,
	// check if it is a float property of count 16.
	// Copy its value to a local variable and
	// do the multiplication with node->mlocal.values directly.

	// Reset dirty flag.
	node->dirty.value = 0;

	return node->mglobal.values;
}

/****************************
 * GFXSpatialNode.mlocal.prop setter implementation.
 */
static bool gfx_snode_mlocal_set_(GFXValueProperty* prop, const void* values)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, mlocal.prop);

	// Directly copy to the stored values.
	memcpy(node->mlocal.values, values, sizeof(node->mlocal.values));

	// TODO: Check if the matrix value changed?
	// Flag this node as dirty & propagate.
	node->dirty.value = 1;
	gfx_node_children_set_dirty_(&node->node);

	return 1;
}

/****************************
 * GFXSpatialNode.mlocal.prop getter implementation.
 */
static const void* gfx_snode_mlocal_get_(GFXValueProperty* prop)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, mlocal.prop);

	return node->mlocal.values;
}

/****************************/
GFX_API bool gfx_node_init(GFXNode* node, const char* name)
{
	assert(node != NULL);

	node->prop.type = GFX_PROP_NODE;

	// First initialize name, may need to allocate.
	if (gfx_string_prop_init(&node->name, name) == NULL)
		return 0;

	// Initialize the rest of the node.
	gfx_sdict_init(&node->properties);
	gfx_link_prop(&node->parent, NULL, gfx_node_parent_set_);
	gfx_list_prop_init(&node->children, gfx_node_children_set_);
	gfx_func_prop(&node->update, &node->prop, NULL);

	// Set all properties.
	gfx_node_set(node, &node->name.prop, "name");
	gfx_node_set(node, &node->parent.prop, "parent");
	gfx_node_set(node, &node->children.prop, "children");
	gfx_node_set(node, &node->update.prop, "update");

	return 1;
}

/****************************/
GFX_API void gfx_node_clear(GFXNode* node)
{
	assert(node != NULL);

	// Unlink from parent & children.
	gfx_node_set_parent(node, NULL);

	for (size_t i = node->children.items.size; i > 0; --i)
	{
		GFXNode* child = gfx_node_get_child(node, i-1);

		// Set its parent to NULL, will erase self from children.
		gfx_node_set_parent(child, NULL);
	}

	// Clear all other things.
	gfx_string_prop_clear(&node->name);
	gfx_dict_clear(&node->properties);
	gfx_list_prop_clear(&node->children);

	// Leave all values, node is invalidated.
}

/****************************/
GFX_API bool gfx_snode_init(GFXSpatialNode* node, const char* name)
{
	assert(node != NULL);

	if (!gfx_node_init(&node->node, name))
		return 0;

	// Override parent setter for transform updates.
	// We can simply override the 'set' field directly!
	// Do not override the children setter; it always calls the parent setter.
	node->node.parent.set = gfx_snode_parent_set_;

	// Initialize transform dirty flag property.
	{
		gfx_bool_prop(
			&node->dirty.prop, 1,
			gfx_snode_dirty_set_,
			gfx_snode_dirty_get_);

		node->dirty.value = 0;
	}

	// Initialize global transform matrix property.
	{
		const size_t numFloats =
			sizeof(node->mglobal.values) / sizeof(float);

		gfx_float_prop(
			&node->mglobal.prop, numFloats,
			NULL,
			gfx_snode_mglobal_get_);

		for (size_t i = 0; i < numFloats; ++i)
			node->mglobal.values[i] = 0.0f;
	}

	// Initialize local transform matrix property.
	{
		const size_t numFloats =
			sizeof(node->mlocal.values) / sizeof(float);

		gfx_float_prop(
			&node->mlocal.prop, numFloats,
			gfx_snode_mlocal_set_,
			gfx_snode_mlocal_get_);

		for (size_t i = 0; i < numFloats; ++i)
			node->mlocal.values[i] = 0.0f;
	}

	// Set all properties.
	gfx_node_set(&node->node, &node->dirty.prop.prop, "dirty");
	gfx_node_set(&node->node, &node->mglobal.prop.prop, "mglobal");
	gfx_node_set(&node->node, &node->mlocal.prop.prop, "mlocal");

	return 1;
}

/****************************/
GFX_API void gfx_snode_clear(GFXSpatialNode* node)
{
	assert(node != NULL);

	gfx_node_clear(&node->node);

	// Leave all values, node is invalidated.
}
