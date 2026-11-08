/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/log.h"
#include "groufix/scene/node.h"
#include "groufix/scene.h"
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
 * Sets the dirty flag of a node.
 * Silently ignored if node does not have a dirty flag.
 */
static void gfx_node_set_dirty_(GFXNode* node)
{
	// Check if the node's dirty property exists and
	// is a boolean value property of count 1.
	GFXValueProperty* dirty =
		(GFXValueProperty*)gfx_node_get(node, "dirty");

	if (
		dirty != NULL &&
		dirty->prop.type == GFX_PROP_BOOL &&
		dirty->count == 1)
	{
		const bool flag = 1;
		gfx_value_prop_set(dirty, &flag);
	}
}

/****************************
 * Adds a node to a scene's process list.
 * Assumes node is not yet added to scene!
 * @param scene Cannot be NULL.
 * @param node  Cannot be NULL.
 * @return Zero when out of memory.
 */
static bool gfx_scene_add_node_(GFXScene* scene, GFXNode* node)
{
	assert(scene != NULL);
	assert(node != NULL);

	// Do nothing if we do not have a process callback.
	if (node->process.fn == NULL) return 1;

	// If removed, simply un-remove.
	// The 'removed' dict might contain invalidated pointers, but if one of
	// those happens to match the node we're inserting, there is no difference.
	// Just remove it from the dict, it's still in the list.
	if (gfx_dict_erase(&scene->process.removed, node) == NULL)
	{
		// If it wasn't removed, add it to the list.
		if (!gfx_vec_push(&scene->process.list, 1, &node))
			return 0;
	}

	// Always set dirty, if we un-remove it could have been an old node.
	scene->process.dirty |= 0x01;

	return 1;
}

/****************************
 * Removes a node from a scene's process list.
 * Assumes node has been added to scene!
 * @param scene Cannot be NULL.
 * @param node  Cannot be NULL.
 */
static void gfx_scene_remove_node_(GFXScene* scene, GFXNode* node)
{
	assert(scene != NULL);
	assert(node != NULL);

	// If we do not have a process callback, it should already be removed.
	if (node->process.fn == NULL) return;

	// Simply add it to the 'removed' dict.
	// The pointer we add to the dict might become invalidated afterwards.
	// However this does not matter, we only need its value to remove it
	// from the list later.
	if (!gfx_dict_set(&scene->process.removed, node, node))
	{
		// If we can't add it to the dict,
		// search for it in the list and directly remove it.
		for (size_t i = 0; i < scene->process.list.size; ++i)
			if (node == *(GFXNode**)gfx_vec_at(&scene->process.list, i))
			{
				gfx_vec_erase(&scene->process.list, 1, i);
				break;
			}

		// Set dirty, but without re-sorting!
		scene->process.dirty |= 0x02;
	}
}

/****************************
 * Sets a node's scene, triggering its enter & leave callbacks and
 * adding or removing it from its scene's process list.
 * Does nothing if node's scene is already set correctly.
 * @param node Cannot be NULL.
 * @return Zero when out of memory.
 *
 * Failure is considered fatal, node will not be processed.
 */
static bool gfx_node_set_scene_(GFXNode* node, GFXScene* scene)
{
	assert(node != NULL);

	bool success = 1;

	if (node->scene != scene)
	{
		// Trigger leave callback before updating & remove from old scene.
		if (node->scene != NULL)
		{
			GFXProperty* arg = &node->scene->root.prop;
			gfx_func_prop_call(&node->leave, 1, &arg);

			gfx_scene_remove_node_(node->scene, node);
		}

		// Add to new scene.
		if (scene != NULL)
		{
			// If we fail to add, nothing can be done.
			if (!gfx_scene_add_node_(scene, node))
			{
				gfx_log_fatal(
					"Could not add a node to a scene's process list, "
					"node will not be processed...");

				success = 0;
			}
		}

		// Set node's scene pointer.
		node->scene = scene;

		// Trigger enter callback after updating.
		if (scene != NULL)
		{
			GFXProperty* arg = &scene->root.prop;
			gfx_func_prop_call(&node->enter, 1, &arg);
		}
	}

	return success;
}

/****************************
 * Recursively propagate the scene, depth and dirty flag to a node's children.
 * Does NOT set the scene, depth or dirty flag of node itself!
 * @param node Cannot be NULL.
 */
static void gfx_node_propagate_(GFXNode* node)
{
	assert(node != NULL);

	// Get new scene & depth of all children.
	GFXScene* scene = node->scene;
	const unsigned int depth = node->depth.value + 1;

	// Loop over all children.
	for (size_t i = 0; i < node->children.items.size; ++i)
	{
		GFXNode* child = gfx_node_get_child(node, i);

		// Unset old scene, potentially triggering leave.
		if (child->scene != scene)
			gfx_node_set_scene_(child, NULL);

		// Set depth of this child.
		child->depth.value = depth;

		// Flag this child as dirty.
		gfx_node_set_dirty_(child);

		// Set new scene, potentially triggering enter.
		gfx_node_set_scene_(child, scene);

		// Propagate to its children.
		gfx_node_propagate_(child);
	}
}

