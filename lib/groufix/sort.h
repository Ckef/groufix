/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 *
 * This file contains a header-only introsort (and relatives).
 * Use the following macros to influence the implementation:
 *
 * GFX_SORT_THRESHOLD_
 *   Threshold at which introsort switches to insertion sort.
 *
 * GFX_SORT_LIST_T_
 *   Type of the list to be sorted.
 *
 * GFX_SORT_ELEM_T_
 *   Type of an element from the list to be compared.
 *
 * GFX_SORT_GET_ELEM_(list, index)
 *   Get an element type from a list type by size_t index.
 *
 * GFX_SORT_CMP_(lElem, rElem)
 *   Compares two element types, < 0 if l < r, > 0 if l > r, 0 if l == r.
 *
 * GFX_SORT_CMP_LIST_(list, lElem, rElem)
 *   Compares two element types with the list type passed as first arg.
 *
 * GFX_SORT_SWAP_(list, lIndex, rIndex)
 *   Swaps two elements of a list type by size_t indices.
 *
 * Default values implement sorting an `int` array.
 */


#ifndef GFX_SORT_H_
#define GFX_SORT_H_

#include "groufix/def.h"
#include <limits.h>


// Introsort threshold macro.
#ifndef GFX_SORT_THRESHOLD_
	#define GFX_SORT_THRESHOLD_ 16
#endif


// Sorted list type macro.
#ifndef GFX_SORT_LIST_T_
	#define GFX_SORT_LIST_T_ int*
#endif

// Sorted list element type macro.
#ifndef GFX_SORT_ELEM_T_
	#define GFX_SORT_ELEM_T_ int
#endif

// Sorted list element getter macro.
#ifndef GFX_SORT_GET_ELEM_
	#define GFX_SORT_GET_ELEM_(list, index) list[index]
#endif

// Sorted list element comparison macro.
#ifndef GFX_SORT_CMP_LIST_
	#if defined GFX_SORT_CMP_
		#define GFX_SORT_CMP_LIST_(list, lElem, rElem) \
			GFX_SORT_CMP_(lElem, rElem)
	#else
		#define GFX_SORT_CMP_LIST_(list, lElem, rElem) \
			(lElem < rElem ? -1 : lElem > rElem ? 1 : 0)
	#endif
#endif

// Sorted list element swap macro.
#ifndef GFX_SORT_SWAP_
	#define GFX_SORT_SWAP_(list, lIndex, rIndex) \
		do { \
			GFX_SORT_ELEM_T_ t = list[lIndex]; \
			list[lIndex] = list[rIndex]; \
			list[rIndex] = t; \
		} while (0)
#endif


/**
 * Fast compiler agnostic implementation of floor(log2(n)).
 * @param n Cannot be 0.
 */
static inline int gfx_log2_(size_t n)
{
	assert(n != 0);

#if defined (__GNUC__) || defined (__clang__)
	// We need to get the position of the highest set bit.
	// Count leading 0s, invert, subtract 1.
	{
	#if SIZE_MAX == ULLONG_MAX
		return (int)sizeof(unsigned long long) * CHAR_BIT - __builtin_clzll(n) - 1;
	#elif SIZE_MAX == ULONG_MAX
		return (int)sizeof(unsigned long) * CHAR_BIT - __builtin_clzl(n) - 1;
	#elif SIZE_MAX == UINT_MAX
		return (int)sizeof(unsigned int) * CHAR_BIT - __builtin_clz(n) - 1;
	#else
		#error "Host compilier does not support __builtin_clz."
	#endif
	}

#else
	int k = 0;
	while (n >>= 1) ++k;

	return k;

#endif
}

/**
 * Insertion sort a list of elements.
 * @param r Exclusive boundary, must be >= l.
 */
static void gfx_insertsort_(GFX_SORT_LIST_T_ list, size_t l, size_t r)
{
	assert(r >= l);

	// Loop over all elements.
	for (size_t i = l + 1; i < r; ++i)
	{
		const GFX_SORT_ELEM_T_ iElem = GFX_SORT_GET_ELEM_(list, i);

		// For each one, swap-insert it back into the sorted list.
		for (size_t j = i; j > l; --j)
		{
			const GFX_SORT_ELEM_T_ jElem = GFX_SORT_GET_ELEM_(list, j - 1);

			if (GFX_SORT_CMP_LIST_(list, jElem, iElem) > 0)
				GFX_SORT_SWAP_(list, j, j - 1);
			else
				break;
		}
	}
}

/**
 * Max-heapify a list of elements.
 * Must pass at least one element!
 * @param r Exclusive boundary, must be > l.
 */
