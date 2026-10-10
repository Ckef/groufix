/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/graph/node.h"
#include <stdlib.h>
#include <string.h>


/****************************
 * Frees any memory a GFXNode.name may hold.
 * Leaves all values of node.name!
 */
static inline void gfx_node_name_free_(GFXNode* node)
{
	// If non-NULL and not pointing to node->name.str,
	// it must be manually allocated, free it!
	if (
		node->name.prop.values != NULL &&
		node->name.prop.values != node->name.str)
	{
		// Can pass NULL.
		free(node->name.prop.values);
	}
}

/****************************
 * GFXNode.name.prop setter implementation.
 */
static bool gfx_node_name_set_(GFXValueProperty* prop, const void* values)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, name.prop);
	const char* name = values;

	// NULL equals empty string.
	if (name == NULL) name = "";

	const size_t nameLen = strlen(name);

	if (nameLen < sizeof(node->name.str))
	{
		// Copy as small string.
		gfx_node_name_free_(node);
		memcpy(node->name.str, name, nameLen + 1);

		node->name.prop.count = nameLen;
		node->name.prop.values = node->name.str;
	}
	else
	{
		// Allocate new long string.
		char* newName = malloc(nameLen + 1);
		if (newName == NULL) return 0;

		// Free old name after successful allocation.
		gfx_node_name_free_(node);
		memcpy(newName, name, nameLen + 1);

		node->name.prop.count = nameLen;
		node->name.prop.values = newName;
	}

	return 1;
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
 * GFXSpatialNode.mlocal.prop setter implementation.
 */
static bool gfx_snode_mlocal_set_(GFXValueProperty* prop, const void* values)
{
	// TODO: Update global matrix. Or set dirty flag?

	// Just directly copy according to the property.
	memcpy(prop->values, values, sizeof(float) * prop->count);

	return 1;
}

/****************************/
GFX_API bool gfx_node_init(GFXNode* node, const char* name)
{
	assert(node != NULL);

	node->prop.type = GFX_PROP_NODE;

	// First initialize name, may need to allocate.
	// Set empty name first so we can call the setter.
	gfx_string_prop(&node->name.prop, "", gfx_node_name_set_);

	if (!gfx_node_name_set_(&node->name.prop, name))
		return 0;

	// Initialize the rest of the node.
	gfx_sdict_init(&node->properties);
	gfx_link_prop(&node->parent, NULL, gfx_node_parent_set_);
	gfx_list_prop_init(&node->children, gfx_node_children_set_);
	gfx_func_prop(&node->update, &node->prop, NULL);

	// Set all properties.
	gfx_node_set(node, &node->name.prop.prop, "name");
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
	gfx_node_name_free_(node);
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

	// Initialize global matrix property.
	{
		const size_t numFloats =
			sizeof(node->mglobal.values) / sizeof(float);
		gfx_float_prop(
			&node->mglobal.prop, numFloats, node->mglobal.values, NULL);

		for (size_t i = 0; i < numFloats; ++i)
			node->mglobal.values[i] = 0.0f;
	}

	// Initialize local matrix property.
	{
		const size_t numFloats =
			sizeof(node->mlocal.values) / sizeof(float);
		gfx_float_prop(
			&node->mlocal.prop, numFloats, node->mlocal.values,
			gfx_snode_mlocal_set_);

		for (size_t i = 0; i < numFloats; ++i)
			node->mlocal.values[i] = 0.0f;
	}

	// TODO: Override parent & children setters for global matrix.

	// Set all properties.
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
