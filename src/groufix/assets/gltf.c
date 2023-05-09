/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/assets/gltf.h"
#include "groufix/containers/vec.h"
#include "groufix/core/log.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"


#define _GFX_GLTF_ERROR_STRING(result) \
	((result) == cgltf_result_success ? \
		"success" : \
	(result) == cgltf_result_data_too_short ? \
		"data too short" : \
	(result) == cgltf_result_unknown_format ? \
		"unknown format" : \
	(result) == cgltf_result_invalid_json ? \
		"invalid JSON" : \
	(result) == cgltf_result_invalid_gltf ? \
		"invalid glTF" : \
	(result) == cgltf_result_out_of_memory ? \
		"out of memory" : \
	(result) == cgltf_result_legacy_gltf ? \
		"legacy glTF" : \
		"unknown error")

#define _GFX_GLTF_NODE_FLAGS(cnode) \
	((cnode->has_translation ? \
		GFX_GLTF_NODE_TRANSLATION : 0) | \
	(cnode->has_rotation ? \
		GFX_GLTF_NODE_ROTATION : 0) | \
	(cnode->has_scale ? \
		GFX_GLTF_NODE_SCALE : 0))

#define _GFX_GLTF_MATERIAL_FLAGS(cmat) \
	((cmat->has_pbr_metallic_roughness ? \
		GFX_GLTF_MATERIAL_PBR_METALLIC_ROUGHNESS : 0) | \
	(cmat->has_pbr_specular_glossiness ? \
		GFX_GLTF_MATERIAL_PBR_SPECULAR_GLOSSINESS : 0) | \
	(cmat->has_ior ? \
		GFX_GLTF_MATERIAL_IOR : 0) | \
	(cmat->has_emissive_strength ? \
		GFX_GLTF_MATERIAL_EMISSIVE_STRENGTH : 0) | \
	(cmat->has_clearcoat ? \
		GFX_GLTF_MATERIAL_CLEARCOAT : 0) | \
	(cmat->has_iridescence ? \
		GFX_GLTF_MATERIAL_IRIDESCENCE : 0) | \
	(cmat->has_sheen ? \
		GFX_GLTF_MATERIAL_SHEEN : 0) | \
	(cmat->has_specular ? \
		GFX_GLTF_MATERIAL_SPECULAR : 0) | \
	(cmat->has_transmission ? \
		GFX_GLTF_MATERIAL_TRANSMISSION : 0) | \
	(cmat->has_volume ? \
		GFX_GLTF_MATERIAL_VOLUME : 0) | \
	(cmat->unlit ? \
		GFX_GLTF_MATERIAL_UNLIT : 0) | \
	(cmat->double_sided ? \
		GFX_GLTF_MATERIAL_DOUBLE_SIDED : 0))

#define _GFX_GLTF_ALPHA_MODE(mode) \
	((mode) == cgltf_alpha_mode_opaque ? \
		GFX_GLTF_ALPHA_OPAQUE : \
	(mode) == cgltf_alpha_mode_mask ? \
		GFX_GLTF_ALPHA_MASK : \
	(mode) == cgltf_alpha_mode_blend ? \
		GFX_GLTF_ALPHA_BLEND : \
		GFX_GLTF_ALPHA_OPAQUE)

#define _GFX_GLTF_TOPOLOGY(topo) \
	((topo) == cgltf_primitive_type_points ? \
		GFX_TOPO_POINT_LIST : \
	(topo) == cgltf_primitive_type_lines ? \
		GFX_TOPO_LINE_LIST : \
	(topo) == cgltf_primitive_type_line_loop ? \
		GFX_TOPO_LINE_STRIP : \
	(topo) == cgltf_primitive_type_line_strip ? \
		GFX_TOPO_LINE_STRIP : \
	(topo) == cgltf_primitive_type_triangles ? \
		GFX_TOPO_TRIANGLE_LIST : \
	(topo) == cgltf_primitive_type_triangle_strip ? \
		GFX_TOPO_TRIANGLE_STRIP : \
	(topo) == cgltf_primitive_type_triangle_fan ? \
		GFX_TOPO_TRIANGLE_FAN : \
		GFX_TOPO_TRIANGLE_LIST)

#define _GFX_GLTF_INDEX_SIZE(type) \
	((type) == cgltf_component_type_r_16u ? sizeof(uint16_t) : \
	(type) == cgltf_component_type_r_32u ? sizeof(uint32_t) : 0)

#define _GFX_GLTF_FILTER(magFilter) \
	((magFilter) == 0x2600 ? GFX_FILTER_NEAREST : \
	(magFilter) == 0x2601 ? GFX_FILTER_LINEAR : \
	(magFilter) == 0x2700 ? GFX_FILTER_NEAREST : \
	(magFilter) == 0x2701 ? GFX_FILTER_LINEAR : \
	(magFilter) == 0x2702 ? GFX_FILTER_NEAREST : \
	(magFilter) == 0x2703 ? GFX_FILTER_LINEAR : GFX_FILTER_NEAREST)

#define _GFX_GLTF_MIP_FILTER(minFilter) \
	((minFilter) == 0x2600 ? GFX_FILTER_NEAREST : \
	(minFilter) == 0x2601 ? GFX_FILTER_NEAREST : \
	(minFilter) == 0x2700 ? GFX_FILTER_NEAREST : \
	(minFilter) == 0x2701 ? GFX_FILTER_NEAREST : \
	(minFilter) == 0x2702 ? GFX_FILTER_LINEAR : \
	(minFilter) == 0x2703 ? GFX_FILTER_LINEAR : GFX_FILTER_NEAREST)

