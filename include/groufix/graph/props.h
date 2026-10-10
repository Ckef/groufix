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
	GFX_PROP_STRING,
	GFX_PROP_FUNC,

	// Value property types.
	GFX_PROP_BOOL,
	GFX_PROP_FLOAT,
	GFX_PROP_DOUBLE,
	GFX_PROP_INT,
	GFX_PROP_UINT,
	GFX_PROP_DATA

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

	bool (*set)(struct GFXLinkProperty* prop, GFXProperty* follow);

} GFXLinkProperty;


/**
 * List property definition.
 */
typedef struct GFXListProperty
{
	GFXProperty prop;  // Base-type.
	GFXVec      items; // Stores GFXProperty*, all non-NULL.

	// index = items.size to add, item = NULL to erase.
	bool (*set)(struct GFXListProperty* prop, GFXProperty* item, size_t index);

} GFXListProperty;


/**
 * Value property definition.
 */
typedef struct GFXValueProperty
{
	GFXProperty prop; // Base-type.
	size_t      count;

	// values = count elements, in bytes if a data prop.
	bool (*set)(struct GFXValueProperty* prop, const void* values);

	// May return NULL to disallow reading.
	const void* (*get)(struct GFXValueProperty* prop);

} GFXValueProperty;


/**
 * String property definition.
 */
typedef struct GFXStringProperty
{
	GFXProperty prop; // Base-type.

	// Small string optimization:
	//  short: up to 31 string bytes, followed by all 0s.
	//  long: pointer bytes, padding bytes, one byte with value 1.
	char str[32]; // Last byte is the flag.

} GFXStringProperty;


/**
 * Function property definition.
 */
typedef struct GFXFuncProperty
{
	GFXProperty  prop; // Base-type.
	GFXProperty* this;

	int (*fn)(GFXProperty* this, const GFXListProperty* args);

} GFXFuncProperty;


/****************************
 * Property initialization and handling.
 ****************************/

/**
 * Get pointer to an object from pointer to its property member.
 * Defined as follows:
 * struct Type { ... (GFXProperty|GFX*Property|...) prop; ... };
 * ...
 * struct Type obj;
 * assert(&obj == GFX_PROP_OBJ(&obj->prop, struct Type, prop))
 */
#define GFX_PROP_OBJ(prop, type_, member_) \
	((type_*)((const char*)(prop) - offsetof(type_, member_)))


/**
 * Indexes a list property.
 * @param index Must be < prop->items.size.
 */
static inline GFXProperty* gfx_list_prop_at(GFXListProperty* prop, size_t index)
{
	return *(GFXProperty**)gfx_vec_at(&prop->items, index);
}

/**
 * Calls the setter of a list property.
 * @return The setter's return, or zero when setter set to NULL.
 */
static inline bool gfx_list_prop_set(GFXListProperty* prop, GFXProperty* item, size_t index)
{
	return prop->set ? prop->set(prop, item, index) : 0;
}

/**
 * Calls the setter of a link property.
 * @return The setter's return, or zero when setter set to NULL.
 */
static inline bool gfx_link_prop_set(GFXLinkProperty* prop, GFXProperty* follow)
{
	return prop->set ? prop->set(prop, follow) : 0;
}

/**
 * Calls the setter of a value property.
 * @return The setter's return, or zero when setter set to NULL.
 */
static inline bool gfx_value_prop_set(GFXValueProperty* prop, const void* values)
{
	return prop->set ? prop->set(prop, values) : 0;
}

/**
 * Calls the getter of a value property.
 * @return The getter's return, or NULL when getter set to NULL.
 */
static inline const void* gfx_value_prop_get(GFXValueProperty* prop)
{
	return prop->get ? prop->get(prop) : NULL;
}

/**
 * Calls a function property.
 * @return The function's return, or zero when set to NULL.
 */
static inline int gfx_func_prop_call(GFXFuncProperty* prop, const GFXListProperty* args)
{
	return prop->fn ? prop->fn(prop->this, args) : 0;
}

/**
 * Initializes a list property.
 * @param prop Cannot be NULL.
 * @param set  May be NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_list_prop_init(GFXListProperty* prop,
                                        bool (*set)(GFXListProperty*, GFXProperty*, size_t));

/**
 * Clears a list property.
 * @param prop Cannot be NULL.
 */
GFX_API void gfx_list_prop_clear(GFXListProperty* prop);