/****************************
 * Retrieves the global transform matrix of node's parent.
 * @param node Cannot be NULL.
 * @return Column-major 4x4 matrix, alignment unknown.
 */
static const float* gfx_node_get_parent_mglobal_(GFXNode* node)
{
	assert(node != NULL);

	GFXNode* parent = gfx_node_get_parent(node);
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
			return gfx_value_prop_get(mglobal);
		}
	}

	return NULL;
}

/****************************
 * GFXNode.name setter implementation.
 */
static bool gfx_node_name_set_(GFXStringProperty* prop, const char* str)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, name);

	return gfx_string_prop_store(&node->name, str);
}

/****************************
 * GFXNode.parent setter implementation.
 */
static bool gfx_node_parent_set_(GFXLinkProperty* prop, GFXProperty* follow)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, parent);
	GFXNode* parent = (GFXNode*)follow;
	const size_t currIndex = node->index.value;
	GFXScene* newScene = NULL;

	// Validate follow is a node.
	if (follow != NULL && follow->type != GFX_PROP_NODE)
		return 0;

	// Parent already set.
	if (node->parent.follow == follow)
		return 1;

	// Check for cycles.
	if (!gfx_check_cycles_(node, parent))
		return 0;

	if (parent == NULL)
	{
		// Unset old scene, potentially triggering leave.
		gfx_node_set_scene_(node, NULL);

		// Reset depth & index if removing parent.
		node->depth.value = 0;
		node->index.value = 0;
	}
	else
	{
		GFXScene* currScene = node->scene;
		newScene = parent->scene;
		const bool diffScene = currScene != newScene;

		// Unset old scene, potentially triggering leave.
		if (diffScene) gfx_node_set_scene_(node, NULL);

		// If new parent, add node as a child to it.
		if (!gfx_list_prop_add(&parent->children, &node->prop))
		{
			// This is the only point this function can fail,
			// but if we do, we've just unset the old scene...
			// To undo, set it again.
			if (diffScene) gfx_node_set_scene_(node, currScene);

			return 0;
		}

		// Set new depth & index according to new parent.
		node->depth.value = parent->depth.value + 1;
		node->index.value = parent->children.items.size - 1;

		// Order changed, scene didn't; flag the scene's process list as dirty.
		// Even if this node has no process callback; its children may have it!
		if (newScene != NULL && !diffScene)
			newScene->process.dirty |= 0x01;
	}

	// Remove node as child from its current parent.
	if (node->parent.follow != NULL)
	{
		GFXNode* currParent = (GFXNode*)node->parent.follow;
		gfx_list_prop_erase(&currParent->children, currIndex);

		// Set new index value of all moved children.
		for (size_t i = currIndex; i < currParent->children.items.size; ++i)
		{
			GFXNode* child =
				(GFXNode*)gfx_list_prop_at(&currParent->children, i);

			child->index.value = i;
		}
	}

	// Set new parent.
	node->parent.follow = follow;

	// Flag this node as dirty.
	gfx_node_set_dirty_(node);

	// Set new scene, potentially triggering enter.
	gfx_node_set_scene_(node, newScene);

	// Propagate to its children.
	gfx_node_propagate_(node);

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
 * GFXNode.enter setter implementation.
 */
static bool gfx_node_enter_set_(GFXFuncProperty* prop, GFXFuncPropertyFn fn, intmax_t ctx)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, enter);

	node->enter.fn = fn;
	node->enter.ctx = ctx;

	return 1;
}

/****************************
 * GFXNode.leave setter implementation.
 */
static bool gfx_node_leave_set_(GFXFuncProperty* prop, GFXFuncPropertyFn fn, intmax_t ctx)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, leave);

	node->leave.fn = fn;
	node->leave.ctx = ctx;

	return 1;
}

/****************************
 * GFXNode.process setter implementation.
 */
static bool gfx_node_process_set_(GFXFuncProperty* prop, GFXFuncPropertyFn fn, intmax_t ctx)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, process);
	GFXScene* scene = node->scene;

	// Determine whether we remove or add a process callback.
	const bool addFn = node->process.fn == NULL && fn != NULL;
	const bool removeFn = node->process.fn != NULL && fn == NULL;

	// To be removed from the current scene's process list.
	if (scene != NULL && removeFn)
		gfx_scene_remove_node_(scene, node);

	node->process.fn = fn;
	node->process.ctx = ctx;

	// To be added to the current scene's process list.
	if (scene != NULL && addFn)
		gfx_scene_add_node_(scene, node);

	return 1;
}

