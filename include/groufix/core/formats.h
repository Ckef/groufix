/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_FORMATS_H
#define GFX_CORE_FORMATS_H

#include "groufix/core/device.h"
#include "groufix/def.h"


/**
 * Numeric format interpretation.
 * Use as flags for combined formats & fuzzy types.
 */
typedef enum GFXFormatType
{
	GFX_UNORM   = 0x0001, // uint -> float [0,1]
	GFX_SNORM   = 0x0002, // int -> float  [-1,1]
	GFX_USCALED = 0x0004, // uint -> float [0,2^n-1]
	GFX_SSCALED = 0x0008, // int -> float  [-2^(n-1),2^(n-1)-1]
	GFX_UINT    = 0x0010, // uint -> uint
	GFX_SINT    = 0x0020, // int -> int
	GFX_UFLOAT  = 0x0040, // ufloat -> float
	GFX_SFLOAT  = 0x0080, // float -> float
	GFX_SRGB    = 0x0100  // sRGB-uint, Alpha-uint -> float [0,1]

} GFXFormatType;


/**
 * Format component order (defines `comps` in GFXFormat).
 * Use as flags for fuzzy orders.
 */
typedef enum GFXOrder
{
	GFX_ORDER_R    = 0x000001,
	GFX_ORDER_RG   = 0x000002,
	GFX_ORDER_RGB  = 0x000004,
	GFX_ORDER_BGR  = 0x000008,
	GFX_ORDER_RGBA = 0x000010,
	GFX_ORDER_BGRA = 0x000020,
	GFX_ORDER_ARGB = 0x000040,
	GFX_ORDER_ABGR = 0x000080,
	GFX_ORDER_EBGR = 0x000100, // comps = [shared exponent, bgr]

	GFX_ORDER_DEPTH         = 0x000200,
	GFX_ORDER_STENCIL       = 0x000400,
	GFX_ORDER_DEPTH_STENCIL = 0x000800,

	// Compression 'orders' (disjoint, 3 non-flag bits).
	GFX_ORDER_BCn  = 0x001000, // comps = [n (1|2|3|4|5|6|7), alpha (0|1), -]
	GFX_ORDER_ETC2 = 0x002000, // comps = [rgba]
	GFX_ORDER_EAC  = 0x003000, // comps = [rg]
	GFX_ORDER_ASTC = 0x004000  // comps = [block width, block height, -]

	// TODO: Add YUV/YCbCr support?

} GFXOrder;


/**
 * Memory (buffer or image) format(s).
 * Uses flags to represent 'fuzzy' set of related formats.
 */
typedef struct GFXFormat
{
	unsigned char comps[4]; // Components, depth in bits (or @see GFXOrder).

	GFXFormatType type;
	GFXOrder      order;

} GFXFormat;


/****************************
 * Format operations & search.
 ****************************/

/**
 * Format feature support flags.
 */
typedef enum GFXFormatFeatures
{
	GFX_FORMAT_VERTEX_BUFFER        = 0x0001,
	GFX_FORMAT_UNIFORM_TEXEL_BUFFER = 0x0002,
	GFX_FORMAT_STORAGE_TEXEL_BUFFER = 0x0004,
	GFX_FORMAT_SAMPLED_IMAGE        = 0x0008,
	GFX_FORMAT_SAMPLED_IMAGE_LINEAR = 0x0010,
	GFX_FORMAT_SAMPLED_IMAGE_MINMAX = 0x0020,
	GFX_FORMAT_STORAGE_IMAGE        = 0x0040,
	GFX_FORMAT_ATTACHMENT           = 0x0080, // Includes depth/stencil attachments.
	GFX_FORMAT_ATTACHMENT_BLEND     = 0x0100,
	GFX_FORMAT_IMAGE_READ           = 0x0200,
	GFX_FORMAT_IMAGE_WRITE          = 0x0400,

} GFXFormatFeatures;


/**
 * Fuzzy search flags.
 */
typedef enum GFXFuzzyFlags
{
	GFX_FUZZY_MIN_DEPTH    = 0x0001,
	GFX_FUZZY_MAX_DEPTH    = 0x0002,
	GFX_FUZZY_STRICT_DEPTH = 0x0003 // Both MIN_DEPTH and MAX_DEPTH.

} GFXFuzzyFlags;


/**
 * Retrieves the features supported by a given format.
 * If a format represents a 'fuzzy' set, for each feature it checks whether
 * there is at least one format in this set that is supported.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @return Zero if fmt is not supported.
 */
GFX_API GFXFormatFeatures gfx_format_support(GFXFormat fmt,
                                             GFXDevice* device);