#define _GFX_GLTF_WRAPPING(wrap) \
	((wrap) == 0x2901 ? GFX_WRAP_REPEAT : \
	(wrap) == 0x8370 ? GFX_WRAP_REPEAT_MIRROR : \
	(wrap) == 0x812f ? GFX_WRAP_CLAMP_TO_EDGE : \
	(wrap) == 0x8743 ? GFX_WRAP_CLAMP_TO_EDGE_MIRROR : \
	(wrap) == 0x812d ? GFX_WRAP_CLAMP_TO_BORDER : GFX_WRAP_REPEAT)


// Helpers to transform glTF data array pointers to groufix.
#define _GFX_FROM_GLTF(vec, array, pElem) \
	((pElem) != NULL ? gfx_vec_at(&(vec), (size_t)((pElem) - (array))) : NULL)

#define _GFX_FROM_GLTF_ACCESSOR(pAccessor) \
	((pAccessor) != NULL && (pAccessor)->buffer_view != NULL ? \
		_GFX_FROM_GLTF( \
			buffers, data->buffers, (pAccessor)->buffer_view->buffer) : NULL)

#define _GFX_FROM_GLTF_TEXVIEW(view) \
	(GFXGltfTexture){ \
		.image = (view).texture != NULL ? \
			*(GFXImage**)_GFX_FROM_GLTF( \
				images, data->images, (view).texture->image) : NULL, \
		.sampler = (view).texture != NULL ? \
			_GFX_FROM_GLTF( \
				samplers, data->samplers, (view).texture->sampler) : NULL \
	}

// Decode a hexadecimal digit.
#define _GFX_UNHEX(digit) \
	(unsigned char)( \
		(unsigned)(digit - '0') < 10 ? (digit - '0') : \
		(unsigned)(digit - 'A') < 6 ? (digit - 'A') + 10 : \
		(unsigned)(digit - 'a') < 6 ? (digit - 'a') + 10 : \
		UCHAR_MAX)


/****************************
 * Compares (case insensitive) two NULL-terminated strings.
 * One of the strings may terminate with '_', its remains will be ignored.
 */
static bool _gfx_gltf_cmp_attributes(const char* l, const char* r)
{
	if (l == NULL || r == NULL)
		return 0;

	while (*l != '\0' && *r != '\0')
		if (tolower(*(l++)) != tolower(*(r++)))
			return 0;

	return *l == *r || *l == '_' || *r == '_';
}

/****************************
 * Constructs a vertex attribute format from the glTF accessor type,
 * component type and normalized flag.
 */
static GFXFormat _gfx_gltf_attribute_fmt(cgltf_component_type cType,
                                         cgltf_type type,
                                         cgltf_bool normalized)
{
	// Compute #components and their properties.
	const size_t comps =
		type == cgltf_type_scalar ? 1 :
		type == cgltf_type_vec2 ? 2 :
		type == cgltf_type_vec3 ? 3 :
		type == cgltf_type_vec4 ? 4 : 0;

	const unsigned char depth =
		cType == cgltf_component_type_r_8 ? 8 :
		cType == cgltf_component_type_r_8u ? 8 :
		cType == cgltf_component_type_r_16 ? 16 :
		cType == cgltf_component_type_r_16u ? 16 :
		cType == cgltf_component_type_r_32u ? 32 :
		cType == cgltf_component_type_r_32f ? 32 : 0;

	const GFXFormatType fType =
		// Signed integer.
		(cType == cgltf_component_type_r_8 ||
		cType == cgltf_component_type_r_16) ?
			(normalized ? GFX_SNORM : GFX_SSCALED) :
		// Unsigned integer.
		(cType == cgltf_component_type_r_8u ||
		cType == cgltf_component_type_r_16u ||
		cType == cgltf_component_type_r_32u) ?
			(normalized ? GFX_UNORM : GFX_USCALED) :
		// Floating point.
		cType == cgltf_component_type_r_32f ?
			GFX_SFLOAT : 0;

	const GFXOrder order =
		type == cgltf_type_scalar ? GFX_ORDER_R :
		type == cgltf_type_vec2 ? GFX_ORDER_RG :
		type == cgltf_type_vec3 ? GFX_ORDER_RGB :
		type == cgltf_type_vec4 ? GFX_ORDER_RGBA : 0;

	return (GFXFormat){
		{
			comps > 0 ? depth : 0,
			comps > 1 ? depth : 0,
			comps > 2 ? depth : 0,
			comps > 3 ? depth : 0
		},
		fType,
		order
	};
}

/****************************
 * Creates a 4x4 column-major scale-rotate-translate matrix:
 * m = mat(t) * mat(q) * mat(s)
 *  where
 *   t = [x,y,z]   (translation vector)
 *   q = [x,y,z,w] (rotation quaternion)
 *   s = [x,y,z]   (scale vector)
 */
static void _gfx_gltf_to_mat(float* m,
                             const float* t, const float* q, const float* s)
{
	// Quaternion -> matrix.
	const float sq0 = q[0] * q[0];
	const float sq1 = q[1] * q[1];
	const float sq2 = q[2] * q[2];
	const float sq3 = q[3] * q[3];
	const float l = 2.0f / sqrtf(sq0 + sq1 + sq2 + sq3);

	// Scale it as we go.
	m[0] = s[0] * (1.0f - l * (sq1 - sq2));
	m[1] = s[0] * l * (q[0] * q[1] + q[2] * q[3]);
	m[2] = s[0] * l * (q[0] * q[2] - q[1] * q[3]);
	m[3] = 0.0f;

	m[4] = s[1] * l * (q[0] * q[1] - q[2] * q[3]);
	m[5] = s[1] * (1.0f - l * (sq0 - sq2));
	m[6] = s[1] * l * (q[1] * q[2] + q[0] * q[3]);
	m[7] = 0.0f;

	m[8] = s[2] * l * (q[0] * q[2] + q[1] * q[3]);
	m[9] = s[2] * l * (q[1] * q[2] - q[0] * q[3]);
	m[10] = s[2] * (1.0f - l * (sq0 - sq1));
	m[11] = 0.0f;

	// Stick in translation.
	m[12] = t[0];
	m[13] = t[1];
	m[14] = t[2];
	m[15] = 1.0f;
}

