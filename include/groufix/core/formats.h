/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_FORMATS_H
#define GFX_CORE_FORMATS_H


/**
 * Numeric format interpretation.
 */
typedef enum GFXFormatType
{
	GFX_UNORM,   // uint -> float [0,1]
	GFX_SNORM,   // int -> float  [-1,1]
	GFX_USCALED, // uint -> float [0,2^n-1]
	GFX_SSCALED, // int -> float  [0,2^n-1]
	GFX_UINT,    // uint -> uint
	GFX_SINT,    // int -> int
	GFX_UFLOAT,  // ufloat -> float
	GFX_SFLOAT,  // float -> float
	GFX_SRGB     // sRGB-uint, Alpha-uint -> float [0,1]

} GFXFormatType;


/**
 * Format component order (defines `comps` in GFXFormat).
 */
typedef enum GFXOrder
{
	GFX_ORDER_R,
	GFX_ORDER_RG,
	GFX_ORDER_RGB,
	GFX_ORDER_BGR,
	GFX_ORDER_RGBA,
	GFX_ORDER_BGRA,
	GFX_ORDER_ARGB,
	GFX_ORDER_ABGR,
	GFX_ORDER_EBGR, // comps = [shared exponent, bgr]
	GFX_ORDER_DEPTH,
	GFX_ORDER_STENCIL,

	// Compression 'orders'.
	GFX_ORDER_BCn,    // comps = [n (1|2|3|4|5|6|7), -]
	GFX_ORDER_ETC2_RGB,
	GFX_ORDER_ETC2_RGBA,
	GFX_ORDER_EAC_R,
	GFX_ORDER_EAC_RG,
	GFX_ORDER_ASTC    // comps = [block-size-x, block-size-y, -]

} GFXOrder;


/**
 * Memory (buffer or image) format.
 */
typedef struct GFXFormat
{
	unsigned char comps[4]; // Components, depth in bits (or @see GFXOrder).

	GFXFormatType type;
	GFXOrder      order;

} GFXFormat;


#endif
