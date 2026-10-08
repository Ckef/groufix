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
 * GFXNode.name.prop setter implementation.
 */
static bool gfx_node_name_set_(GFXValueProperty* prop, const void* values)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, name.prop);

	return gfx_node_set_name(node, values);
}

/****************************
 * GFXNode.parent setter implementation.
 */
static bool gfx_node_parent_set_(GFXLinkProperty* prop, GFXProperty* follow)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, parent);

	// Only set if it's a node.
	if (follow == NULL || follow->type == GFX_PROP_NODE)
		return gfx_node_set_parent(node, (GFXNode*)follow);

	return 0;
}

/****************************
 * GFXNode.children setter implementation.
 */
static bool gfx_node_children_set_(GFXListProperty* prop, GFXProperty* item, size_t index)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, children);

	// Add a child.
	if (
		item != NULL && item->type == GFX_PROP_NODE &&
		index == prop->items.size)
	{
		return gfx_node_set_parent((GFXNode*)item, node);
	}

	// Remove a child.
	if (
		item == NULL &&
		index < prop->items.size)
	{
		GFXProperty* child =
			gfx_list_prop_at(prop, index);

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
			// If different parent, just erase the item.
			gfx_list_prop_erase(prop, index);
		}

		return 1;
	}

	// No reassigning childs!
	return 0;
}

/****************************
 * Frees any memory the string name from a GFXNode may hold.
 * Leaves all values of node.name!
 */
static inline void gfx_node_name_free_(GFXNode* node)
{
	// If not pointing to node->name.str,
	// it must be manually allocated, free it!
	if (node->name.prop.values != node->name.str)
		// Can pass NULL.
		free(node->name.prop.values);
}

/****************************/
GFX_API bool gfx_node_init(GFXNode* node, const char* name)
{
	assert(node != NULL);

	// First set name, may need to allocate.
	// Set name property values pointer to avoid free call.
	node->name.prop.values = node->name.str;

	if (!gfx_node_set_name(node, name))
		return 0;

	// Initialize the rest of the node.
	node->prop.type = GFX_PROP_NODE;
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

	// Initialize matrix value property.
	const size_t numFloats =
		sizeof(node->matrix.values) / sizeof(float);

	gfx_float_prop(
		&node->matrix.prop, numFloats, node->matrix.values, NULL);

	for (size_t i = 0; i < numFloats; ++i)
		node->matrix.values[i] = 0.0f;

	// Set all properties.
	gfx_node_set(&node->node, &node->matrix.prop.prop, "matrix");

	return 1;
}

/****************************/
GFX_API void gfx_snode_clear(GFXSpatialNode* node)
{
	assert(node != NULL);

	gfx_node_clear(&node->node);

	// Leave all values, node is invalidated.
}

/****************************/
GFX_API bool gfx_node_set_name(GFXNode* node, const char* name)
{
	assert(node != NULL);

	// NULL equals empty string.
	if (name == NULL) name = "";

	const size_t nameLen = strlen(name);

	if (nameLen < sizeof(node->name.str))
	{
		// Copy as small string.
		gfx_node_name_free_(node);
		memcpy(node->name.str, name, nameLen + 1);

		gfx_string_prop(
			&node->name.prop, node->name.str, gfx_node_name_set_);
	}
	else
	{
		// Allocate new long string.
		char* newName = malloc(nameLen + 1);
		if (newName == NULL) return 0;

		// Free old name after successful allocation.
		gfx_node_name_free_(node);
		memcpy(newName, name, nameLen + 1);

		gfx_string_prop(
			&node->name.prop, newName, gfx_node_name_set_);
	}

	return 1;
}

/****************************/
GFX_API const char* gfx_node_get_name(GFXNode* node)
{
	assert(node != NULL);

	return node->name.prop.values;
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
	gfx_link_prop(
		&node->parent, (GFXProperty*)parent, gfx_node_parent_set_);

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

/****************************/
GFX_API GFXNode* gfx_node_get_child(GFXNode* node, size_t index)
{
	assert(node != NULL);
	assert(index < node->children.items.size);

	GFXProperty* child = gfx_list_prop_at(&node->children, index);

	return (child->type == GFX_PROP_NODE) ?
		(GFXNode*)child : NULL;
}
