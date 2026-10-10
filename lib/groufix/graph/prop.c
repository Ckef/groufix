/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/graph/props.h"
#include <stdlib.h>
#include <string.h>


// Small string last-byte flag values.
#define GFX_STR_SHORT_STRING_ 0
#define GFX_STR_LONG_STRING_  1

// Retrieve the flag from a GFXStringProperty as lvalue.
#define GFX_STR_FLAG_(prop) (prop)->str[sizeof((prop)->str) - 1]


/****************************
 * Frees any memory the optimized string may hold.
 * Leaves all values of prop!
 */
static void gfx_string_prop_free_(GFXStringProperty* prop)
{
	if (GFX_STR_FLAG_(prop) == GFX_STR_LONG_STRING_)
	{
		uintptr_t ptr;
		memcpy(&ptr, prop->str, sizeof(ptr));

		free((char*)ptr);
	}
}

/****************************/
GFX_API GFXProperty* gfx_list_prop_init(GFXListProperty* prop,
                                        bool (*set)(GFXListProperty*, GFXProperty*, size_t))
{
	assert(prop != NULL);

	prop->prop.type = GFX_PROP_LIST;
	gfx_vec_init(&prop->items, sizeof(GFXProperty*));
	prop->set = set;

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
GFX_API GFXProperty* gfx_string_prop_init(GFXStringProperty* prop, const char* str)
{
	assert(prop != NULL);

	prop->prop.type = GFX_PROP_STRING;

	// Flag as short string to avoid free call.
	GFX_STR_FLAG_(prop) = GFX_STR_SHORT_STRING_;

	if (!gfx_string_prop_set(prop, str))
		return NULL;

	return &prop->prop;
}

/****************************/
GFX_API void gfx_string_prop_clear(GFXStringProperty* prop)
{
	assert(prop != NULL);

	// Free any string.
	gfx_string_prop_free_(prop);

	// Set to empty string.
	prop->str[0] = '\0';
	GFX_STR_FLAG_(prop) = GFX_STR_SHORT_STRING_;
}

/****************************/
GFX_API bool gfx_string_prop_set(GFXStringProperty* prop, const char* str)
{
	assert(prop != NULL);

	// NULL equals empty string.
	if (str == NULL || str[0] == '\0')
	{
		gfx_string_prop_clear(prop);
		return 1;
	}

	// Create short or long string.
	const size_t strLen = strlen(str);

	if (strLen < sizeof(prop->str))
	{
		// Copy as small string.
		gfx_string_prop_free_(prop);
		memcpy(prop->str, str, strLen + 1);

		GFX_STR_FLAG_(prop) = GFX_STR_SHORT_STRING_;
	}
	else
	{
		// Allocate new long string.
		char* newStr = malloc(strLen + 1);
		if (newStr == NULL) return 0;

		// Free old string after successful allocation.
		gfx_string_prop_free_(prop);
		memcpy(newStr, str, strLen + 1);

		uintptr_t ptr = (uintptr_t)newStr;
		memcpy(prop->str, &ptr, sizeof(ptr));

		GFX_STR_FLAG_(prop) = GFX_STR_LONG_STRING_;
	}

	return 1;
}

/****************************/
GFX_API const char* gfx_string_prop_get(GFXStringProperty* prop)
{
	assert(prop != NULL);

	// Return the short string.
	if (GFX_STR_FLAG_(prop) == GFX_STR_SHORT_STRING_)
		return prop->str;

	// Return the long string.
	uintptr_t ptr;
	memcpy(&ptr, prop->str, sizeof(ptr));

	return (const char*)ptr;
}

/****************************/
GFX_API GFXProperty* gfx_link_prop(GFXLinkProperty* prop, GFXProperty* follow,
                                   bool (*set)(GFXLinkProperty*, GFXProperty*))
{
	assert(prop != NULL);

	prop->prop.type = GFX_PROP_LINK;
	prop->follow = follow;
	prop->set = set;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_bool_prop(GFXValueProperty* prop, size_t count,
                                   bool* values,
                                   bool (*set)(GFXValueProperty*, const void*))
{
	assert(prop != NULL);
	assert(count > 0);
	assert(values != NULL);

	prop->prop.type = GFX_PROP_BOOL;
	prop->count = count;
	prop->values = values;
	prop->set = set;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_float_prop(GFXValueProperty* prop, size_t count,
                                    float* values,
                                    bool (*set)(GFXValueProperty*, const void*))
{
	assert(prop != NULL);
	assert(count > 0);
	assert(values != NULL);

	prop->prop.type = GFX_PROP_FLOAT;
	prop->count = count;
	prop->values = values;
	prop->set = set;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_double_prop(GFXValueProperty* prop, size_t count,
                                     double* values,
                                     bool (*set)(GFXValueProperty*, const void*))
{
	assert(prop != NULL);
	assert(count > 0);
	assert(values != NULL);

	prop->prop.type = GFX_PROP_DOUBLE;
	prop->count = count;
	prop->values = values;
	prop->set = set;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_int_prop(GFXValueProperty* prop, size_t count,
                                  int32_t* values,
                                  bool (*set)(GFXValueProperty*, const void*))
{
	assert(prop != NULL);
	assert(count > 0);
	assert(values != NULL);

	prop->prop.type = GFX_PROP_INT;
	prop->count = count;
	prop->values = values;
	prop->set = set;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_uint_prop(GFXValueProperty* prop, size_t count,
                                   uint32_t* values,
                                   bool (*set)(GFXValueProperty*, const void*))
{
	assert(prop != NULL);
	assert(count > 0);
	assert(values != NULL);

	prop->prop.type = GFX_PROP_UINT;
	prop->count = count;
	prop->values = values;
	prop->set = set;

	return &prop->prop;
}

/****************************/
GFX_API GFXProperty* gfx_func_prop(GFXFuncProperty* prop, GFXProperty* this,
                                   int (*fn)(GFXProperty*, const GFXListProperty*))
{
	assert(prop != NULL);

	prop->prop.type = GFX_PROP_FUNC;
	prop->this = this;
	prop->fn = fn;

	return &prop->prop;
}