/****************************
 * Decodes an encoded URI into a newly allocated string.
 * @return Must call free() on success!
 */
static char* _gfx_gltf_decode_uri(const char* uri)
{
	const size_t len = strlen(uri);

	// Make a copy URI.
	char* buf = malloc(len + 1);
	if (buf == NULL) return NULL;

	strcpy(buf, uri);

	// Decode all %-encodings inline.
	char* w = buf;
	char* i = buf;

	while (*i != '\0')
	{
		if (*i == '%')
		{
			unsigned char c0 = _GFX_UNHEX(i[1]);
			if (c0 < 16)
			{
				unsigned char c1 = _GFX_UNHEX(i[2]);
				if (c1 < 16)
				{
					*(w++) = (char)(c0 * 16 + c1);
					i += 3;
					continue;
				}
			}
		}

		*(w++) = *(i++);
	}

	*w = '\0';
	return buf;
}

/****************************
 * Gets the base64 part from a data URI.
 * @return NULL if not a base64 data uri, pointer to the base64 data otherwise.
 */
static const char* _gfx_gltf_get_base64(const char* uri)
{
	const char* comma =
		strchr(uri, ',');

	const bool isbase64 =
		comma != NULL &&
		comma - uri >= 7 &&
		strncmp(comma - 7, ";base64", 7) == 0;

	return isbase64 ? comma + 1 : NULL;
}

/****************************
 * Decodes a base64 string into a newly allocated binary buffer.
 * @param size Size of the output buffer (_NOT_ of src) in bytes, fails if 0.
 * @return Must call free() on success!
 */
static void* _gfx_gltf_decode_base64(size_t size, const char* src)
{
	if (size == 0) return NULL; // Empty is explicit error.

	unsigned char* bin = malloc(size);
	if (bin == NULL) return NULL;

	unsigned int buff = 0;
	unsigned int bits = 0;

	// Loop over all output bytes.
	for (size_t i = 0; i < size; ++i)
	{
		// Loop over base64 digits until we have at least 8 bits.
		while (bits < 8)
		{
			char digit = *(src++);
			unsigned char index = (unsigned char)(
				(unsigned)(digit - 'A') < 26 ? (digit - 'A') :
				(unsigned)(digit - 'a') < 26 ? (digit - 'a') + 26 :
				(unsigned)(digit - '0') < 10 ? (digit - '0') + 52 :
				(digit == '+') ? 62 :
				(digit == '/') ? 63 :
				UCHAR_MAX);

			if (index > 63)
			{
				free(bin);
				return NULL;
			}

			buff = (buff << 6) | index;
			bits += 6;
		}

		// Output the oldest 8 bits.
		bits -= 8;
		bin[i] = (unsigned char)(buff >> bits);
	}

	return bin;
}

/****************************
 * Resolves and reads a buffer URI.
 * @param inc  Includer to use, may be NULL.
 * @param uri  Data URI to resolve, cannot be NULL, must be NULL-terminated.
 * @param size Output length of the returned buffer.
 * @return NULL on failure.
 */
static void* _gfx_gltf_include_buffer(const GFXIncluder* inc, const char* uri,
                                      size_t* size)
{
	assert(uri != NULL);
	assert(size != NULL);

	*size = 0;

	// Cannot do anything without an includer.
	if (inc == NULL)
	{
		gfx_log_error("Cannot load buffer URIs without an includer.");
		return NULL;
	}

	// Resolve the URI.
	char* dec = _gfx_gltf_decode_uri(uri);
	if (dec == NULL)
	{
		gfx_log_error("Could not decode buffer URI: %s.", uri);
		return NULL;
	}

	const GFXReader* src = gfx_io_resolve(inc, dec);
	free(dec); // Immediately free.

	if (src == NULL)
	{
		gfx_log_error("Could not resolve buffer URI: %s.", uri);
		return NULL;
	}

	// Allocate binary buffer.
	long long len = gfx_io_len(src);
	if (len <= 0) goto clean;

	void* bin = malloc((size_t)len);
	if (bin == NULL) goto clean;

	// Read source.
	len = gfx_io_read(src, bin, (size_t)len);
	if (len <= 0)
	{
		free(bin);
		goto clean;
	}

	// Release the stream & output.
	gfx_io_release(inc, src);

	*size = (size_t)len;
	return bin;


	// Cleanup on failure.
clean:
	gfx_log_error("Could not read data from stream to load URI: %s.", uri);

	gfx_io_release(inc, src);
	return NULL;
}

/****************************
 * Decodes a base64 encoded image URI.
 * @param src Source to decode, cannot be NULL, must be NULL-terminated.
 * @return NULL on failure.
 */
static GFXImage* _gfx_gltf_decode_image(const char* src,
                                        GFXHeap* heap, GFXDependency* dep,
                                        GFXImageFlags flags, GFXImageUsage usage)
{
	assert(src != NULL);
	assert(heap != NULL);
	assert(dep != NULL);

	// Decode base64.
	const size_t len = strlen(src);
	const size_t size = len / 4 * 3
		// Remove to account for padding.
		- (src[len-2] == '=' ? 2 : src[len-1] == '=' ? 1 : 0)
		// Add to account for missing padding.
		+ ((len & 3) == 3 ? 2 : (len & 3) == 2 ? 1 : 0);

	void* bin = _gfx_gltf_decode_base64(size, src);
	if (bin == NULL)
	{
		gfx_log_error("Failed to decode base64 image data URI.");
		return NULL;
	}

	// Load image.
	GFXBinReader reader;
	GFXImage* image = gfx_load_image(
		heap, dep, flags, usage, gfx_bin_reader(&reader, size, bin));

	if (image == NULL)
		gfx_log_error("Failed to load image data URI.");

	// Free the data & output.
	free(bin);

	return image;
}