/**
 * Performs a fuzzy search over all supported formats, i.e. it will return
 * the closest matching (component wise, ignoring empty components) format.
 * @param fmt      Format to match, type and order must strictly match.
 * @param flags    Flags to influence the fuzzy search.
 * @param features The minimal supported feature set of the returned format.
 * @param device   NULL is equivalent to gfx_get_primary_device().
 * @return GFX_FORMAT_EMPTY if no match found.
 *
 * Note: If fmt is a 'fuzzy' set, it will prefer returning formats that are
 * contained within this set. However this fuzzy search will search outside
 * the defined set as well.
 */
GFX_API GFXFormat gfx_format_fuzzy(GFXFormat fmt, GFXFuzzyFlags flags,
                                   GFXFormatFeatures features,
                                   GFXDevice* device);


/****************************
 * Format constants & macros.
 ****************************/

/**
 * Empty format macro (i.e. undefined) & checkers.
 * Checkers cannot take constants (including GFX_FORMAT_EMPTY) as argument!
 */
#define GFX_FORMAT_EMPTY \
	(GFXFormat){ .comps = {0,0,0,0}, .type = 0, .order = 0 }

#define GFX_FORMAT_IS_EMPTY(fmt) \
	((fmt).comps[0] == 0 && \
	(fmt).comps[1] == 0 && \
	(fmt).comps[2] == 0 && \
	(fmt).comps[3] == 0 && \
	(fmt).type == 0 && (fmt).order == 0)

#define GFX_FORMAT_IS_COMPRESSED(fmt) \
	((fmt).order == GFX_ORDER_BCn || \
	(fmt).order == GFX_ORDER_ETC2 || \
	(fmt).order == GFX_ORDER_EAC || \
	(fmt).order == GFX_ORDER_ASTC)

#define GFX_FORMAT_HAS_DEPTH(fmt) \
	((fmt).order & (GFX_ORDER_DEPTH | GFX_ORDER_DEPTH_STENCIL))

#define GFX_FORMAT_HAS_STENCIL(fmt) \
	((fmt).order & (GFX_ORDER_STENCIL | GFX_ORDER_DEPTH_STENCIL))

#define GFX_FORMAT_IS_EQUAL(fmta, fmtb) \
	((fmta).comps[0] == (fmtb).comps[0] && \
	(fmta).comps[1] == (fmtb).comps[1] && \
	(fmta).comps[2] == (fmtb).comps[2] && \
	(fmta).comps[3] == (fmtb).comps[3] && \
	(fmta).type == (fmtb).type && (fmta).order == (fmtb).order)

#define GFX_FORMAT_IS_CONTAINED(fmta, fmtb) \
	(((fmta).type & (fmtb).type) == (fmta).type && \
	(GFX_FORMAT_IS_COMPRESSED(fmta) ? \
		(fmta).comps[0] == (fmtb).comps[0] && \
		(fmta).comps[1] == (fmtb).comps[1] && \
		(fmta).comps[2] == (fmtb).comps[2] && \
		(fmta).comps[3] == (fmtb).comps[3] && \
		(fmta).order == (fmtb).order : \
		((fmta).comps[0] == 0 || (fmta).comps[0] == (fmtb).comps[0]) && \
		((fmta).comps[1] == 0 || (fmta).comps[1] == (fmtb).comps[1]) && \
		((fmta).comps[2] == 0 || (fmta).comps[2] == (fmtb).comps[2]) && \
		((fmta).comps[3] == 0 || (fmta).comps[3] == (fmtb).comps[3]) && \
		((fmta).order & (fmtb).order) == (fmta).order))


/**
 * Compute the texel block size in bits (i.e. total depth).
 * For compressed formats a 'block' contains multiple texels.
 * Computes the largest size if fmt is a 'fuzzy' set.
 */
#define GFX_FORMAT_BLOCK_DEPTH(fmt) \
	((fmt).order == GFX_ORDER_BCn ? \
		((fmt).comps[0] == 1 ? (int)64 : \
		(fmt).comps[0] == 2 ? (int)128 : \
		(fmt).comps[0] == 3 ? (int)128 : \
		(fmt).comps[0] == 4 ? (int)64 : \
		(fmt).comps[0] == 5 ? (int)128 : \
		(fmt).comps[0] == 6 ? (int)128 : \
		(fmt).comps[0] == 7 ? (int)128 : (int)0) : \
	(fmt).order == GFX_ORDER_ETC2 ? \
		(int)64 : \
	(fmt).order == GFX_ORDER_EAC ? \
		((fmt).comps[1] == 0 ? (int)64 : (int)128) : \
	(fmt).order == GFX_ORDER_ASTC ? \
		(int)128 : \
	((int)(fmt).comps[0] + (int)(fmt).comps[1] + \
	(int)(fmt).comps[2] + (int)(fmt).comps[3]))


/**
 * Format macros, i.e. constant GFXFormat definitions.
 * Mirrors all Vulkan formats (the subset that groufix supports).
 * Note: some parts of the Vulkan identifiers are omitted.
 */
