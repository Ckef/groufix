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
 * Link (forward) property definition.
 */
typedef struct GFXLinkProperty
{
	GFXProperty  prop; // Base-type.
	GFXProperty* forward;

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
	void*  value; // Type determined by prop.type.

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
 * Calls a function property.
 */
static inline int gfx_func_prop_call(GFXFuncProperty* prop, GFXProperty* this,
                                     const GFXListProperty* args)
{
	return  prop->fn ? prop->fn(this, args) : 0;
}

/**
 * Indexes a list property.
 */
static inline GFXProperty* gfx_list_prop_at(GFXListProperty* prop, size_t index)
{
	return *(GFXProperty**)gfx_vec_at(&prop->items, index);
}

/**
 * Pushes properties to the end of a list property.
 */
static inline bool gfx_list_prop_push(GFXListProperty* prop, GFXProperty* item)
{
	return gfx_vec_push(&prop->items, 1, &item);
}

/**
 * Inserts properties in the list property at some index.
 */
static inline bool gfx_list_prop_insert(GFXListProperty* prop, GFXProperty* item,
                                        size_t index)
{
	return gfx_vec_insert(&prop->items, 1, &item, index);
}

/**
 * Pops properties from the end of a list property.
 */
static inline void gfx_list_prop_pop(GFXListProperty* prop)
{
	gfx_vec_pop(&prop->items, 1);
}

/**
 * Erases properties from the list property at some index.
 */
static inline void gfx_list_prop_erase(GFXListProperty* prop, size_t index)
{
	gfx_vec_erase(&prop->items, 1, index);
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
 * Initializes a link property.
 * Does not need to be cleared, hence no _init postfix.
 * @param prop Cannot be NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_link_prop(GFXLinkProperty* prop, GFXProperty* link);

/**
 * Initializes a value property.
 * Does not need to be cleared, hence no _init postfix.
 * @param prop  Cannot be NULL.
 * @param type  Must be GFX_PROP_(BOOL|FLOAT|DOUBLE|INT|UINT|STRING).
 * @param count Must be > 0.
 * @param value Cannot be NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_value_prop(GFXValueProperty* prop, GFXPropertyType type,
                                    size_t count, void* value);

/**
 * Initializes a function property.
 * Does not need to be cleared, hence no _init postfix.
 * @param prop Cannot be NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_func_prop(GFXFuncProperty* prop,
                                   int (*fn)(GFXProperty*, const GFXListProperty*));


#endif