/**
 * Adds a property to the end of a list property.
 * @param prop Cannot be NULL.
 * @param item Cannot be NULL.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_list_prop_add(GFXListProperty* prop, GFXProperty* item);

/**
 * Erases a property from a list property at some index.
 * @param prop  Cannot be NULL.
 * @param index Must be < prop->items.size.
 */
GFX_API void gfx_list_prop_erase(GFXListProperty* prop, size_t index);

/**
 * Initializes a string property.
 * @param prop Cannot be NULL.
 * @param str  Must be NULL-terminated or NULL for empty string.
 * @return &prop->prop, NULL when out of memory.
 */
GFX_API GFXProperty* gfx_string_prop_init(GFXStringProperty* prop, const char* str);

/**
 * Clears a string property, setting its value to the empty string.
 * @param prop Cannot be NULL.
 */
GFX_API void gfx_string_prop_clear(GFXStringProperty* prop);

/**
 * Sets the value of a string property.
 * @param prop Cannot be NULL.
 * @param str  Must be NULL-terminated or NULL for empty string.
 * @return Zero when out of memory.
 */
GFX_API bool gfx_string_prop_set(GFXStringProperty* prop, const char* str);

/**
 * Retrieves the value of a string property.
 * @param prop Cannot be NULL.
 * @return Never NULL, always NULL-terminated.
 *
 * Note: the returned pointer is invalidated when the value is changed.
 */
GFX_API const char* gfx_string_prop_get(GFXStringProperty* prop);

/**
 * Initializes a link property.
 * Does not need to be cleared, hence no _init postfix.
 * @param prop   Cannot be NULL.
 * @param follow May be NULL.
 * @param set    May be NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_link_prop(GFXLinkProperty* prop, GFXProperty* follow,
                                   bool (*set)(GFXLinkProperty*, GFXProperty*));

/**
 * Initializes a boolean value property.
 * Does not need to be cleared, hence no _init postfix.
 * @param prop   Cannot be NULL.
 * @param count  Must be > 0.
 * @param set    May be NULL if get is not NULL.
 * @param get    May be NULL if set is not NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_bool_prop(GFXValueProperty* prop, size_t count,
                                   bool (*set)(GFXValueProperty*, const void*),
                                   const void* (*get)(GFXValueProperty*));

/**
 * Initializes a float value property.
 * Does not need to be cleared, hence no _init postfix.
 * @see gfx_bool_prop.
 */
GFX_API GFXProperty* gfx_float_prop(GFXValueProperty* prop, size_t count,
                                    bool (*set)(GFXValueProperty*, const void*),
                                    const void* (*get)(GFXValueProperty*));

/**
 * Initializes a double value property.
 * Does not need to be cleared, hence no _init postfix.
 * @see gfx_bool_prop.
 */
GFX_API GFXProperty* gfx_double_prop(GFXValueProperty* prop, size_t count,
                                     bool (*set)(GFXValueProperty*, const void*),
                                     const void* (*get)(GFXValueProperty*));

/**
 * Initializes an integer value property.
 * Does not need to be cleared, hence no _init postfix.
 * @see gfx_bool_prop.
 */
GFX_API GFXProperty* gfx_int_prop(GFXValueProperty* prop, size_t count,
                                  bool (*set)(GFXValueProperty*, const void*),
                                  const void* (*get)(GFXValueProperty*));

/**
 * Initializes an unsigned integer value property.
 * Does not need to be cleared, hence no _init postfix.
 * @see gfx_bool_prop.
 */
GFX_API GFXProperty* gfx_uint_prop(GFXValueProperty* prop, size_t count,
                                   bool (*set)(GFXValueProperty*, const void*),
                                   const void* (*get)(GFXValueProperty*));

/**
 * Initializes a data value property.
 * Does not need to be cleared, hence no _init postfix.
 * @param count Byte size, must be > 0.
 * @see gfx_bool_prop.
 */
GFX_API GFXProperty* gfx_data_prop(GFXValueProperty* prop, size_t count,
                                   bool (*set)(GFXValueProperty*, const void*),
                                   const void* (*get)(GFXValueProperty*));

/**
 * Initializes a function property.
 * Does not need to be cleared, hence no _init postfix.
 * @param prop Cannot be NULL.
 * @param fn   May be NULL.
 * @return &prop->prop.
 */
GFX_API GFXProperty* gfx_func_prop(GFXFuncProperty* prop, GFXProperty* this,
                                   int (*fn)(GFXProperty*, const GFXListProperty*));


#endif