#define GFX_FORMAT_R4G4_UNORM \
	(GFXFormat){ {4,4,0,0}, GFX_UNORM, GFX_ORDER_RG }
#define GFX_FORMAT_R4G4B4A4_UNORM \
	(GFXFormat){ {4,4,4,4}, GFX_UNORM, GFX_ORDER_RGBA }
#define GFX_FORMAT_B4G4R4A4_UNORM \
	(GFXFormat){ {4,4,4,4}, GFX_UNORM, GFX_ORDER_BGRA }
#define GFX_FORMAT_R5G6B5_UNORM \
	(GFXFormat){ {5,6,5,0}, GFX_UNORM, GFX_ORDER_RGB }
#define GFX_FORMAT_B5G6R5_UNORM \
	(GFXFormat){ {5,6,5,0}, GFX_UNORM, GFX_ORDER_BGR }
#define GFX_FORMAT_R5G5B5A1_UNORM \
	(GFXFormat){ {5,5,5,1}, GFX_UNORM, GFX_ORDER_RGBA }
#define GFX_FORMAT_B5G5R5A1_UNORM \
	(GFXFormat){ {5,5,5,1}, GFX_UNORM, GFX_ORDER_BGRA }
#define GFX_FORMAT_A1R5G5B5_UNORM \
	(GFXFormat){ {1,5,5,5}, GFX_UNORM, GFX_ORDER_ARGB }

#define GFX_FORMAT_R8_UNORM \
	(GFXFormat){ {8,0,0,0}, GFX_UNORM, GFX_ORDER_R }
#define GFX_FORMAT_R8_SNORM \
	(GFXFormat){ {8,0,0,0}, GFX_SNORM, GFX_ORDER_R }
#define GFX_FORMAT_R8_USCALED \
	(GFXFormat){ {8,0,0,0}, GFX_USCALED, GFX_ORDER_R }
#define GFX_FORMAT_R8_SSCALED \
	(GFXFormat){ {8,0,0,0}, GFX_SSCALED, GFX_ORDER_R }
#define GFX_FORMAT_R8_UINT \
	(GFXFormat){ {8,0,0,0}, GFX_UINT, GFX_ORDER_R }
#define GFX_FORMAT_R8_SINT \
	(GFXFormat){ {8,0,0,0}, GFX_SINT, GFX_ORDER_R }
#define GFX_FORMAT_R8_SRGB \
	(GFXFormat){ {8,0,0,0}, GFX_SRGB, GFX_ORDER_R }

#define GFX_FORMAT_R8G8_UNORM \
	(GFXFormat){ {8,8,0,0}, GFX_UNORM, GFX_ORDER_RG }
#define GFX_FORMAT_R8G8_SNORM \
	(GFXFormat){ {8,8,0,0}, GFX_SNORM, GFX_ORDER_RG }
#define GFX_FORMAT_R8G8_USCALED \
	(GFXFormat){ {8,8,0,0}, GFX_USCALED, GFX_ORDER_RG }
#define GFX_FORMAT_R8G8_SSCALED \
	(GFXFormat){ {8,8,0,0}, GFX_SSCALED, GFX_ORDER_RG }
#define GFX_FORMAT_R8G8_UINT \
	(GFXFormat){ {8,8,0,0}, GFX_UINT, GFX_ORDER_RG }
#define GFX_FORMAT_R8G8_SINT \
	(GFXFormat){ {8,8,0,0}, GFX_SINT, GFX_ORDER_RG }
#define GFX_FORMAT_R8G8_SRGB \
	(GFXFormat){ {8,8,0,0}, GFX_SRGB, GFX_ORDER_RG }

#define GFX_FORMAT_R8G8B8_UNORM \
	(GFXFormat){ {8,8,8,0}, GFX_UNORM, GFX_ORDER_RGB }
#define GFX_FORMAT_R8G8B8_SNORM \
	(GFXFormat){ {8,8,8,0}, GFX_SNORM, GFX_ORDER_RGB }
#define GFX_FORMAT_R8G8B8_USCALED \
	(GFXFormat){ {8,8,8,0}, GFX_USCALED, GFX_ORDER_RGB }
#define GFX_FORMAT_R8G8B8_SSCALED \
	(GFXFormat){ {8,8,8,0}, GFX_SSCALED, GFX_ORDER_RGB }
#define GFX_FORMAT_R8G8B8_UINT \
	(GFXFormat){ {8,8,8,0}, GFX_UINT, GFX_ORDER_RGB }
#define GFX_FORMAT_R8G8B8_SINT \
	(GFXFormat){ {8,8,8,0}, GFX_SINT, GFX_ORDER_RGB }
#define GFX_FORMAT_R8G8B8_SRGB \
	(GFXFormat){ {8,8,8,0}, GFX_SRGB, GFX_ORDER_RGB }

