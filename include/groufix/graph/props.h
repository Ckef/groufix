/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_GRAPH_PROPS_H
#define GFX_GRAPH_PROPS_H

#include "groufix/containers/vec.h"
#include "groufix/def.h"


/**
 * Generic property type.
 */
typedef enum GFXPropertyType
{
	GFX_PROP_NODE,
	GFX_PROP_LINK,
	GFX_PROP_LIST,
	GFX_PROP_FUNC,

	// Value property types.
	GFX_PROP_BOOL,
	GFX_PROP_FLOAT,
	GFX_PROP_DOUBLE,
	GFX_PROP_INT,
	GFX_PROP_UINT,
	GFX_PROP_STRING

} GFXPropertyType;


/**
 * Generic property definition.
 */
typedef struct GFXProperty
{
	GFXPropertyType type;

} GFXProperty;


/**
 * Link (follow) property definition.
 */
typedef struct GFXLinkProperty
{
	GFXProperty  prop; // Base-type.
	GFXProperty* follow;

} GFXLinkProperty;


/**
 * List property definition.
 */
typedef struct GFXListProperty
{
	GFXProperty prop;  // Base-type.
	GFXVec      items; // Stores GFXProperty*.

} GFXListProperty;


/**
 * Value property definition.
 */
typedef struct GFXValueProperty
{
	GFXProperty prop; // Base-type.

	size_t count;
	void*  values; // Types determined by prop.type.

} GFXValueProperty;


/**
 * Function property definition.
 */
typedef struct GFXFuncProperty
{
	GFXProperty prop; // Base-type.

	int (*fn)(GFXProperty* this, const GFXListProperty* args);

} GFXFuncProperty;


/****************************
 * Property initialization and handling.
 ****************************/

/**
 * Indexes a list property.
 */
static inline GFXProperty* gfx_list_prop_at(GFXListProperty* prop, size_t index)
{
	return *(GFXProperty**)gfx_vec_at(&prop->items, index);
}

/**
 * Calls a function property.
 * @return The function's return, or zero when set to NULL.
 */
static inline int gfx_func_prop_call(GFXFuncProperty* prop, GFXProperty* this,
                                     const GFXListProperty* args)
{
	return prop->fn ? prop->fn(this, args) : 0;
}

/**
 * Initializes a list property.
 * @param prop Cannot be NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_list_prop_init(GFXListProperty* prop);

/**
 * Clears a list property.
 * @param prop Cannot be NULL.
 */
GFX_API void gfx_list_prop_clear(GFXListProperty* prop);

/**
 * Pushes properties to the end of a list property.
 * @param prop Cannot be NULL.
 * @param item Cannot be NULL.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_list_prop_push(GFXListProperty* prop, GFXProperty* item);

/**
 * Inserts properties in the list property at some index.
 * @param prop  Cannot be NULL.
 * @param item  Cannot be NULL.
 * @param index Must be <= prop->items.size.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_list_prop_insert(GFXListProperty* prop, GFXProperty* item,
                                  size_t index);

/**
 * Pops properties from the end of a list property.
 * @param prop Cannot be NULL.
 */
GFX_API void gfx_list_prop_pop(GFXListProperty* prop);

/**
 * Erases properties from the list property at some index.
 * @param prop  Cannot be NULL.
 * @param index Must be < prop->items.size.
 */
GFX_API void gfx_list_prop_erase(GFXListProperty* prop, size_t index);

/**
 * Initializes a link property.
 * Does not need to be cleared, hence no _init postfix.
 * @param prop   Cannot be NULL.
 * @param follow May be NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_link_prop(GFXLinkProperty* prop, GFXProperty* follow);

/**
 * Initializes a boolean value property.
 * Does not need to be cleared, hence no _init postfix.
 * @param prop   Cannot be NULL.
 * @param count  Must be > 0.
 * @param values Cannot be NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_bool_prop(GFXValueProperty* prop, size_t count,
                                   bool* values);

/**
 * Initializes a float value property.
 * Does not need to be cleared, hence no _init postfix.
 * @see gfx_bool_prop.
 */
GFX_API GFXProperty* gfx_float_prop(GFXValueProperty* prop, size_t count,
                                    float* values);

/**
 * Initializes a double value property.
 * Does not need to be cleared, hence no _init postfix.
 * @see gfx_bool_prop.
 */
GFX_API GFXProperty* gfx_double_prop(GFXValueProperty* prop, size_t count,
                                     double* values);

/**
 * Initializes an integer value property.
 * Does not need to be cleared, hence no _init postfix.
 * @see gfx_bool_prop.
 */
GFX_API GFXProperty* gfx_int_prop(GFXValueProperty* prop, size_t count,
                                  int32_t* values);

/**
 * Initializes an unsigned integer value property.
 * Does not need to be cleared, hence no _init postfix.
 * @see gfx_bool_prop.
 */
GFX_API GFXProperty* gfx_uint_prop(GFXValueProperty* prop, size_t count,
                                   uint32_t* values);

/**
 * Initializes a string value property.
 * Does not need to be cleared, hence no _init postfix.
 * @param prop Cannot be NULL.
 * @param str  Cannot be NULL, must be NULL-terminated.
 * @return &prop->prop.
 *
 * str is directly stored in prop, hence no const.
 */
GFX_API GFXProperty* gfx_string_prop(GFXValueProperty* prop, char* str);

/**
 * Initializes a function property.
 * Does not need to be cleared, hence no _init postfix.
 * @param prop Cannot be NULL.
 * @param fn   May be NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_func_prop(GFXFuncProperty* prop,
                                   int (*fn)(GFXProperty*, const GFXListProperty*));


#endif