/****************************
 * GFXNode.depth.prop getter implementation.
 */
static const void* gfx_node_depth_get_(GFXValueProperty* prop)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, depth.prop);

	return &node->depth.value;
}

/****************************
 * GFXNode.index.prop setter implementation.
 */
static bool gfx_node_index_set_(GFXValueProperty* prop, const void* values)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, index.prop);
	GFXNode* parent = gfx_node_get_parent(node);
	const size_t to = *(const size_t*)values;
	const size_t from = node->index.value;

	// Validate index exists at parent.
	if (parent == NULL || to > parent->children.items.size)
		return 0;

	// Validate we're moving something, if not, avoid flagging dirty!
	if (to == from || to == from + 1)
		return 1;

	// Move it as child at the parent.
	gfx_list_prop_move(&parent->children, from, to);

	// Set new index value of all moved children.
	const size_t smaller = from < to ? from : to;
	const size_t bigger = from > to ? from + 1 : to;

	for (size_t i = smaller; i < bigger; ++i)
	{
		GFXNode* child =
			(GFXNode*)gfx_list_prop_at(&parent->children, i);

		child->index.value = i;
	}

	// Order changed; flag the scene's process list as dirty.
	// Even if this node has no process callback; its children may have it!
	if (node->scene != NULL)
		node->scene->process.dirty |= 0x01;

	return 1;
}

/****************************
 * GFXNode.index.prop getter implementation.
 */
static const void* gfx_node_index_get_(GFXValueProperty* prop)
{
	GFXNode* node = GFX_PROP_OBJ(prop, GFXNode, index.prop);

	return &node->index.value;
}

/****************************
 * GFXSpatialNode.dirty.prop setter implementation.
 */
static bool gfx_snode_dirty_set_(GFXValueProperty* prop, const void* values)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, dirty.prop);
	const bool flag = *(const bool*)values;

	// ONLY set the transform dirty flag if setting to 1.
	// This way we will get propagation updates from the parent,
	// but matrix updates still happen when reset by the user.
	if (flag) node->mdirty = 1;
	node->dirty.value = flag;

	return 1;
}

/****************************
 * GFXSpatialNode.dirty.prop getter implementation.
 */
static const void* gfx_snode_dirty_get_(GFXValueProperty* prop)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, dirty.prop);

	// Only output the public dirty property.
	// This way this and child nodes can use and reset the dirty flag
	// themselves, even when mgobal is retrieved somewhere else.
	return &node->dirty.value;
}

/****************************
 * GFXSpatialNode.mglobal.prop setter implementation.
 */
static bool gfx_snode_mglobal_set_(GFXValueProperty* prop, const void* values)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, mglobal.prop);

	// Update local transform matrix if changed.
	// Do not postpone this update to reduce matrix operations;
	// we need the local transform to be up to date when changes
	// occur from higher up in the node's tree!
	// If flagged dirty, always update, outdated stored values!
	const bool dirty = node->mdirty || memcmp(
		node->mglobal.values,
		values,
		sizeof(node->mglobal.values)) != 0;

	if (dirty)
	{
		// Directly copy to the stored values.
		glm_mat4_make(
			values,
			*(mat4*)&node->mglobal.values);

		// own mlocal = (parent mglobal)^-1 * own mglobal
		// See if we have a parent mglobal.
		const float* pmglobal =
			gfx_node_get_parent_mglobal_(&node->base);

		if (pmglobal != NULL)
		{
			// If we have parent mglobal values, calculate own mlocal.
			// pmglobal has unknown alignment, copy to local variable.
			mat4 pGlobal;
			glm_mat4_make(pmglobal, pGlobal);
			glm_mat4_inv(pGlobal, pGlobal);
			glm_mat4_mul(pGlobal,
				*(mat4*)&node->mglobal.values,
				*(mat4*)&node->mlocal.values);
		}
		else
		{
			// Simply copy mglobal to mlocal if we have no parent values.
			glm_mat4_copy(
				*(mat4*)&node->mglobal.values,
				*(mat4*)&node->mlocal.values);
		}

		// Lazily propagate if values changed.
		// Always reset own transform dirty flag, but do set public dirty!
		if (!node->mdirty)
			node->dirty.value = 1,
			gfx_node_propagate_(&node->base);
		else
			node->mdirty = 0;
	}

	return 1;
}

/****************************
 * GFXSpatialNode.mglobal.prop getter implementation.
 */