#define GFX_FORMAT_B8G8R8_UNORM \
	(GFXFormat){ {8,8,8,0}, GFX_UNORM, GFX_ORDER_BGR }
#define GFX_FORMAT_B8G8R8_SNORM \
	(GFXFormat){ {8,8,8,0}, GFX_SNORM, GFX_ORDER_BGR }
#define GFX_FORMAT_B8G8R8_USCALED \
	(GFXFormat){ {8,8,8,0}, GFX_USCALED, GFX_ORDER_BGR }
#define GFX_FORMAT_B8G8R8_SSCALED \
	(GFXFormat){ {8,8,8,0}, GFX_SSCALED, GFX_ORDER_BGR }
#define GFX_FORMAT_B8G8R8_UINT \
	(GFXFormat){ {8,8,8,0}, GFX_UINT, GFX_ORDER_BGR }
#define GFX_FORMAT_B8G8R8_SINT \
	(GFXFormat){ {8,8,8,0}, GFX_SINT, GFX_ORDER_BGR }
#define GFX_FORMAT_B8G8R8_SRGB \
	(GFXFormat){ {8,8,8,0}, GFX_SRGB, GFX_ORDER_BGR }

#define GFX_FORMAT_R8G8B8A8_UNORM \
	(GFXFormat){ {8,8,8,8}, GFX_UNORM, GFX_ORDER_RGBA }
#define GFX_FORMAT_R8G8B8A8_SNORM \
	(GFXFormat){ {8,8,8,8}, GFX_SNORM, GFX_ORDER_RGBA }
#define GFX_FORMAT_R8G8B8A8_USCALED \
	(GFXFormat){ {8,8,8,8}, GFX_USCALED, GFX_ORDER_RGBA }
#define GFX_FORMAT_R8G8B8A8_SSCALED \
	(GFXFormat){ {8,8,8,8}, GFX_SSCALED, GFX_ORDER_RGBA }
#define GFX_FORMAT_R8G8B8A8_UINT \
	(GFXFormat){ {8,8,8,8}, GFX_UINT, GFX_ORDER_RGBA }
#define GFX_FORMAT_R8G8B8A8_SINT \
	(GFXFormat){ {8,8,8,8}, GFX_SINT, GFX_ORDER_RGBA }
#define GFX_FORMAT_R8G8B8A8_SRGB \
	(GFXFormat){ {8,8,8,8}, GFX_SRGB, GFX_ORDER_RGBA }

#define GFX_FORMAT_B8G8R8A8_UNORM \
	(GFXFormat){ {8,8,8,8}, GFX_UNORM, GFX_ORDER_BGRA }
#define GFX_FORMAT_B8G8R8A8_SNORM \
	(GFXFormat){ {8,8,8,8}, GFX_SNORM, GFX_ORDER_BGRA }
#define GFX_FORMAT_B8G8R8A8_USCALED \
	(GFXFormat){ {8,8,8,8}, GFX_USCALED, GFX_ORDER_BGRA }
#define GFX_FORMAT_B8G8R8A8_SSCALED \
	(GFXFormat){ {8,8,8,8}, GFX_SSCALED, GFX_ORDER_BGRA }
#define GFX_FORMAT_B8G8R8A8_UINT \
	(GFXFormat){ {8,8,8,8}, GFX_UINT, GFX_ORDER_BGRA }
#define GFX_FORMAT_B8G8R8A8_SINT \
	(GFXFormat){ {8,8,8,8}, GFX_SINT, GFX_ORDER_BGRA }
#define GFX_FORMAT_B8G8R8A8_SRGB \
	(GFXFormat){ {8,8,8,8}, GFX_SRGB, GFX_ORDER_BGRA }

#define GFX_FORMAT_A8B8G8R8_UNORM \
	(GFXFormat){ {8,8,8,8}, GFX_UNORM, GFX_ORDER_ABGR }
#define GFX_FORMAT_A8B8G8R8_SNORM \
	(GFXFormat){ {8,8,8,8}, GFX_SNORM, GFX_ORDER_ABGR }
#define GFX_FORMAT_A8B8G8R8_USCALED \
	(GFXFormat){ {8,8,8,8}, GFX_USCALED, GFX_ORDER_ABGR }
#define GFX_FORMAT_A8B8G8R8_SSCALED \
	(GFXFormat){ {8,8,8,8}, GFX_SSCALED, GFX_ORDER_ABGR }
#define GFX_FORMAT_A8B8G8R8_UINT \
	(GFXFormat){ {8,8,8,8}, GFX_UINT, GFX_ORDER_ABGR }
#define GFX_FORMAT_A8B8G8R8_SINT \
	(GFXFormat){ {8,8,8,8}, GFX_SINT, GFX_ORDER_ABGR }
#define GFX_FORMAT_A8B8G8R8_SRGB \
	(GFXFormat){ {8,8,8,8}, GFX_SRGB, GFX_ORDER_ABGR }