/****************************
 * Resolves and reads an image URI.
 * @param inc Includer to use, may be NULL.
 * @param uri Data URI to resolve, cannot be NULL, must be NULL-terminated.
 * @return NULL on failure.
 */
static GFXImage* _gfx_gltf_include_image(const GFXIncluder* inc, const char* uri,
                                         GFXHeap* heap, GFXDependency* dep,
                                         GFXImageFlags flags, GFXImageUsage usage)
{
	assert(uri != NULL);
	assert(heap != NULL);
	assert(dep != NULL);

	// Cannot do anything without an includer.
	if (inc == NULL)
	{
		gfx_log_error("Cannot load image URIs without an includer.");
		return NULL;
	}

	// Resolve the URI.
	char* dec = _gfx_gltf_decode_uri(uri);
	if (dec == NULL)
	{
		gfx_log_error("Could not decode image URI: %s.", uri);
		return NULL;
	}

	const GFXReader* src = gfx_io_resolve(inc, dec);
	free(dec); // Immediately free.

	if (src == NULL)
	{
		gfx_log_error("Could not resolve image URI: %s.", uri);
		return NULL;
	}

	// Simply load the image.
	GFXImage* image = gfx_load_image(heap, dep, flags, usage, src);
	if (image == NULL)
		gfx_log_error("Failed to load image URI: %s.", uri);

	// Release the stream & output.
	gfx_io_release(inc, src);

	return image;
}

/****************************
 * TODO: This will allocate the ENTIRE buffer, even if e.g. images are stored in it!
 * Allocates a buffer from the data already in a GFXGltfBuffer object.
 * @param buffer May be NULL, on which it will fail (same if size is 0).
 * @return Non-zero on success.
 */
static bool _gfx_gltf_buffer_alloc(GFXHeap* heap, GFXDependency* dep,
                                   GFXGltfBuffer* buffer)
{
	assert(heap != NULL);
	assert(dep != NULL);

	// Nothing to allocate.
	if (buffer == NULL || buffer->size == 0 || buffer->bin == NULL)
		return 0;

	// Already done.
	if (buffer->buffer != NULL)
		return 1;

	// Allocate.
	buffer->buffer = gfx_alloc_buffer(heap,
		GFX_MEMORY_WRITE,
		GFX_BUFFER_VERTEX | GFX_BUFFER_INDEX,
		buffer->size);

	if (buffer->buffer == NULL)
		return 0;

	// Write data.
	const GFXRegion region = {
		.offset = 0,
		.size = buffer->size
	};

	const GFXInject inject =
		gfx_dep_sig(dep,
			GFX_ACCESS_VERTEX_READ | GFX_ACCESS_INDEX_READ, GFX_STAGE_ANY);

	if (!gfx_write(buffer->bin, gfx_ref_buffer(buffer->buffer),
		GFX_TRANSFER_ASYNC,
		1, 1, &region, &region, &inject))
	{
		gfx_free_buffer(buffer->buffer);
		buffer->buffer = NULL;

		return 0;
	}

	return 1;
}

/****************************
 * Reorders named glTF attributes based on given options.
 * @param cprim       glTF primitive's attributes to reorder, cannot be NULL.
 * @param attribOrder Output array of ordered attribute indices, cannot be NULL.
 * @return Actual number of attributes to consume.
 *
 * attribOrder's size must be cprim->attributes_count.
 */
static size_t _gfx_gltf_order_attributes(const cgltf_primitive* cprim,
                                         const GFXGltfOptions* options,
                                         size_t* attribOrder)
{
	assert(cprim != NULL);
	assert(cprim->attributes_count > 0);
	assert(attribOrder != NULL);

	const size_t numAttributes =
		options != NULL && options->maxAttributes > 0 ?
			GFX_MIN(options->maxAttributes, cprim->attributes_count) :
			cprim->attributes_count;

	if (options == NULL || options->orderSize == 0)
		// Keep order if no options are given.
		for (size_t a = 0; a < numAttributes; ++a)
			attribOrder[a] = a;
	else
	{
		// Keep track of used glTF attributes.
		bool attribUsed[cprim->attributes_count];
		for (size_t ca = 0; ca < cprim->attributes_count; ++ca)
			attribUsed[ca] = 0;

		// Go over all given attribute order names (in order).
		size_t a = 0;
		for (size_t o = 0;
			o < options->orderSize && a < numAttributes; ++o)
		{
			// See if they match any glTF attributes.
			for (size_t ca = 0; ca < cprim->attributes_count; ++ca)
				if (!attribUsed[ca] && _gfx_gltf_cmp_attributes(
					options->attributeOrder[o],
					cprim->attributes[ca].name))
				{
					attribOrder[a++] = ca;
					attribUsed[ca] = 1;
					break;
				}
		}

		// Fill in the rest with remaining unused attributes.
		for (size_t ca = 0;
			ca < cprim->attributes_count && a < numAttributes; ++ca)
		{
			if (!attribUsed[ca])
				attribOrder[a++] = ca;
		}
	}

	return numAttributes;
}

