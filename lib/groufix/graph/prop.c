/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/graph/props.h"
#include <string.h>


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
GFX_API bool gfx_list_prop_add(GFXListProperty* prop, GFXProperty* item)
{
	assert(prop != NULL);
	assert(item != NULL);

	return gfx_vec_push(&prop->items, 1, &item);
}

/****************************/
GFX_API void gfx_list_prop_erase(GFXListProperty* prop, size_t index)
{
	assert(prop != NULL);
	assert(index < prop->items.size);

	gfx_vec_erase(&prop->items, 1, index);
}

/****************************/
GFX_API GFXProperty* gfx_link_prop(GFXLinkProperty* prop, GFXProperty* follow)
{
	assert(prop != NULL);

	prop->prop.type = GFX_PROP_LINK;
	prop->follow = follow;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_bool_prop(GFXValueProperty* prop, size_t count,
                                   bool* values)
{
	assert(prop != NULL);
	assert(count > 0);
	assert(values != NULL);

	prop->prop.type = GFX_PROP_BOOL;
	prop->count = count;
	prop->values = values;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_float_prop(GFXValueProperty* prop, size_t count,
                                    float* values)
{
	assert(prop != NULL);
	assert(count > 0);
	assert(values != NULL);

	prop->prop.type = GFX_PROP_FLOAT;
	prop->count = count;
	prop->values = values;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_double_prop(GFXValueProperty* prop, size_t count,
                                     double* values)
{
	assert(prop != NULL);
	assert(count > 0);
	assert(values != NULL);

	prop->prop.type = GFX_PROP_DOUBLE;
	prop->count = count;
	prop->values = values;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_int_prop(GFXValueProperty* prop, size_t count,
                                  int32_t* values)
{
	assert(prop != NULL);
	assert(count > 0);
	assert(values != NULL);

	prop->prop.type = GFX_PROP_INT;
	prop->count = count;
	prop->values = values;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_uint_prop(GFXValueProperty* prop, size_t count,
                                   uint32_t* values)
{
	assert(prop != NULL);
	assert(count > 0);
	assert(values != NULL);

	prop->prop.type = GFX_PROP_UINT;
	prop->count = count;
	prop->values = values;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_string_prop(GFXValueProperty* prop, char* str)
{
	assert(prop != NULL);
	assert(str != NULL);

	prop->prop.type = GFX_PROP_STRING;
	prop->count = strlen(str);
	prop->values = str;

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