#define GFX_FORMAT_A2R10G10B10_UNORM \
	(GFXFormat){ {2,10,10,10}, GFX_UNORM, GFX_ORDER_ARGB }
#define GFX_FORMAT_A2R10G10B10_SNORM \
	(GFXFormat){ {2,10,10,10}, GFX_SNORM, GFX_ORDER_ARGB }
#define GFX_FORMAT_A2R10G10B10_USCALED \
	(GFXFormat){ {2,10,10,10}, GFX_USCALED, GFX_ORDER_ARGB }
#define GFX_FORMAT_A2R10G10B10_SSCALED \
	(GFXFormat){ {2,10,10,10}, GFX_SSCALED, GFX_ORDER_ARGB }
#define GFX_FORMAT_A2R10G10B10_UINT \
	(GFXFormat){ {2,10,10,10}, GFX_UINT, GFX_ORDER_ARGB }
#define GFX_FORMAT_A2R10G10B10_SINT \
	(GFXFormat){ {2,10,10,10}, GFX_SINT, GFX_ORDER_ARGB }

#define GFX_FORMAT_A2B10G10R10_UNORM \
	(GFXFormat){ {2,10,10,10}, GFX_UNORM, GFX_ORDER_ABGR }
#define GFX_FORMAT_A2B10G10R10_SNORM \
	(GFXFormat){ {2,10,10,10}, GFX_SNORM, GFX_ORDER_ABGR }
#define GFX_FORMAT_A2B10G10R10_USCALED \
	(GFXFormat){ {2,10,10,10}, GFX_USCALED, GFX_ORDER_ABGR }
#define GFX_FORMAT_A2B10G10R10_SSCALED \
	(GFXFormat){ {2,10,10,10}, GFX_SSCALED, GFX_ORDER_ABGR }
#define GFX_FORMAT_A2B10G10R10_UINT \
	(GFXFormat){ {2,10,10,10}, GFX_UINT, GFX_ORDER_ABGR }
#define GFX_FORMAT_A2B10G10R10_SINT \
	(GFXFormat){ {2,10,10,10}, GFX_SINT, GFX_ORDER_ABGR }

#define GFX_FORMAT_R16_UNORM \
	(GFXFormat){ {16,0,0,0}, GFX_UNORM, GFX_ORDER_R }
#define GFX_FORMAT_R16_SNORM \
	(GFXFormat){ {16,0,0,0}, GFX_SNORM, GFX_ORDER_R }
#define GFX_FORMAT_R16_USCALED \
	(GFXFormat){ {16,0,0,0}, GFX_USCALED, GFX_ORDER_R }
#define GFX_FORMAT_R16_SSCALED \
	(GFXFormat){ {16,0,0,0}, GFX_SSCALED, GFX_ORDER_R }
#define GFX_FORMAT_R16_UINT \
	(GFXFormat){ {16,0,0,0}, GFX_UINT, GFX_ORDER_R }
#define GFX_FORMAT_R16_SINT \
	(GFXFormat){ {16,0,0,0}, GFX_SINT, GFX_ORDER_R }
#define GFX_FORMAT_R16_SFLOAT \
	(GFXFormat){ {16,0,0,0}, GFX_SFLOAT, GFX_ORDER_R }

#define GFX_FORMAT_R16G16_UNORM \
	(GFXFormat){ {16,16,0,0}, GFX_UNORM, GFX_ORDER_RG }
#define GFX_FORMAT_R16G16_SNORM \
	(GFXFormat){ {16,16,0,0}, GFX_SNORM, GFX_ORDER_RG }
#define GFX_FORMAT_R16G16_USCALED \
	(GFXFormat){ {16,16,0,0}, GFX_USCALED, GFX_ORDER_RG }
#define GFX_FORMAT_R16G16_SSCALED \
	(GFXFormat){ {16,16,0,0}, GFX_SSCALED, GFX_ORDER_RG }
#define GFX_FORMAT_R16G16_UINT \
	(GFXFormat){ {16,16,0,0}, GFX_UINT, GFX_ORDER_RG }
#define GFX_FORMAT_R16G16_SINT \
	(GFXFormat){ {16,16,0,0}, GFX_SINT, GFX_ORDER_RG }
#define GFX_FORMAT_R16G16_SFLOAT \
	(GFXFormat){ {16,16,0,0}, GFX_SFLOAT, GFX_ORDER_RG }

#define GFX_FORMAT_R16G16B16_UNORM \
	(GFXFormat){ {16,16,16,0}, GFX_UNORM, GFX_ORDER_RGB }
#define GFX_FORMAT_R16G16B16_SNORM \
	(GFXFormat){ {16,16,16,0}, GFX_SNORM, GFX_ORDER_RGB }
