/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_STR_H_
#define GFX_CORE_STR_H_

#include "groufix/def.h"
#include <ctype.h>


/**
 * Checks whether or not str matches with match.
 * All inputs must be NULL-terminated (or NULL to never match).
 * Comparison is case insensitive.
 */
static inline bool gfx_str_match_(const char* str, const char* match)
{
	if (str == NULL || match == NULL) return 0;

	for (; *str != '\0' && *match != '\0'; ++str, ++match)
		if (tolower(*str) != tolower(*match)) break;

	return *str == '\0' && *match == '\0';
}

/**
 * Checks whether str is a `false` value or not.
 * Input must be NULL-terminated (or NULL to never match).
 * Comparison is case insensitive.
 */
static inline bool gfx_str_is_false_(const char* str)
{
	const char* falseValues[] = { "false", "off", "no", "f", "n", "0" };

	for (size_t i = 0; i < sizeof(falseValues)/sizeof(char*); ++i)
		if (gfx_str_match_(str, falseValues[i])) return 1;

	return 0;
}

/**
 * Checks whether str is a `true` value or not.
 * Input must be NULL-terminated (or NULL to never match).
 * Comparison is case insensitive.
 */
static inline bool gfx_str_is_true_(const char* str)
{
	const char* trueValues[] = { "true", "on", "yes", "t", "y", "1" };

	for (size_t i = 0; i < sizeof(trueValues)/sizeof(char*); ++i)
		if (gfx_str_match_(str, trueValues[i])) return 1;

	return 0;
}

/**
 * Finds whether or not substr occurs in str.
 * All inputs must be NULL-terminated (or NULL to never match).
 * Comparison is case insensitive.
 */
static inline bool gfx_str_has_substr_(const char* str, const char* substr)
{
	if (str == NULL || substr == NULL) return 0;

	const char* needle = substr;
	const char* found = NULL;

	while (*str != '\0' && *needle != '\0')
	{
		if (tolower(*str) == tolower(*needle))
		{
			if (found == NULL) found = str;
			++needle;
		}
		else
		{
			if (found != NULL) str = found;
			needle = substr;
			found = NULL;
		}

		++str;
	}

	return *needle == '\0';
}


#endif
