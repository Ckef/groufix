/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/graph/node.h"


/****************************/
GFX_API void gfx_node_init(GFXNode* node)
{
	assert(node != NULL);

	node->prop.type = GFX_PROP_NODE;
	gfx_sdict_init(&node->properties);

	gfx_link_prop(&node->parent, NULL);
	gfx_list_prop_init(&node->children);
	gfx_func_prop(&node->update, NULL);

	// Set all properties.
	gfx_dict_set(&node->properties, &node->parent, "parent");
	gfx_dict_set(&node->properties, &node->children, "children");
	gfx_dict_set(&node->properties, &node->update, "update");
}

/****************************/
GFX_API void gfx_snode_init(GFXSpatialNode* node)
{
	assert(node != NULL);

	const size_t numFloats =
		sizeof(node->matrix.value) / sizeof(float);

	gfx_node_init(&node->node);
	gfx_value_prop(&node->matrix.prop,
		GFX_PROP_FLOAT, numFloats, node->matrix.value);

	// TODO: Set new update function updating the matrices.

	for (size_t i = 0; i < numFloats; ++i)
		node->matrix.value[i] = 0.0f;

	// Set all properties.
	gfx_dict_set(&node->node.properties, &node->matrix.prop, "matrix");
}

/****************************/
GFX_API void gfx_node_clear(GFXNode* node)
{
	assert(node != NULL);

	gfx_dict_clear(&node->properties);
	gfx_list_prop_clear(&node->children);

	// Leave all values, node is invalidated.
}

/****************************/
GFX_API void gfx_snode_clear(GFXSpatialNode* node)
{
	assert(node != NULL);

	gfx_node_clear(&node->node);

	// Leave all values, node is invalidated.
}

/****************************/
GFX_API bool gfx_node_set(GFXNode* node, GFXProperty* prop, const char* key)
{
	assert(node != NULL);
	assert(key != NULL);

	return gfx_dict_set(&node->properties, prop, key);
}

/****************************/
GFX_API GFXProperty* gfx_node_get(GFXNode* node, const char* key)
{
	assert(node != NULL);
	assert(key != NULL);

	GFXProperty* prop = gfx_dict_get(&node->properties, key);

	// Resolve any links.
	while (prop && prop->type == GFX_PROP_LINK)
		prop = ((GFXLinkProperty*)prop)->forward;

	return prop;
}