#define GFX_FORMAT_R16G16B16_USCALED \
	(GFXFormat){ {16,16,16,0}, GFX_USCALED, GFX_ORDER_RGB }
#define GFX_FORMAT_R16G16B16_SSCALED \
	(GFXFormat){ {16,16,16,0}, GFX_SSCALED, GFX_ORDER_RGB }
#define GFX_FORMAT_R16G16B16_UINT \
	(GFXFormat){ {16,16,16,0}, GFX_UINT, GFX_ORDER_RGB }
#define GFX_FORMAT_R16G16B16_SINT \
	(GFXFormat){ {16,16,16,0}, GFX_SINT, GFX_ORDER_RGB }
#define GFX_FORMAT_R16G16B16_SFLOAT \
	(GFXFormat){ {16,16,16,0}, GFX_SFLOAT, GFX_ORDER_RGB }

#define GFX_FORMAT_R16G16B16A16_UNORM \
	(GFXFormat){ {16,16,16,16}, GFX_UNORM, GFX_ORDER_RGBA }
#define GFX_FORMAT_R16G16B16A16_SNORM \
	(GFXFormat){ {16,16,16,16}, GFX_SNORM, GFX_ORDER_RGBA }
#define GFX_FORMAT_R16G16B16A16_USCALED \
	(GFXFormat){ {16,16,16,16}, GFX_USCALED, GFX_ORDER_RGBA }
#define GFX_FORMAT_R16G16B16A16_SSCALED \
	(GFXFormat){ {16,16,16,16}, GFX_SSCALED, GFX_ORDER_RGBA }
#define GFX_FORMAT_R16G16B16A16_UINT \
	(GFXFormat){ {16,16,16,16}, GFX_UINT, GFX_ORDER_RGBA }
#define GFX_FORMAT_R16G16B16A16_SINT \
	(GFXFormat){ {16,16,16,16}, GFX_SINT, GFX_ORDER_RGBA }
#define GFX_FORMAT_R16G16B16A16_SFLOAT \
	(GFXFormat){ {16,16,16,16}, GFX_SFLOAT, GFX_ORDER_RGBA }

#define GFX_FORMAT_R32_UINT \
	(GFXFormat){ {32,0,0,0}, GFX_UINT, GFX_ORDER_R }
#define GFX_FORMAT_R32_SINT \
	(GFXFormat){ {32,0,0,0}, GFX_SINT, GFX_ORDER_R }
#define GFX_FORMAT_R32_SFLOAT \
	(GFXFormat){ {32,0,0,0}, GFX_SFLOAT, GFX_ORDER_R }

#define GFX_FORMAT_R32G32_UINT \
	(GFXFormat){ {32,32,0,0}, GFX_UINT, GFX_ORDER_RG }
#define GFX_FORMAT_R32G32_SINT \
	(GFXFormat){ {32,32,0,0}, GFX_SINT, GFX_ORDER_RG }
#define GFX_FORMAT_R32G32_SFLOAT \
	(GFXFormat){ {32,32,0,0}, GFX_SFLOAT, GFX_ORDER_RG }

#define GFX_FORMAT_R32G32B32_UINT \
	(GFXFormat){ {32,32,32,0}, GFX_UINT, GFX_ORDER_RGB }
#define GFX_FORMAT_R32G32B32_SINT \
	(GFXFormat){ {32,32,32,0}, GFX_SINT, GFX_ORDER_RGB }
#define GFX_FORMAT_R32G32B32_SFLOAT \
	(GFXFormat){ {32,32,32,0}, GFX_SFLOAT, GFX_ORDER_RGB }

#define GFX_FORMAT_R32G32B32A32_UINT \
	(GFXFormat){ {32,32,32,32}, GFX_UINT, GFX_ORDER_RGBA }
#define GFX_FORMAT_R32G32B32A32_SINT \
	(GFXFormat){ {32,32,32,32}, GFX_SINT, GFX_ORDER_RGBA }
#define GFX_FORMAT_R32G32B32A32_SFLOAT \
	(GFXFormat){ {32,32,32,32}, GFX_SFLOAT, GFX_ORDER_RGBA }

#define GFX_FORMAT_R64_UINT \
	(GFXFormat){ {64,0,0,0}, GFX_UINT, GFX_ORDER_R }
#define GFX_FORMAT_R64_SINT \
	(GFXFormat){ {64,0,0,0}, GFX_SINT, GFX_ORDER_R }
#define GFX_FORMAT_R64_SFLOAT \
	(GFXFormat){ {64,0,0,0}, GFX_SFLOAT, GFX_ORDER_R }

#define GFX_FORMAT_R64G64_UINT \
	(GFXFormat){ {64,64,0,0}, GFX_UINT, GFX_ORDER_RG }
#define GFX_FORMAT_R64G64_SINT \
	(GFXFormat){ {64,64,0,0}, GFX_SINT, GFX_ORDER_RG }