/****************************/
GFX_API bool gfx_load_gltf(GFXHeap* heap, GFXDependency* dep,
                           const GFXGltfOptions* options,
                           GFXImageFlags flags, GFXImageUsage usage,
                           const GFXReader* src,
                           const GFXIncluder* inc,
                           GFXGltfResult* result)
{
	assert(heap != NULL);
	assert(dep != NULL);
	assert(options == NULL || options->orderSize == 0 || options->attributeOrder != NULL);
	assert(src != NULL);
	assert(result != NULL);

	// Read source.
	const void* source;
	long long len = gfx_io_raw_init(&source, src);

	if (len <= 0)
	{
		gfx_log_error("Could not read glTF source from stream.");
		return NULL;
	}

	// Parse the glTF source.
	cgltf_options opts = {0};
	cgltf_data* data = NULL;

	cgltf_result res = cgltf_parse(&opts, source, (size_t)len, &data);
	// Postpone freeing `source`, cgltf returns pointers to it.

	// Some extra validation.
	if (res == cgltf_result_success) res = cgltf_validate(data);
	else data = NULL; // So we do not free garbage.

	// Fail on error.
	if (res != cgltf_result_success)
	{
		gfx_log_error(
			"Failed to load glTF, %s.",
			_GFX_GLTF_ERROR_STRING(res));

		cgltf_free(data);
		gfx_io_raw_clear(&source, src);
		return 0;
	}

	// Setup some output vectors.
	// We are going to fill them with groufix equivalents of the glTF.
	// From this point onwards we need to clean on failure.
	size_t numNodePtrs = 0;
	GFXGltfNode** nodePtrs = NULL; // Scene/node children-pointers

	GFXVec buffers;
	GFXVec images;
	GFXVec samplers;
	GFXVec materials;
	GFXVec primitives;
	GFXVec meshes;
	GFXVec nodes;
	GFXVec scenes;
	gfx_vec_init(&buffers, sizeof(GFXGltfBuffer));
	gfx_vec_init(&images, sizeof(GFXImage*));
	gfx_vec_init(&samplers, sizeof(GFXGltfSampler));
	gfx_vec_init(&materials, sizeof(GFXGltfMaterial));
	gfx_vec_init(&primitives, sizeof(GFXGltfPrimitive));
	gfx_vec_init(&meshes, sizeof(GFXGltfMesh));
	gfx_vec_init(&nodes, sizeof(GFXGltfNode));
	gfx_vec_init(&scenes, sizeof(GFXGltfScene));
	gfx_vec_reserve(&buffers, data->buffers_count);
	gfx_vec_reserve(&images, data->images_count);
	gfx_vec_reserve(&samplers, data->samplers_count);
	gfx_vec_reserve(&materials, data->materials_count);
	gfx_vec_reserve(&meshes, data->meshes_count);
	gfx_vec_reserve(&nodes, data->nodes_count);
	gfx_vec_reserve(&scenes, data->scenes_count);

	// Create all buffers.
	for (size_t b = 0; b < data->buffers_count; ++b)
	{
		GFXGltfBuffer buffer = {
			.size = data->buffers[b].size,
			.bin = NULL,
			.buffer = NULL
		};

		const char* uri = data->buffers[b].uri;

		// Check if data URI.
		if (uri != NULL && strncmp(uri, "data:", 5) == 0)
		{
			// Decode as base64.
			const char* base64 = _gfx_gltf_get_base64(uri);
			if (base64 == NULL)
			{
				gfx_log_error("Buffer data URIs can only be base64.");
				goto clean;
			}

			buffer.bin = _gfx_gltf_decode_base64(buffer.size, base64);
			if (buffer.bin == NULL)
			{
				gfx_log_error("Failed to decode base64 buffer data URI.");
				goto clean;
			}
		}

		// Check if actual URI.
		else if (uri != NULL)
		{
			buffer.bin = _gfx_gltf_include_buffer(inc, uri, &buffer.size);
			if (buffer.bin == NULL) goto clean;
		}

		// Check if it references the GLB-stored BIN chunk.
		// Only the first buffer can reference it, according to the specs!
		else if (b == 0 && data->bin_size >= buffer.size && buffer.size > 0)
		{
			buffer.bin = malloc(buffer.size);
			if (buffer.bin == NULL) goto clean;

			memcpy(buffer.bin, data->bin, buffer.size);
		}

		// Insert buffer.
		if (!gfx_vec_push(&buffers, 1, &buffer))
		{
			free(buffer.bin);
			goto clean;
		}
	}

	// Create all images.
	for (size_t i = 0; i < data->images_count; ++i)
	{
		GFXImage* image = NULL;

		const char* uri = data->images[i].uri;
		const cgltf_buffer_view* cview = data->images[i].buffer_view;

		// Check if data URI.
		if (uri != NULL && strncmp(uri, "data:", 5) == 0)
		{
			// Decode as base64.
			const char* base64 = _gfx_gltf_get_base64(uri);
			if (base64 == NULL)
			{
				gfx_log_error("Image data URIs can only be base64.");
				goto clean;
			}

			image = _gfx_gltf_decode_image(base64, heap, dep, flags, usage);
			if (image == NULL) goto clean;
		}

		// Check if actual URI.
		else if (uri != NULL)
		{
			image = _gfx_gltf_include_image(inc, uri, heap, dep, flags, usage);
			if (image == NULL) goto clean;
		}

		// Check if a buffer view.
		else if (cview != NULL)
		{
			GFXGltfBuffer* buffer =
				_GFX_FROM_GLTF(buffers, data->buffers, cview->buffer);

			// Load the image.
			if (buffer == NULL || buffer->bin == NULL)
			{
				gfx_log_error("Image buffer view has no data.");
				goto clean;
			}

			if (cview->offset + cview->size > buffer->size)
			{
				gfx_log_error("Image buffer view is out of range.");
				goto clean;
			}

			GFXBinReader reader;
			image = gfx_load_image(heap, dep, flags, usage,
				gfx_bin_reader(
					&reader, cview->size,
					((uint8_t*)buffer->bin) + cview->offset));

			if (image == NULL)
			{
				gfx_log_error("Failed to load image data from buffer.");
				goto clean;
			}
		}

		// Insert image.
		if (!gfx_vec_push(&images, 1, &image))
		{
			gfx_free_image(image);
			goto clean;
		}
	}

	// Create all samplers.
	for (size_t s = 0; s < data->samplers_count; ++s)
	{
		// Insert sampler.
		GFXGltfSampler sampler = {
			.minFilter = _GFX_GLTF_FILTER(data->samplers[s].min_filter),
			.magFilter = _GFX_GLTF_FILTER(data->samplers[s].mag_filter),
			.mipFilter = _GFX_GLTF_MIP_FILTER(data->samplers[s].min_filter),

			.wrapU = _GFX_GLTF_WRAPPING(data->samplers[s].wrap_s),
			.wrapV = _GFX_GLTF_WRAPPING(data->samplers[s].wrap_t),
		};

		if (!gfx_vec_push(&samplers, 1, &sampler))
			goto clean;
	}

	// Create all materials.
	for (size_t m = 0; m < data->materials_count; ++m)
	{
		const cgltf_material* cmat = &data->materials[m];

		// Insert material.
		GFXGltfMaterial material = {
			.flags = _GFX_GLTF_MATERIAL_FLAGS(cmat),

			.pbr = {
				// Metallic roughness.
				.baseColor = _GFX_FROM_GLTF_TEXVIEW(
					cmat->pbr_metallic_roughness.base_color_texture),
				.metallicRoughness = _GFX_FROM_GLTF_TEXVIEW(
					cmat->pbr_metallic_roughness.metallic_roughness_texture),

				.baseColorFactors = {
					cmat->pbr_metallic_roughness.base_color_factor[0],
					cmat->pbr_metallic_roughness.base_color_factor[1],
					cmat->pbr_metallic_roughness.base_color_factor[2],
					cmat->pbr_metallic_roughness.base_color_factor[3]
				},

				.metallicFactor =
					cmat->pbr_metallic_roughness.metallic_factor,
				.roughnessFactor =
					cmat->pbr_metallic_roughness.roughness_factor,
				.ior =
					cmat->ior.ior,

				// Specular glossiness.
				.diffuse = _GFX_FROM_GLTF_TEXVIEW(
					cmat->pbr_specular_glossiness.diffuse_texture),
				.specularGlossiness = _GFX_FROM_GLTF_TEXVIEW(
					cmat->pbr_specular_glossiness.specular_glossiness_texture),

				.diffuseFactors = {
					cmat->pbr_specular_glossiness.diffuse_factor[0],
					cmat->pbr_specular_glossiness.diffuse_factor[1],
					cmat->pbr_specular_glossiness.diffuse_factor[2],
					cmat->pbr_specular_glossiness.diffuse_factor[3]
				},

				.specularFactors = {
					cmat->pbr_specular_glossiness.specular_factor[0],
					cmat->pbr_specular_glossiness.specular_factor[1],
					cmat->pbr_specular_glossiness.specular_factor[2]
				},

				.glossinessFactor =
					cmat->pbr_specular_glossiness.glossiness_factor
			},

			// Standard.
			.normal = _GFX_FROM_GLTF_TEXVIEW(cmat->normal_texture),
			.occlusion = _GFX_FROM_GLTF_TEXVIEW(cmat->occlusion_texture),
			.emissive = _GFX_FROM_GLTF_TEXVIEW(cmat->emissive_texture),

			.alphaMode = _GFX_GLTF_ALPHA_MODE(cmat->alpha_mode),

			.normalScale = cmat->normal_texture.scale,
			.occlusionStrength = cmat->occlusion_texture.scale,
			.emissiveStrength = cmat->emissive_strength.emissive_strength,
			.alphaCutoff = cmat->alpha_cutoff,

			.emissiveFactors = {
				cmat->emissive_factor[0],
				cmat->emissive_factor[1],
				cmat->emissive_factor[2]
			},

			// Clearcoat.
			.clearcoat = _GFX_FROM_GLTF_TEXVIEW(
				cmat->clearcoat.clearcoat_texture),
			.clearcoatRoughness = _GFX_FROM_GLTF_TEXVIEW(
				cmat->clearcoat.clearcoat_roughness_texture),
			.clearcoatNormal = _GFX_FROM_GLTF_TEXVIEW(
				cmat->clearcoat.clearcoat_normal_texture),

			.clearcoatFactor =
				cmat->clearcoat.clearcoat_factor,
			.clearcoatRoughnessFactor =
				cmat->clearcoat.clearcoat_roughness_factor,

			// Iridescence.
			.iridescence = _GFX_FROM_GLTF_TEXVIEW(
				cmat->iridescence.iridescence_texture),
			.iridescenceThickness = _GFX_FROM_GLTF_TEXVIEW(
				cmat->iridescence.iridescence_thickness_texture),

			.iridescenceFactor =
				cmat->iridescence.iridescence_factor,
			.iridescenceIor =
				cmat->iridescence.iridescence_ior,
			.iridescenceThicknessMin =
				cmat->iridescence.iridescence_thickness_min,
			.iridescenceThicknessMax =
				cmat->iridescence.iridescence_thickness_max,

			// Sheen.
			.sheenColor = _GFX_FROM_GLTF_TEXVIEW(
				cmat->sheen.sheen_color_texture),
			.sheenRoughness = _GFX_FROM_GLTF_TEXVIEW(
				cmat->sheen.sheen_roughness_texture),

			.sheenColorFactors = {
				cmat->sheen.sheen_color_factor[0],
				cmat->sheen.sheen_color_factor[1],
				cmat->sheen.sheen_color_factor[2]
			},

			.sheenRoughnessFactor =
				cmat->sheen.sheen_roughness_factor,

			// Specular.
			.specular = _GFX_FROM_GLTF_TEXVIEW(
				cmat->specular.specular_texture),
			.specularColor = _GFX_FROM_GLTF_TEXVIEW(
				cmat->specular.specular_color_texture),

			.specularFactor =
				cmat->specular.specular_factor,

			.specularColorFactors = {
				cmat->specular.specular_color_factor[0],
				cmat->specular.specular_color_factor[1],
				cmat->specular.specular_color_factor[2]
			},

			// Transmission.
			.transmission = _GFX_FROM_GLTF_TEXVIEW(
				cmat->transmission.transmission_texture),

			.transmissionFactor =
				cmat->transmission.transmission_factor,

			// Volume.
			.thickness = _GFX_FROM_GLTF_TEXVIEW(
				cmat->volume.thickness_texture),

			.thicknessFactor = cmat->volume.thickness_factor,
			.attenuationDistance = cmat->volume.attenuation_distance,

			.attenuationColors = {
				cmat->volume.attenuation_color[0],
				cmat->volume.attenuation_color[1],
				cmat->volume.attenuation_color[2]
			}
		};

		if (!gfx_vec_push(&materials, 1, &material))
			goto clean;
	}

	// Create all primitives.
	for (size_t m = 0; m < data->meshes_count; ++m)
	{
		for (size_t p = 0; p < data->meshes[m].primitives_count; ++p)
		{
			// Gather all primitive data.
			const cgltf_primitive* cprim = &data->meshes[m].primitives[p];

			const size_t numIndices =
				cprim->indices != NULL ? cprim->indices->count : 0;
			const char indexSize =
				cprim->indices != NULL ?
				_GFX_GLTF_INDEX_SIZE(cprim->indices->component_type) : 0;
			GFXGltfBuffer* indexBuffer =
				_GFX_FROM_GLTF_ACCESSOR(cprim->indices);

			if (cprim->attributes_count == 0)
			{
				gfx_log_error("Primitives must have attributes.");
				goto clean;
			}

			if (numIndices > 0 && indexSize == 0)
			{
				gfx_log_error("Index accessors must be sizeof(uint16_t|uint32_t).");
				goto clean;
			}

			if (numIndices > 0 && !_gfx_gltf_buffer_alloc(heap, dep, indexBuffer))
			{
				gfx_log_error("Failed to allocate index buffer.");
				goto clean;
			}

			// Here we consider that attributes are named in glTF,
			// and they may not always appear in the same order in a file.
			// Calculate the actual order to consume the attributes in.
			size_t attribOrder[cprim->attributes_count];
			size_t numAttributes =
				_gfx_gltf_order_attributes(cprim, options, attribOrder);

			size_t numVertices = SIZE_MAX;
			GFXAttribute attributes[numAttributes];

			// Fill attribute data.
			for (size_t a = 0; a < numAttributes; ++a)
			{
				const cgltf_attribute* cattr =
					&cprim->attributes[attribOrder[a]];

				numVertices = GFX_MIN(
					numVertices, cattr->data->count);

				GFXGltfBuffer* buffer =
					_GFX_FROM_GLTF_ACCESSOR(cattr->data);

				if (!_gfx_gltf_buffer_alloc(heap, dep, buffer))
				{
					gfx_log_error("Failed to allocate vertex buffer.");
					goto clean;
				}

				attributes[a] = (GFXAttribute){
					.offset = (uint32_t)cattr->data->offset,
					.rate = GFX_RATE_VERTEX,

					.format = _gfx_gltf_attribute_fmt(
						cattr->data->component_type,
						cattr->data->type,
						cattr->data->normalized),

					.stride = (uint32_t)(
						(cattr->data->buffer_view == NULL ||
						cattr->data->buffer_view->stride == 0) ?
							cattr->data->stride :
							cattr->data->buffer_view->stride),

					.buffer = gfx_ref_buffer_at(
						buffer->buffer,
						cattr->data->buffer_view->offset)
				};
			}

			if (numVertices == 0)
			{
				gfx_log_error("Primitives must have vertices.");
				goto clean;
			}

			// Allocate primitive.
			GFXPrimitive* prim = gfx_alloc_prim(heap,
				0, 0, _GFX_GLTF_TOPOLOGY(cprim->type),
				(uint32_t)numIndices, indexSize,
				(uint32_t)numVertices,
				numIndices > 0 ? gfx_ref_buffer_at(
					indexBuffer->buffer,
					cprim->indices->buffer_view->offset) : GFX_REF_NULL,
				numAttributes, attributes);

			if (prim == NULL) goto clean;

			// Insert primitive.
			GFXGltfPrimitive primitive = {
				.primitive = prim,
				.material = _GFX_FROM_GLTF(
					materials, data->materials, cprim->material)
			};

			if (!gfx_vec_push(&primitives, 1, &primitive))
			{
				gfx_free_prim(prim);
				goto clean;
			}
		}
	}

	// Create all meshes.
	for (size_t m = 0, p = 0; m < data->meshes_count; ++m)
	{
		// Insert mesh.
		GFXGltfMesh mesh = {
			.numPrimitives = data->meshes[m].primitives_count,
			.primitives = gfx_vec_at(&primitives, p)
		};

		if (!gfx_vec_push(&meshes, 1, &mesh))
			goto clean;

		p += mesh.numPrimitives;
	}

	// Create all nodes.
	for (size_t n = 0; n < data->nodes_count; ++n)
	{
		const cgltf_node* cnode = &data->nodes[n];
		numNodePtrs += cnode->children_count; // Keep count!

		// Insert node.
		GFXGltfNode node = {
			.flags = _GFX_GLTF_NODE_FLAGS(cnode),

			.parent = NULL,
			.children = NULL,
			.numChildren = cnode->children_count,

			.translation = { 0.0f, 0.0f, 0.0f },
			.rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
			.scale = { 1.0f, 1.0f, 1.0f },

			.mesh = _GFX_FROM_GLTF(meshes, data->meshes, cnode->mesh)
		};

		if (cnode->has_translation)
			memcpy(node.translation, cnode->translation, sizeof(node.translation));

		if (cnode->has_rotation)
			memcpy(node.rotation, cnode->rotation, sizeof(node.rotation));

		if (cnode->has_scale)
			memcpy(node.scale, cnode->scale, sizeof(node.scale));

		if (cnode->has_matrix)
			memcpy(node.matrix, cnode->matrix, sizeof(node.matrix));
		else
			_gfx_gltf_to_mat(node.matrix, node.translation, node.rotation, node.scale);

		if (!gfx_vec_push(&nodes, 1, &node))
			goto clean;
	}

	// Create all scenes.
	for (size_t s = 0; s < data->scenes_count; ++s)
	{
		numNodePtrs += data->scenes[s].nodes_count; // Keep count!

		// Insert scene.
		GFXGltfScene scene = {
			.numNodes = data->scenes[s].nodes_count,
			.nodes = NULL
		};

		if (!gfx_vec_push(&scenes, 1, &scene))
			goto clean;
	}

	// Set parent pointers of all nodes.
	for (size_t n = 0; n < data->nodes_count; ++n)
	{
		GFXGltfNode* node = gfx_vec_at(&nodes, n);
		node->parent = _GFX_FROM_GLTF(
			nodes, data->nodes, data->nodes[n].parent);
	}

	// Allocate all scene/node children-pointers.
	if (numNodePtrs > 0)
	{
		nodePtrs = malloc(sizeof(GFXGltfNode*) * numNodePtrs);
		if (nodePtrs == NULL) goto clean;

		size_t nodePtrsLoc = 0;

		// Set pointers in all nodes.
		for (size_t n = 0; n < data->nodes_count; ++n)
		{
			GFXGltfNode* node = gfx_vec_at(&nodes, n);
			node->children = nodePtrs + nodePtrsLoc;
			nodePtrsLoc += node->numChildren;

			for (size_t np = 0; np < node->numChildren; ++np)
				node->children[np] = _GFX_FROM_GLTF(
					nodes, data->nodes, data->nodes[n].children[np]);
		}

		// Set pointers in all scenes.
		for (size_t s = 0; s < data->scenes_count; ++s)
		{
			GFXGltfScene* scene = gfx_vec_at(&scenes, s);
			scene->nodes = nodePtrs + nodePtrsLoc;
			nodePtrsLoc += scene->numNodes;

			for (size_t np = 0; np < scene->numNodes; ++np)
				scene->nodes[np] = _GFX_FROM_GLTF(
					nodes, data->nodes, data->scenes[s].nodes[np]);
		}
	}

	// Set default scene.
	result->scene = _GFX_FROM_GLTF(scenes, data->scenes, data->scene);

	// We are done building groufix objects, free gltf things.
	cgltf_free(data);
	gfx_io_raw_clear(&source, src);

	// Claim all data and return.
	result->numBuffers = buffers.size;
	result->buffers = gfx_vec_claim(&buffers);

	result->numImages = images.size;
	result->images = gfx_vec_claim(&images);

	result->numSamplers = samplers.size;
	result->samplers = gfx_vec_claim(&samplers);

	result->numMaterials = materials.size;
	result->materials = gfx_vec_claim(&materials);

	result->numPrimitives = primitives.size;
	result->primitives = gfx_vec_claim(&primitives);

	result->numMeshes = meshes.size;
	result->meshes = gfx_vec_claim(&meshes);

	result->numNodes = nodes.size;
	result->nodes = gfx_vec_claim(&nodes);

	result->numScenes = scenes.size;
	result->scenes = gfx_vec_claim(&scenes);

	return 1;


	// Cleanup on failure.
clean:
	// Flush & block the heap so all memory transfers have been completed
	// and no command buffers reference the resources anymore!
	gfx_heap_flush(heap);
	gfx_heap_block(heap);

	for (size_t b = 0; b < buffers.size; ++b)
	{
		GFXGltfBuffer* buffer = gfx_vec_at(&buffers, b);
		free(buffer->bin);
		gfx_free_buffer(buffer->buffer);
	}

	for (size_t i = 0; i < images.size; ++i)
		gfx_free_image(*(GFXImage**)gfx_vec_at(&images, i));

	for (size_t p = 0; p < primitives.size; ++p)
		gfx_free_prim(((GFXGltfPrimitive*)gfx_vec_at(&primitives, p))->primitive);

	free(nodePtrs);
	gfx_vec_clear(&buffers);
	gfx_vec_clear(&images);
	gfx_vec_clear(&samplers);
	gfx_vec_clear(&materials);
	gfx_vec_clear(&primitives);
	gfx_vec_clear(&meshes);
	gfx_vec_clear(&nodes);
	gfx_vec_clear(&scenes);

	cgltf_free(data);
	gfx_io_raw_clear(&source, src);

	gfx_log_error("Failed to load glTF from stream.");

	return 0;
}

/****************************/
GFX_API void gfx_release_gltf(GFXGltfResult* result)
{
	assert(result != NULL);

	// First free all scene/node children-pointers.
	if (result->numNodes > 0)
		free(result->nodes[0].children);

	// And all non-GPU buffers.
	for (size_t b = 0; b < result->numBuffers; ++b)
		free(result->buffers[b].bin);

	free(result->buffers);
	free(result->images);
	free(result->samplers);
	free(result->materials);
	free(result->primitives);
	free(result->meshes);
	free(result->nodes);
	free(result->scenes);

	// Leave all values, result is invalidated.
}
