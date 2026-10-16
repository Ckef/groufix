/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/graph/node.h"
#include "cglm/mat4.h"
#include <string.h>


/****************************
 * Checks whether or not setting a new parent of a node
 * will result in cycles in the node's tree.
 * @return Non-zero if no cycles will be created.
 */
static bool gfx_check_cycles_(GFXNode* node, GFXNode* parent)
{
	// Keep following parents.
	while (parent != NULL)
	{
		// Check if we ended up back at node.
		if (parent == node) return 0;

		// No cycle yet, check parent.
		parent = gfx_node_get_parent(parent);
	}

	return 1;
}

/****************************
 * Recursively set the dirty flag of a node's children.
 * Will stop traversing children once it cannot find or set a dirty flag.
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

	// Check for cycles.
	if (!gfx_check_cycles_(node, parent))
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
	if (!node->dirty.value && follow != prevParent)
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

	// If this node is flagged dirty, calculate new global transform matrix.
	if (node->dirty.value)
	{
		// own mglobal = parent mglobal * own mlocal.
		// See if we have a parent mglobal.
		const float* pmglobal = NULL;
		GFXNode* parent = gfx_node_get_parent(&node->node);

		if (parent != NULL)
		{
			// If we have a parent, check if its mglobal property exists
			// and is a float value property of the proper count.
			GFXValueProperty* mglobal =
				(GFXValueProperty*)gfx_node_get(parent, "mglobal");

			if (
				mglobal != NULL &&
				mglobal->prop.type == GFX_PROP_FLOAT &&
				mglobal->count == sizeof(mat4) / sizeof(float))
			{
				// If so, get its values.
				pmglobal = gfx_value_prop_get(mglobal);
			}
		}

		if (pmglobal != NULL)
		{
			// If we have parent mglobal values, calculate own mglobal.
			// pmglobal has unknown alignment, copy to local variable.
			mat4 pGlobal;
			glm_mat4_make(pmglobal, pGlobal);
			glm_mat4_mul(pGlobal,
				*(mat4*)&node->mlocal.values,
				*(mat4*)&node->mglobal.values);
		}
		else
		{
			// Simply copy mlocal to mglobal if we have no parent values.
			glm_mat4_copy(
				*(mat4*)&node->mlocal.values,
				*(mat4*)&node->mglobal.values);
		}

		// Reset dirty flag.
		node->dirty.value = 0;
	}

	return node->mglobal.values;
}

/****************************
 * GFXSpatialNode.mlocal.prop setter implementation.
 */
static bool gfx_snode_mlocal_set_(GFXValueProperty* prop, const void* values)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, mlocal.prop);

	// Compare stored & given values.
	const bool equal = memcmp(
		node->mlocal.values,
		values,
		sizeof(node->mlocal.values)) == 0;

	// Directly copy to the stored values.
	glm_mat4_make(
		values,
		*(mat4*)&node->mlocal.values);

	// Flag this node as dirty & propagate if values changed.
	if (!node->dirty.value && !equal)
	{
		node->dirty.value = 1;
		gfx_node_children_set_dirty_(&node->node);
	}

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
	gfx_bool_prop(
		&node->dirty.prop, 1,
		gfx_snode_dirty_set_,
		gfx_snode_dirty_get_);

	node->dirty.value = 0;

	// Initialize global transform matrix property.
	gfx_float_prop(
		&node->mglobal.prop,
		sizeof(node->mglobal.values) / sizeof(float),
		NULL,
		gfx_snode_mglobal_get_);

	glm_mat4_identity(*(mat4*)&node->mglobal.values);

	// Initialize local transform matrix property.
	gfx_float_prop(
		&node->mlocal.prop,
		sizeof(node->mlocal.values) / sizeof(float),
		gfx_snode_mlocal_set_,
		gfx_snode_mlocal_get_);

	glm_mat4_identity(*(mat4*)&node->mlocal.values);

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