#define GFX_FORMAT_R64G64_SFLOAT \
	(GFXFormat){ {64,64,0,0}, GFX_SFLOAT, GFX_ORDER_RG }

#define GFX_FORMAT_R64G64B64_UINT \
	(GFXFormat){ {64,64,64,0}, GFX_UINT, GFX_ORDER_RGB }
#define GFX_FORMAT_R64G64B64_SINT \
	(GFXFormat){ {64,64,64,0}, GFX_SINT, GFX_ORDER_RGB }
#define GFX_FORMAT_R64G64B64_SFLOAT \
	(GFXFormat){ {64,64,64,0}, GFX_SFLOAT, GFX_ORDER_RGB }

#define GFX_FORMAT_R64G64B64A64_UINT \
	(GFXFormat){ {64,64,64,64}, GFX_UINT, GFX_ORDER_RGBA }
#define GFX_FORMAT_R64G64B64A64_SINT \
	(GFXFormat){ {64,64,64,64}, GFX_SINT, GFX_ORDER_RGBA }
#define GFX_FORMAT_R64G64B64A64_SFLOAT \
	(GFXFormat){ {64,64,64,64}, GFX_SFLOAT, GFX_ORDER_RGBA }

#define GFX_FORMAT_B10G11R11_UFLOAT \
	(GFXFormat){ {10,11,11,0}, GFX_UFLOAT, GFX_ORDER_BGR }
#define GFX_FORMAT_E5B9G9R9_UFLOAT \
	(GFXFormat){ {5,9,9,9}, GFX_UFLOAT, GFX_ORDER_EBGR }

#define GFX_FORMAT_D16_UNORM \
	(GFXFormat){ {16,0,0,0}, GFX_UNORM, GFX_ORDER_DEPTH }
#define GFX_FORMAT_X8_D24_UNORM \
	(GFXFormat){ {8,24,0,0}, GFX_UNORM, GFX_ORDER_DEPTH }
#define GFX_FORMAT_D32_SFLOAT \
	(GFXFormat){ {32,0,0,0}, GFX_SFLOAT, GFX_ORDER_DEPTH }
#define GFX_FORMAT_S8_UINT \
	(GFXFormat){ {8,0,0,0}, GFX_UINT, GFX_ORDER_STENCIL }
#define GFX_FORMAT_D16_UNORM_S8_UINT \
	(GFXFormat){ {16,8,0,0}, GFX_UNORM | GFX_UINT, GFX_ORDER_DEPTH_STENCIL }
#define GFX_FORMAT_D24_UNORM_S8_UINT \
	(GFXFormat){ {24,8,0,0}, GFX_UNORM | GFX_UINT, GFX_ORDER_DEPTH_STENCIL }
#define GFX_FORMAT_D32_SFLOAT_S8_UINT \
	(GFXFormat){ {32,8,0,0}, GFX_SFLOAT | GFX_UINT, GFX_ORDER_DEPTH_STENCIL }

#define GFX_FORMAT_BC1_RGB_UNORM \
	(GFXFormat){ {1,0,0,0}, GFX_UNORM, GFX_ORDER_BCn }
#define GFX_FORMAT_BC1_RGB_SRGB \
	(GFXFormat){ {1,0,0,0}, GFX_SRGB, GFX_ORDER_BCn }
#define GFX_FORMAT_BC1_RGBA_UNORM \
	(GFXFormat){ {1,1,0,0}, GFX_UNORM, GFX_ORDER_BCn }
#define GFX_FORMAT_BC1_RGBA_SRGB \
	(GFXFormat){ {1,1,0,0}, GFX_SRGB, GFX_ORDER_BCn }
#define GFX_FORMAT_BC2_UNORM \
	(GFXFormat){ {2,1,0,0}, GFX_UNORM, GFX_ORDER_BCn }
#define GFX_FORMAT_BC2_SRGB \
	(GFXFormat){ {2,1,0,0}, GFX_SRGB, GFX_ORDER_BCn }
#define GFX_FORMAT_BC3_UNORM \
	(GFXFormat){ {3,1,0,0}, GFX_UNORM, GFX_ORDER_BCn }
#define GFX_FORMAT_BC3_SRGB \
	(GFXFormat){ {3,1,0,0}, GFX_SRGB, GFX_ORDER_BCn }
#define GFX_FORMAT_BC4_UNORM \
	(GFXFormat){ {4,0,0,0}, GFX_UNORM, GFX_ORDER_BCn }
#define GFX_FORMAT_BC4_SNORM \
	(GFXFormat){ {4,0,0,0}, GFX_SNORM, GFX_ORDER_BCn }
#define GFX_FORMAT_BC5_UNORM \
	(GFXFormat){ {5,0,0,0}, GFX_UNORM, GFX_ORDER_BCn }
