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
 * GFX_SORT_CMP_(const lElem, const rElem)
 *   Compares two element types, < 0 if l < r, > 0 if l > r, 0 if l == r.
 *
 * GFX_SORT_CMP_LIST_(list, const lElem, const rElem)
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
		#error "Host compiler does not support appropriate __builtin_clz."
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
 * Max-heapify a list of elements, i.e. sift down an element.
 * @param s Element to sift down, must be < len.
 */
static void gfx_heapify_(GFX_SORT_LIST_T_ list, size_t l, size_t len, size_t s)
{
	assert(s < len);

	size_t largest = s;
	const size_t i = (s << 1) + 1;
	const size_t j = (s << 1) + 2;

	// Check if left child is larger.
	if (i < len)
	{
		const GFX_SORT_ELEM_T_ sElem = GFX_SORT_GET_ELEM_(list, l + s);
		const GFX_SORT_ELEM_T_ iElem = GFX_SORT_GET_ELEM_(list, l + i);
		if (GFX_SORT_CMP_LIST_(list, iElem, sElem) > 0)
			largest = i;

		// Check if right child is larger.
		if (j < len)
		{
			const GFX_SORT_ELEM_T_ tElem = largest == s ? sElem : iElem;
			const GFX_SORT_ELEM_T_ jElem = GFX_SORT_GET_ELEM_(list, l + j);
			if (GFX_SORT_CMP_LIST_(list, jElem, tElem) > 0)
				largest = j;
		}

		// Parent is not largest, swap.
		if (largest != s)
		{
			GFX_SORT_SWAP_(list, l + s, l + largest);
			gfx_heapify_(list, l, len, largest);
		}
	}
}

/**
 * Heap sort a list of elements.
 * @param r Exclusive boundary, must be >= l.
 */
static void gfx_heapsort_(GFX_SORT_LIST_T_ list, size_t l, size_t r)
{
	assert(r >= l);

	const size_t len = r - l;

	// Build max-heap.
	for (size_t i = len >> 1; i > 0; --i)
		gfx_heapify_(list, l, len, i - 1);

	// Extract largest elements to the ending.
	for (size_t i = len; i > 1; --i)
	{
		GFX_SORT_SWAP_(list, l, l + i - 1);
		gfx_heapify_(list, l, i - 1, 0);
	}
}

/**
 * Uses fat partitioning to divide a list of elements into three.
 * @param r  Exclusive boundary, must be > l.
 * @param lt Output boundary, left of is < right.
 * @param gt Output boundary, right of is > left.
 */
static void gfx_partition_(GFX_SORT_LIST_T_ list,
                           size_t l, size_t r, size_t* lt, size_t* gt)
{
	assert(r > l);
	assert(lt != NULL);
	assert(gt != NULL);

	// Use the middle as pivot.
	size_t piv = l + ((r - l) >> 1);
	const GFX_SORT_ELEM_T_ pivElem = GFX_SORT_GET_ELEM_(list, piv);

	// Perform partition.
	size_t eq = l;
	*lt = l;
	*gt = r;

	while (eq < *gt)
	{
		if (eq == piv)
		{
			// Pivot element, skip comparison.
			eq = eq + 1;
			continue;
		}

		// Compare to pivot.
		const GFX_SORT_ELEM_T_ eqElem = GFX_SORT_GET_ELEM_(list, eq);
		const int cmp = GFX_SORT_CMP_LIST_(list, eqElem, pivElem);

		if (cmp < 0)
		{
			// < pivot, move to left region.
			if (eq != *lt)
				GFX_SORT_SWAP_(list, eq, *lt);

			*lt = *lt + 1;
			eq = eq + 1;
		}
		else if (cmp > 0)
		{
			// > pivot, move to right region.
			*gt = *gt - 1;

			if (eq != *gt)
				GFX_SORT_SWAP_(list, eq, *gt);

			// Potentially moved the pivot, fast skip.
			if (piv == *gt)
			{
				piv = eq;
				eq = eq + 1;
			}
		}
		else
		{
			// == pivot, leave in middle region.
			eq = eq + 1;
		}
	}
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
		// We use fat partitioning, so we get 3 regions.
		size_t lt;
		size_t gt;
		gfx_partition_(list, l, r, &lt, &gt);

		// Recurse on the left and righ regions.
		gfx_introsort_(list, l, lt, maxDepth - 1);
		gfx_introsort_(list, gt, r, maxDepth - 1);
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
