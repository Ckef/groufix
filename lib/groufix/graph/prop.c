/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/graph/props.h"


/****************************/
GFX_API GFXProperty* gfx_list_prop_init(GFXListProperty* prop)
{
	assert(prop != NULL);

	prop->prop.type = GFX_PROP_LIST;
	gfx_vec_init(&prop->items, sizeof(GFXProperty*));

	return &prop->prop;
}

/****************************/
GFX_API void gfx_list_prop_clear(GFXListProperty* prop)
{
	assert(prop != NULL);

	gfx_vec_clear(&prop->items);
}

/****************************/
GFX_API GFXProperty* gfx_link_prop(GFXLinkProperty* prop, GFXProperty* link)
{
	assert(prop != NULL);

	prop->prop.type = GFX_PROP_LINK;
	prop->forward = link;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_value_prop(GFXValueProperty* prop,
                                    GFXPropertyType type, size_t count, void* value)
{
	assert(prop != NULL);
	assert(count > 0);
	assert(value != NULL);
	assert(
		type == GFX_PROP_BOOL ||
		type == GFX_PROP_FLOAT ||
		type == GFX_PROP_DOUBLE ||
		type == GFX_PROP_INT ||
		type == GFX_PROP_UINT ||
		type == GFX_PROP_STRING);

	prop->prop.type = type;
	prop->count = count;
	prop->value = value;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_func_prop(GFXFuncProperty* prop,
                                   int (*fn)(GFXProperty*, const GFXListProperty*))
{
	assert(prop != NULL);

	prop->prop.type = GFX_PROP_FUNC;
	prop->fn = fn;

	return &prop->prop;
}