#define GFX_FORMAT_BC5_SNORM \
	(GFXFormat){ {5,0,0,0}, GFX_SNORM, GFX_ORDER_BCn }
#define GFX_FORMAT_BC6_UFLOAT \
	(GFXFormat){ {6,0,0,0}, GFX_UFLOAT, GFX_ORDER_BCn }
#define GFX_FORMAT_BC6_SFLOAT \
	(GFXFormat){ {6,0,0,0}, GFX_SFLOAT, GFX_ORDER_BCn }
#define GFX_FORMAT_BC7_UNORM \
	(GFXFormat){ {7,1,0,0}, GFX_UNORM, GFX_ORDER_BCn }
#define GFX_FORMAT_BC7_SRGB \
	(GFXFormat){ {7,1,0,0}, GFX_SRGB, GFX_ORDER_BCn }

#define GFX_FORMAT_ETC2_R8G8B8_UNORM \
	(GFXFormat){ {8,8,8,0}, GFX_UNORM, GFX_ORDER_ETC2 }
#define GFX_FORMAT_ETC2_R8G8B8_SRGB \
	(GFXFormat){ {8,8,8,0}, GFX_SRGB, GFX_ORDER_ETC2 }
#define GFX_FORMAT_ETC2_R8G8B8A1_UNORM \
	(GFXFormat){ {8,8,8,1}, GFX_UNORM, GFX_ORDER_ETC2 }
#define GFX_FORMAT_ETC2_R8G8B8A1_SRGB \
	(GFXFormat){ {8,8,8,1}, GFX_SRGB, GFX_ORDER_ETC2 }
#define GFX_FORMAT_ETC2_R8G8B8A8_UNORM \
	(GFXFormat){ {8,8,8,8}, GFX_UNORM, GFX_ORDER_ETC2 }
#define GFX_FORMAT_ETC2_R8G8B8A8_SRGB \
	(GFXFormat){ {8,8,8,8}, GFX_SRGB, GFX_ORDER_ETC2 }

#define GFX_FORMAT_EAC_R11_UNORM \
	(GFXFormat){ {11,0,0,0}, GFX_UNORM, GFX_ORDER_EAC }
#define GFX_FORMAT_EAC_R11_SNORM \
	(GFXFormat){ {11,0,0,0}, GFX_SNORM, GFX_ORDER_EAC }
#define GFX_FORMAT_EAC_R11G11_UNORM \
	(GFXFormat){ {11,11,0,0}, GFX_UNORM, GFX_ORDER_EAC }
#define GFX_FORMAT_EAC_R11G11_SNORM \
	(GFXFormat){ {11,11,0,0}, GFX_SNORM, GFX_ORDER_EAC }

#define GFX_FORMAT_ASTC_4x4_UNORM \
	(GFXFormat){ {4,4,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_4x4_SRGB \
	(GFXFormat){ {4,4,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_5x4_UNORM \
	(GFXFormat){ {5,4,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_5x4_SRGB \
	(GFXFormat){ {5,4,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_5x5_UNORM \
	(GFXFormat){ {5,5,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_5x5_SRGB \
	(GFXFormat){ {5,5,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_6x5_UNORM \
	(GFXFormat){ {6,5,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_6x5_SRGB \
	(GFXFormat){ {6,5,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_6x6_UNORM \
	(GFXFormat){ {6,6,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_6x6_SRGB \
	(GFXFormat){ {6,6,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_8x5_UNORM \
	(GFXFormat){ {8,5,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_8x5_SRGB \
	(GFXFormat){ {8,5,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_8x6_UNORM \
	(GFXFormat){ {8,6,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_8x6_SRGB \
	(GFXFormat){ {8,6,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_8x8_UNORM \
	(GFXFormat){ {8,8,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_8x8_SRGB \
	(GFXFormat){ {8,8,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_10x5_UNORM \
	(GFXFormat){ {10,5,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_10x5_SRGB \
	(GFXFormat){ {10,5,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_10x6_UNORM \
	(GFXFormat){ {10,6,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_10x6_SRGB \
	(GFXFormat){ {10,6,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_10x8_UNORM \
	(GFXFormat){ {10,8,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_10x8_SRGB \
	(GFXFormat){ {10,8,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_10x10_UNORM \
	(GFXFormat){ {10,10,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_10x10_SRGB \
	(GFXFormat){ {10,10,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_12x10_UNORM \
	(GFXFormat){ {12,10,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_12x10_SRGB \
	(GFXFormat){ {12,10,0,0}, GFX_SRGB, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_12x12_UNORM \
	(GFXFormat){ {12,12,0,0}, GFX_UNORM, GFX_ORDER_ASTC }
#define GFX_FORMAT_ASTC_12x12_SRGB \
	(GFXFormat){ {12,12,0,0}, GFX_SRGB, GFX_ORDER_ASTC }


#endif