static const void* gfx_snode_mglobal_get_(GFXValueProperty* prop)
{
	GFXSpatialNode* node = GFX_PROP_OBJ(prop, GFXSpatialNode, mglobal.prop);

	// Update global transform matrix if flagged as dirty.
	if (node->mdirty)
	{
		// own mglobal = parent mglobal * own mlocal.
		// See if we have a parent mglobal.
		const float* pmglobal =
			gfx_node_get_parent_mglobal_(&node->base);

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

		// Reset transform dirty flag.
		node->mdirty = 0;
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

	if (!equal)
	{
		// Directly copy to the stored values.
		glm_mat4_make(
			values,
			*(mat4*)&node->mlocal.values);

		// Lazily flag dirty & propagate.
		if (!node->mdirty)
		{
			node->mdirty = 1;
			node->dirty.value = 1;
			gfx_node_propagate_(&node->base);
		}
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

/****************************
 * gfx_node_type implementation of the alloc.init function.
 */
static bool gfx_node_type_init_(void* node, const char* name)
{
	return gfx_node_init(node, name);
}

/****************************
 * gfx_node_type implementation of the alloc.clear function.
 */
static void gfx_node_type_clear_(void* node)
{
	gfx_node_clear(node);
}

/****************************
 * gfx_spatial_node_type implementation of the alloc.init function.
 */
static bool gfx_spatial_node_type_init_(void* node, const char* name)
{
	return gfx_snode_init(node, name);
}

/****************************
 * gfx_spatial_node_type implementation of the alloc.clear function.
 */
static void gfx_spatial_node_type_clear_(void* node)
{
	gfx_snode_clear(node);
}


/****************************/
const GFXNodeType gfx_node_type =
{
	.name = "Node",
	.inherits = NULL,

	.alloc = {
		.size = sizeof(GFXNode),
		.init = gfx_node_type_init_,
		.clear = gfx_node_type_clear_
	}
};


/****************************/
const GFXNodeType gfx_spatial_node_type =
{
	.name = "SpatialNode",
	.inherits = GFX_NODE_TYPE,

	.alloc = {
		.size = sizeof(GFXSpatialNode),
		.init = gfx_spatial_node_type_init_,
		.clear = gfx_spatial_node_type_clear_
	}
};


/****************************/
GFX_API bool gfx_node_init(GFXNode* node, const char* name)
{
	assert(node != NULL);

	node->prop.type = GFX_PROP_NODE;

	// First initialize name, may need to allocate.
	if (gfx_string_prop_init(&node->name, name, gfx_node_name_set_) == NULL)
		return 0;

	// Initialize the rest of the node.
	gfx_sdict_init(&node->properties);
	node->scene = NULL;
	node->type = GFX_NODE_TYPE;

	gfx_link_prop(&node->parent, NULL, gfx_node_parent_set_);
	gfx_list_prop_init(&node->children, gfx_node_children_set_);
	gfx_func_prop(&node->enter, &node->prop, gfx_node_enter_set_);
	gfx_func_prop(&node->leave, &node->prop, gfx_node_leave_set_);
	gfx_func_prop(&node->process, &node->prop, gfx_node_process_set_);

	// Initialize depth property.
	gfx_uint_prop(
		&node->depth.prop, 1,
		NULL,
		gfx_node_depth_get_);

	node->depth.value = 0;

	// Initialize index property.
	gfx_size_prop(
		&node->index.prop, 1,
		gfx_node_index_set_,
		gfx_node_index_get_);

	node->index.value = 0;

	// Set all properties.
	gfx_dict_reserve(&node->properties, 8); // Just a kick-start.
	gfx_node_set(node, &node->name.prop, "name");
	gfx_node_set(node, &node->parent.prop, "parent");
	gfx_node_set(node, &node->children.prop, "children");
	gfx_node_set(node, &node->enter.prop, "enter");
	gfx_node_set(node, &node->leave.prop, "leave");
	gfx_node_set(node, &node->process.prop, "process");
	gfx_node_set(node, &node->depth.prop.prop, "depth");
	gfx_node_set(node, &node->index.prop.prop, "index");

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

	if (!gfx_node_init(&node->base, name))
		return 0;

	node->base.type = GFX_SPATIAL_NODE_TYPE;

	// Initialize (public) dirty flag property.
	gfx_bool_prop(
		&node->dirty.prop, 1,
		gfx_snode_dirty_set_,
		gfx_snode_dirty_get_);

	node->mdirty = 0;
	node->dirty.value = 0;

	// Initialize global transform matrix property.
	gfx_float_prop(
		&node->mglobal.prop,
		sizeof(node->mglobal.values) / sizeof(float),
		gfx_snode_mglobal_set_,
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
	gfx_node_set(&node->base, &node->dirty.prop.prop, "dirty");
	gfx_node_set(&node->base, &node->mglobal.prop.prop, "mglobal");
	gfx_node_set(&node->base, &node->mlocal.prop.prop, "mlocal");

	return 1;
}

/****************************/
GFX_API void gfx_snode_clear(GFXSpatialNode* node)
{
	assert(node != NULL);

	gfx_node_clear(&node->base);

	// Leave all values, node is invalidated.
}