static void gfx_heapify_(GFX_SORT_LIST_T_ list, size_t l, size_t r)
{
	assert(r > l);

	size_t largest = l;

	// Check if left child is larger.
	if (l + 1 < r)
	{
		const GFX_SORT_ELEM_T_ pElem = GFX_SORT_GET_ELEM_(list, largest);
		const GFX_SORT_ELEM_T_ iElem = GFX_SORT_GET_ELEM_(list, l + 1);
		if (GFX_SORT_CMP_LIST_(list, iElem, pElem) > 0)
			largest = l + 1;
	}

	// Check if right child is larger.
	if (l + 2 < r)
	{
		const GFX_SORT_ELEM_T_ pElem = GFX_SORT_GET_ELEM_(list, largest);
		const GFX_SORT_ELEM_T_ jElem = GFX_SORT_GET_ELEM_(list, l + 2);
		if (GFX_SORT_CMP_LIST_(list, jElem, pElem) > 0)
			largest = l + 2;
	}

	// Parent is not largest, swap.
	if (largest != l)
	{
		GFX_SORT_SWAP_(list, l, largest);
		gfx_heapify_(list, largest, r);
	}
}

/**
 * Heap sort a list of elements.
 * @param r Exclusive boundary, must be >= l.
 */
static void gfx_heapsort_(GFX_SORT_LIST_T_ list, size_t l, size_t r)
{
	assert(r >= l);

	// Build max-heap.
	for (size_t i = l + ((r - l) >> 1); i > l; --i)
		gfx_heapify_(list, i - 1, r);

	// Extract largest elements to the ending.
	for (size_t i = r; i > l + 1; --i)
	{
		GFX_SORT_SWAP_(list, l, i - 1);
		gfx_heapify_(list, l, i - 1);
	}
}

/**
 * Divides a list of elements in two.
 * Must pass at least one element!
 * @param r Exclusive boundary, must be > l.
 * @return The used pivot element.
 */
static size_t gfx_partition_(GFX_SORT_LIST_T_ list, size_t l, size_t r)
{
	assert(r > l);

	// Use the middle as pivot.
	const size_t piv = l + ((r - l) >> 1);
	const GFX_SORT_ELEM_T_ pivElem = GFX_SORT_GET_ELEM_(list, piv);

	// Swap pivot to the beginning of the list.
	// So we never compare the same elements, in case it is expensive!
	if (piv != l) GFX_SORT_SWAP_(list, piv, l);

	// Perform partition, omit pivot.
	size_t i = l + 1;
	size_t j = r - 1; // Make inclusive.

	while (i <= j)
	{
		// Move left index to the right as long as
		// its element belongs left of the pivot.
		while (i <= j)
		{
			const GFX_SORT_ELEM_T_ iElem = GFX_SORT_GET_ELEM_(list, i);
			if (GFX_SORT_CMP_LIST_(list, iElem, pivElem) < 0)
				++i;
			else
				break;
		}

		// Move right index to the left as long as
		// its element belongs right of the pivot.
		while (i <= j)
		{
			const GFX_SORT_ELEM_T_ jElem = GFX_SORT_GET_ELEM_(list, j);
			if (GFX_SORT_CMP_LIST_(list, jElem, pivElem) > 0)
				--j;
			else
				break;
		}

		// If they crossed, we're done.
		if (i >= j) break;

		// If not, swap and continue.
		GFX_SORT_SWAP_(list, i, j);
		++i, --j;
	}

	// Swap back pivot to its correct position.
	if (j != l) GFX_SORT_SWAP_(list, j, l);

	return j;
}

/**
 * Introspective sort a list of elements.
 * @param r Exclusive boundary, must be >= l.
 */
static void gfx_introsort_(GFX_SORT_LIST_T_ list, size_t l, size_t r, int maxDepth)
{
	assert(r >= l);

	// Course of action depends on #elements to sort.
	const size_t len = r - l;

	if (len <= GFX_SORT_THRESHOLD_)
		// Use insertion sort if we have less than the threshold.
		gfx_insertsort_(list, l, r);
	else if (maxDepth <= 0)
		// Use heap sort if we exceeded max recursion depth.
		gfx_heapsort_(list, l, r);
	else
	{
		// Quick-sort partition one level deeper otherwise.
		const size_t piv = gfx_partition_(list, l, r);

		gfx_introsort_(list, l, piv, maxDepth - 1);
		gfx_introsort_(list, piv + 1, r, maxDepth - 1);
	}
}

/**
 * Sort a list of elements.
 * @param r Exclusive boundary, must be >= l.
 */
static inline void gfx_sort_(GFX_SORT_LIST_T_ list, size_t l, size_t r)
{
	assert(r >= l);

	// Nothing to do if we have less than two elements.
	const size_t len = r - l;

	if (len > 1)
		gfx_introsort_(list, l, r, gfx_log2_(len) * 2);
}


#endif
