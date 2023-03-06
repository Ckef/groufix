/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>


/****************************/
size_t _gfx_str_join_len(size_t num, const char** strs, const char* delim)
{
	assert(num == 0 || strs != NULL);

	// At least 1 character for the terminating NULL.
	size_t len = 1;

	// Count the lengths of all strings to concat.
	for (size_t i = 0; i < num; ++i) len += strlen(strs[i]);

	// And add the length of the delimiter a bunch of times.
	return len + (delim && num > 0 ? strlen(delim) * (num - 1) : 0);
}

/****************************/
char* _gfx_str_join(char* dest, size_t num, const char** strs, const char* delim)
{
	assert(num == 0 || strs != NULL);

	char* out = dest; // So we can return 'dest'.

	for (size_t i = 0; i < num; ++i)
	{
		// Copy string.
		const char* str = strs[i];
		while (*str != '\0') *(out++) = *(str++);

		// Copy delimiter.
		if (delim && i + 1 < num)
		{
			const char* del = delim;
			while (*del != '\0') *(out++) = *(del++);
		}
	}

	// Terminate string.
	*out = '\0';

	return dest;
}

/****************************/
char* _gfx_str_join_alloc(size_t num, const char** strs, const char* delim)
{
	assert(num == 0 || strs != NULL);

	// Get length, allocate & join.
	size_t len = _gfx_str_join_len(num, strs, delim);
	if (len <= 1) return NULL;

	char* str = malloc(len);
	if (str == NULL) return NULL;

	return _gfx_str_join(str, num, strs, delim);
}
