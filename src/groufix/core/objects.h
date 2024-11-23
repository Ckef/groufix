/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_OBJECTS_H
#define _GFX_CORE_OBJECTS_H

#include "groufix/containers/deque.h"
#include "groufix/containers/list.h"
#include "groufix/containers/vec.h"
#include "groufix/core/mem.h"
#include "groufix/core.h"


#define _GFX_GET_VK_BUFFER_USAGE(flags, usage) \
	(((flags) & GFX_MEMORY_READ ? \
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT : (VkBufferUsageFlags)0) | \
	((flags) & GFX_MEMORY_WRITE ? \
		VK_BUFFER_USAGE_TRANSFER_DST_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_VERTEX ? \
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_INDEX ? \
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_UNIFORM ? \
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_STORAGE ? \
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_INDIRECT ? \
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_UNIFORM_TEXEL ? \
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT : (VkBufferUsageFlags)0) | \
	((usage) & GFX_BUFFER_STORAGE_TEXEL ? \
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT : (VkBufferUsageFlags)0))

#define _GFX_GET_VK_IMAGE_TYPE(type) \
	(((type) == GFX_IMAGE_1D) ? VK_IMAGE_TYPE_1D : \
	((type) == GFX_IMAGE_2D) ? VK_IMAGE_TYPE_2D : \
	((type) == GFX_IMAGE_3D) ? VK_IMAGE_TYPE_3D : \
	((type) == GFX_IMAGE_3D_SLICED) ? VK_IMAGE_TYPE_3D : \
	((type) == GFX_IMAGE_CUBE) ? VK_IMAGE_TYPE_2D : \
	VK_IMAGE_TYPE_2D)

#define _GFX_GET_VK_IMAGE_VIEW_TYPE(type) \
	(((type) == GFX_VIEW_1D) ? VK_IMAGE_VIEW_TYPE_1D : \
	((type) == GFX_VIEW_1D_ARRAY) ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : \
	((type) == GFX_VIEW_2D) ? VK_IMAGE_VIEW_TYPE_2D : \
	((type) == GFX_VIEW_2D_ARRAY) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : \
	((type) == GFX_VIEW_CUBE) ? VK_IMAGE_VIEW_TYPE_CUBE : \
	((type) == GFX_VIEW_CUBE_ARRAY) ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : \
	((type) == GFX_VIEW_3D) ? VK_IMAGE_VIEW_TYPE_3D : \
	VK_IMAGE_VIEW_TYPE_2D)

#define _GFX_GET_VK_IMAGE_ASPECT(aspect) \
	(((aspect) & GFX_IMAGE_COLOR ? \
		VK_IMAGE_ASPECT_COLOR_BIT : (VkImageAspectFlags)0) | \
	((aspect) & GFX_IMAGE_DEPTH ? \
		VK_IMAGE_ASPECT_DEPTH_BIT : (VkImageAspectFlags)0) | \
	((aspect) & GFX_IMAGE_STENCIL ? \
		VK_IMAGE_ASPECT_STENCIL_BIT : (VkImageAspectFlags)0))

#define _GFX_GET_VK_IMAGE_USAGE(flags, usage, fmt) \
	(((flags) & GFX_MEMORY_READ ? \
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT : (VkImageUsageFlags)0) | \
	((flags) & GFX_MEMORY_WRITE ? \
		VK_IMAGE_USAGE_TRANSFER_DST_BIT : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED ? \
		VK_IMAGE_USAGE_SAMPLED_BIT : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED_LINEAR ? \
		VK_IMAGE_USAGE_SAMPLED_BIT : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED_MINMAX ? \
		VK_IMAGE_USAGE_SAMPLED_BIT : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_STORAGE ? \
		VK_IMAGE_USAGE_STORAGE_BIT : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_INPUT ? \
		VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_OUTPUT ? \
		(GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ? \
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : \
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) : (VkImageUsageFlags)0) | \
	((usage) & GFX_IMAGE_TRANSIENT ? \
		VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : (VkImageUsageFlags)0))

#define _GFX_GET_VK_SAMPLE_COUNT(count) \
	/* Vulkan sample counts are the hexadecimal equivalent. */ \
	(((count) == 1) ? VK_SAMPLE_COUNT_1_BIT : \
	((count) == 2) ? VK_SAMPLE_COUNT_2_BIT : \
	((count) == 4) ? VK_SAMPLE_COUNT_4_BIT : \
	((count) == 8) ? VK_SAMPLE_COUNT_8_BIT : \
	((count) == 16) ? VK_SAMPLE_COUNT_16_BIT : \
	((count) == 32) ? VK_SAMPLE_COUNT_32_BIT : \
	((count) == 64) ? VK_SAMPLE_COUNT_64_BIT : \
	VK_SAMPLE_COUNT_1_BIT)

#define _GFX_GET_VK_FORMAT_FEATURES(flags, usage, fmt) \
	(((flags) & GFX_MEMORY_READ ? \
		VK_FORMAT_FEATURE_TRANSFER_SRC_BIT : (VkFormatFeatureFlags)0) | \
	((flags) & GFX_MEMORY_WRITE ? \
		VK_FORMAT_FEATURE_TRANSFER_DST_BIT : (VkFormatFeatureFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED ? \
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT : (VkFormatFeatureFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED_LINEAR ? \
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT : (VkFormatFeatureFlags)0) | \
	((usage) & GFX_IMAGE_SAMPLED_MINMAX ? \
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT : (VkFormatFeatureFlags)0) | \
	((usage) & GFX_IMAGE_STORAGE ? \
		VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT : (VkFormatFeatureFlags)0) | \
	((usage) & (GFX_IMAGE_INPUT | GFX_IMAGE_OUTPUT) ? \
		(GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ? \
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : \
			VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) : (VkFormatFeatureFlags)0) | \
	((usage) & GFX_IMAGE_BLEND ? \
		VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT : (VkFormatFeatureFlags)0))

#define _GFX_GET_VK_PRIMITIVE_TOPOLOGY(topo) \
	((topo) == GFX_TOPO_POINT_LIST ? \
		VK_PRIMITIVE_TOPOLOGY_POINT_LIST : \
	(topo) == GFX_TOPO_LINE_LIST ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST : \
	(topo) == GFX_TOPO_LINE_STRIP ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_STRIP : \
	(topo) == GFX_TOPO_TRIANGLE_LIST ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : \
	(topo) == GFX_TOPO_TRIANGLE_STRIP ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : \
	(topo) == GFX_TOPO_TRIANGLE_FAN ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN : \
	(topo) == GFX_TOPO_LINE_LIST_ADJACENT ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY : \
	(topo) == GFX_TOPO_LINE_STRIP_ADJACENT ? \
		VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY : \
	(topo) == GFX_TOPO_TRIANGLE_LIST_ADJACENT ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY : \
	(topo) == GFX_TOPO_TRIANGLE_STRIP_ADJACENT ? \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY : \
	(topo) == GFX_TOPO_PATCH_LIST ? \
		VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : \
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)

#define _GFX_GET_VK_CULL_MODE(cull) \
	(((cull) == GFX_CULL_FRONT ? \
		VK_CULL_MODE_FRONT_BIT : (VkCullModeFlags)0) | \
	((cull) == GFX_CULL_BACK ? \
		VK_CULL_MODE_BACK_BIT : (VkCullModeFlags)0))

#define _GFX_GET_VK_POLYGON_MODE(mode) \
	(((mode) == GFX_RASTER_POINT) ? VK_POLYGON_MODE_POINT : \
	((mode) == GFX_RASTER_LINE) ? VK_POLYGON_MODE_LINE : \
	((mode) == GFX_RASTER_FILL) ? VK_POLYGON_MODE_FILL : \
	VK_POLYGON_MODE_FILL)

#define _GFX_GET_VK_FRONT_FACE(front) \
	(((front) == GFX_FRONT_FACE_CCW) ? VK_FRONT_FACE_COUNTER_CLOCKWISE : \
	((front) == GFX_FRONT_FACE_CW) ? VK_FRONT_FACE_CLOCKWISE : \
	VK_FRONT_FACE_CLOCKWISE)

#define _GFX_GET_VK_FILTER(filter) \
	(((filter) == GFX_FILTER_NEAREST) ? VK_FILTER_NEAREST : \
	((filter) == GFX_FILTER_LINEAR) ? VK_FILTER_LINEAR : \
	VK_FILTER_NEAREST)

#define _GFX_GET_VK_MIPMAP_MODE(filter) \
	(((filter) == GFX_FILTER_NEAREST) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : \
	((filter) == GFX_FILTER_LINEAR) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : \
	VK_SAMPLER_MIPMAP_MODE_NEAREST)

#define _GFX_GET_VK_REDUCTION_MODE(mode) \
	((mode) == GFX_FILTER_MODE_AVERAGE ? \
		VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE : \
	(mode) == GFX_FILTER_MODE_MIN ? \
		VK_SAMPLER_REDUCTION_MODE_MIN : \
	(mode) == GFX_FILTER_MODE_MAX ? \
		VK_SAMPLER_REDUCTION_MODE_MAX : \
		VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE)

#define _GFX_GET_VK_ADDRESS_MODE(wrap) \
	((wrap) == GFX_WRAP_REPEAT ? \
		VK_SAMPLER_ADDRESS_MODE_REPEAT : \
	(wrap) == GFX_WRAP_REPEAT_MIRROR ? \
		VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : \
	(wrap) == GFX_WRAP_CLAMP_TO_EDGE ? \
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : \
	(wrap) == GFX_WRAP_CLAMP_TO_EDGE_MIRROR ? \
		VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : \
	(wrap) == GFX_WRAP_CLAMP_TO_BORDER ? \
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : \
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)

#define _GFX_GET_VK_LOGIC_OP(op) \
	(((op) == GFX_LOGIC_NO_OP) ? VK_LOGIC_OP_COPY : \
	((op) == GFX_LOGIC_CLEAR) ? VK_LOGIC_OP_CLEAR : \
	((op) == GFX_LOGIC_SET) ? VK_LOGIC_OP_SET : \
	((op) == GFX_LOGIC_KEEP) ? VK_LOGIC_OP_NO_OP : \
	((op) == GFX_LOGIC_KEEP_INVERSE) ? VK_LOGIC_OP_INVERT : \
	((op) == GFX_LOGIC_INVERSE) ? VK_LOGIC_OP_COPY_INVERTED : \
	((op) == GFX_LOGIC_AND) ? VK_LOGIC_OP_AND : \
	((op) == GFX_LOGIC_AND_INVERSE) ? VK_LOGIC_OP_AND_INVERTED : \
	((op) == GFX_LOGIC_AND_REVERSE) ? VK_LOGIC_OP_AND_REVERSE : \
	((op) == GFX_LOGIC_NAND) ? VK_LOGIC_OP_NAND : \
	((op) == GFX_LOGIC_OR) ? VK_LOGIC_OP_OR : \
	((op) == GFX_LOGIC_OR_INVERSE) ? VK_LOGIC_OP_OR_INVERTED : \
	((op) == GFX_LOGIC_OR_REVERSE) ? VK_LOGIC_OP_OR_REVERSE : \
	((op) == GFX_LOGIC_XOR) ? VK_LOGIC_OP_XOR : \
	((op) == GFX_LOGIC_NOR) ? VK_LOGIC_OP_NOR : \
	((op) == GFX_LOGIC_EQUAL) ? VK_LOGIC_OP_EQUIVALENT : \
	VK_LOGIC_OP_COPY)

#define _GFX_GET_VK_BLEND_OP(op) \
	(((op) == GFX_BLEND_NO_OP) ? VK_BLEND_OP_ADD : \
	((op) == GFX_BLEND_ADD) ? VK_BLEND_OP_ADD : \
	((op) == GFX_BLEND_SUBTRACT) ? VK_BLEND_OP_SUBTRACT : \
	((op) == GFX_BLEND_SUBTRACT_REVERSE) ? VK_BLEND_OP_REVERSE_SUBTRACT : \
	((op) == GFX_BLEND_MIN) ? VK_BLEND_OP_MIN : \
	((op) == GFX_BLEND_MAX) ? VK_BLEND_OP_MAX : \
	VK_BLEND_OP_ADD)

#define _GFX_GET_VK_BLEND_FACTOR(factor) \
	(((factor) == GFX_FACTOR_ZERO) ? VK_BLEND_FACTOR_ZERO : \
	((factor) == GFX_FACTOR_ONE) ? VK_BLEND_FACTOR_ONE : \
	((factor) == GFX_FACTOR_SRC) ? VK_BLEND_FACTOR_SRC_COLOR : \
	((factor) == GFX_FACTOR_ONE_MINUS_SRC) ? VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR : \
	((factor) == GFX_FACTOR_DST) ? VK_BLEND_FACTOR_DST_COLOR : \
	((factor) == GFX_FACTOR_ONE_MINUS_DST) ? VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR : \
	((factor) == GFX_FACTOR_SRC_ALPHA) ? VK_BLEND_FACTOR_SRC_ALPHA : \
	((factor) == GFX_FACTOR_SRC_ALPHA_SATURATE) ? VK_BLEND_FACTOR_SRC_ALPHA_SATURATE : \
	((factor) == GFX_FACTOR_ONE_MINUS_SRC_ALPHA) ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : \
	((factor) == GFX_FACTOR_DST_ALPHA) ? VK_BLEND_FACTOR_DST_ALPHA : \
	((factor) == GFX_FACTOR_ONE_MINUS_DST_ALPHA) ? VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA : \
	((factor) == GFX_FACTOR_CONSTANT) ? VK_BLEND_FACTOR_CONSTANT_COLOR : \
	((factor) == GFX_FACTOR_ONE_MINUS_CONSTANT) ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR : \
	((factor) == GFX_FACTOR_CONSTANT_ALPHA) ? VK_BLEND_FACTOR_CONSTANT_ALPHA : \
	((factor) == GFX_FACTOR_ONE_MINUS_CONSTANT_ALPHA) ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA : \
	VK_BLEND_FACTOR_ZERO)

#define _GFX_GET_VK_COMPARE_OP(op) \
	(((op) == GFX_CMP_NEVER) ?  VK_COMPARE_OP_NEVER : \
	((op) == GFX_CMP_LESS) ? VK_COMPARE_OP_LESS : \
	((op) == GFX_CMP_LESS_EQUAL) ? VK_COMPARE_OP_LESS_OR_EQUAL : \
	((op) == GFX_CMP_GREATER) ? VK_COMPARE_OP_GREATER : \
	((op) == GFX_CMP_GREATER_EQUAL) ? VK_COMPARE_OP_GREATER_OR_EQUAL : \
	((op) == GFX_CMP_EQUAL) ? VK_COMPARE_OP_EQUAL : \
	((op) == GFX_CMP_NOT_EQUAL) ? VK_COMPARE_OP_NOT_EQUAL : \
	((op) == GFX_CMP_ALWAYS) ? VK_COMPARE_OP_ALWAYS : \
	VK_COMPARE_OP_ALWAYS)

#define _GFX_GET_VK_STENCIL_OP(op) \
	(((op) == GFX_STENCIL_KEEP) ? VK_STENCIL_OP_KEEP : \
	((op) == GFX_STENCIL_ZERO) ? VK_STENCIL_OP_ZERO : \
	((op) == GFX_STENCIL_REPLACE) ? VK_STENCIL_OP_REPLACE : \
	((op) == GFX_STENCIL_INVERT) ? VK_STENCIL_OP_INVERT : \
	((op) == GFX_STENCIL_INCR_CLAMP) ? VK_STENCIL_OP_INCREMENT_AND_CLAMP : \
	((op) == GFX_STENCIL_INCR_WRAP) ? VK_STENCIL_OP_INCREMENT_AND_WRAP : \
	((op) == GFX_STENCIL_DECR_CLAMP) ? VK_STENCIL_OP_DECREMENT_AND_CLAMP : \
	((op) == GFX_STENCIL_DECR_WRAP) ? VK_STENCIL_OP_DECREMENT_AND_WRAP : \
	VK_STENCIL_OP_KEEP)

#define _GFX_GET_VK_SHADER_STAGE(stage) \
	(((stage) & GFX_STAGE_VERTEX ? \
		VK_SHADER_STAGE_VERTEX_BIT : (VkShaderStageFlagBits)0) | \
	((stage) & GFX_STAGE_TESS_CONTROL ? \
		VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT : (VkShaderStageFlagBits)0) | \
	((stage) & GFX_STAGE_TESS_EVALUATION ? \
		VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT : (VkShaderStageFlagBits)0) | \
	((stage) & GFX_STAGE_GEOMETRY ? \
		VK_SHADER_STAGE_GEOMETRY_BIT : (VkShaderStageFlagBits)0) | \
	((stage) & GFX_STAGE_FRAGMENT ? \
		VK_SHADER_STAGE_FRAGMENT_BIT : (VkShaderStageFlagBits)0) | \
	((stage) & GFX_STAGE_COMPUTE ? \
		VK_SHADER_STAGE_COMPUTE_BIT : (VkShaderStageFlagBits)0))

#define _GFX_GET_VK_ACCESS_FLAGS(mask, fmt) \
	(((mask) & GFX_ACCESS_VERTEX_READ ? \
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_INDEX_READ ? \
		VK_ACCESS_INDEX_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_UNIFORM_READ ? \
		VK_ACCESS_UNIFORM_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_INDIRECT_READ ? \
		VK_ACCESS_INDIRECT_COMMAND_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_SAMPLED_READ ? \
		VK_ACCESS_SHADER_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_STORAGE_READ ? \
		VK_ACCESS_SHADER_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_STORAGE_WRITE ? \
		VK_ACCESS_SHADER_WRITE_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_INPUT ? \
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_READ ? \
		(GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ? \
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT : \
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT) : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_WRITE ? \
		(GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ? \
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : \
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_RESOLVE ? \
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_TRANSFER_READ ? \
		VK_ACCESS_TRANSFER_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_TRANSFER_WRITE ? \
		VK_ACCESS_TRANSFER_WRITE_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_HOST_READ ? \
		VK_ACCESS_HOST_READ_BIT : (VkAccessFlags)0) | \
	((mask) & GFX_ACCESS_HOST_WRITE ? \
		VK_ACCESS_HOST_WRITE_BIT : (VkAccessFlags)0))

#define _GFX_GET_VK_PIPELINE_STAGE(mask, stage, fmt) \
	(((mask) & GFX_ACCESS_VERTEX_READ ? \
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_INDEX_READ ? \
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_INDIRECT_READ ? \
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT : (VkPipelineStageFlags)0) | \
	((mask) & (GFX_ACCESS_UNIFORM_READ | GFX_ACCESS_SAMPLED_READ | \
	GFX_ACCESS_STORAGE_READ | GFX_ACCESS_STORAGE_WRITE) ? \
		(((stage) == 0 || ((stage) & GFX_STAGE_VERTEX) ? \
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : (VkPipelineStageFlags)0) | \
		((stage) == 0 || ((stage) & GFX_STAGE_TESS_CONTROL) ? \
			VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT : (VkPipelineStageFlags)0) | \
		((stage) == 0 || ((stage) & GFX_STAGE_TESS_EVALUATION) ? \
			VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT : (VkPipelineStageFlags)0) | \
		((stage) == 0 || ((stage) & GFX_STAGE_GEOMETRY) ? \
			VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT : (VkPipelineStageFlags)0) | \
		((stage) == 0 || ((stage) & GFX_STAGE_FRAGMENT) ? \
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : (VkPipelineStageFlags)0) | \
		((stage) == 0 || ((stage) & GFX_STAGE_COMPUTE) ? \
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : (VkPipelineStageFlags)0)) : \
		(VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_INPUT ? \
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : (VkPipelineStageFlags)0) | \
	((mask) & (GFX_ACCESS_ATTACHMENT_READ | GFX_ACCESS_ATTACHMENT_WRITE) ? \
		(GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ? \
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | \
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : \
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_ATTACHMENT_RESOLVE ? \
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_TRANSFER_READ ? \
		VK_PIPELINE_STAGE_TRANSFER_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_TRANSFER_WRITE ? \
		VK_PIPELINE_STAGE_TRANSFER_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_HOST_READ ? \
		VK_PIPELINE_STAGE_HOST_BIT : (VkPipelineStageFlags)0) | \
	((mask) & GFX_ACCESS_HOST_WRITE ? \
		VK_PIPELINE_STAGE_HOST_BIT : (VkPipelineStageFlags)0))

#define _GFX_GET_VK_IMAGE_LAYOUT(mask, fmt) \
	((mask) == 0 ? \
		VK_IMAGE_LAYOUT_UNDEFINED : /* Default is to discard. */ \
	!((mask) & ~(GFXAccessMask)(GFX_ACCESS_TRANSFER_READ | GFX_ACCESS_MODIFIERS)) ? \
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : \
	!((mask) & ~(GFXAccessMask)(GFX_ACCESS_TRANSFER_WRITE | GFX_ACCESS_MODIFIERS)) ? \
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : \
	(GFX_FORMAT_HAS_DEPTH_OR_STENCIL(fmt) ? \
		(!((mask) & ~(GFXAccessMask)(GFX_ACCESS_SAMPLED_READ | \
		GFX_ACCESS_ATTACHMENT_INPUT | GFX_ACCESS_ATTACHMENT_READ | \
		GFX_ACCESS_MODIFIERS)) ? \
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : \
		!((mask) & ~(GFXAccessMask)(GFX_ACCESS_ATTACHMENT_READ | \
		GFX_ACCESS_ATTACHMENT_WRITE | GFX_ACCESS_MODIFIERS)) ? \
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : \
			VK_IMAGE_LAYOUT_GENERAL) : \
		(!((mask) & ~(GFXAccessMask)(GFX_ACCESS_SAMPLED_READ | \
		GFX_ACCESS_ATTACHMENT_INPUT | GFX_ACCESS_MODIFIERS)) ? \
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : \
		!((mask) & ~(GFXAccessMask)(GFX_ACCESS_ATTACHMENT_READ | \
		GFX_ACCESS_ATTACHMENT_WRITE | GFX_ACCESS_ATTACHMENT_RESOLVE | \
		GFX_ACCESS_MODIFIERS)) ? \
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : \
			VK_IMAGE_LAYOUT_GENERAL)))


// Helper to remove unsupported pipeline stages.
#define _GFX_MOD_VK_PIPELINE_STAGE(vkStage, context) \
	((vkStage) & \
		~((!((context)->features & _GFX_SUPPORT_GEOMETRY_SHADER) ? \
			VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT : \
			(VkPipelineStageFlags)0) | \
		(!((context)->features & _GFX_SUPPORT_TESSELLATION_SHADER) ? \
			VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | \
			VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT : \
			(VkPipelineStageFlags)0)))


/****************************
 * Shading objects.
 ****************************/

/**
 * Get an index from a single shader stage
 * and total #stages that exist.
 * Indices are ordered the same as GFXShaderStage bit-flags!
 */
#define _GFX_GET_SHADER_STAGE_INDEX(stage) \
	((stage) == GFX_STAGE_VERTEX ? 0 : \
	(stage) == GFX_STAGE_TESS_CONTROL ? 1 : \
	(stage) == GFX_STAGE_TESS_EVALUATION ? 2 : \
	(stage) == GFX_STAGE_GEOMETRY ? 3 : \
	(stage) == GFX_STAGE_FRAGMENT ? 4 : \
	(stage) == GFX_STAGE_COMPUTE ? 5 : \
	6) /* Should not happen. */

#define _GFX_NUM_SHADER_STAGES 6


/**
 * Reflected shader resource.
 */
typedef struct _GFXShaderResource
{
	union {
		uint32_t location;
		uint32_t set;
		uint32_t id;
	};

	uint32_t binding;

	// Array size (increasing location for vert/frag io), 0 = unsized.
	size_t count;

	// Struct size, 0 if not a struct or unknown.
	size_t size;

	// Undefined if not a 'non-attachment image'.
	GFXViewType viewType;


	// Resource type.
	enum
	{
		_GFX_SHADER_VERTEX_INPUT,
		_GFX_SHADER_FRAGMENT_OUTPUT,
		_GFX_SHADER_BUFFER_UNIFORM, // Can be dynamic.
		_GFX_SHADER_BUFFER_STORAGE, // Can be dynamic.
		_GFX_SHADER_BUFFER_UNIFORM_TEXEL,
		_GFX_SHADER_BUFFER_STORAGE_TEXEL,
		_GFX_SHADER_IMAGE_AND_SAMPLER,
		_GFX_SHADER_IMAGE_SAMPLED,
		_GFX_SHADER_IMAGE_STORAGE,
		_GFX_SHADER_SAMPLER,
		_GFX_SHADER_ATTACHMENT_INPUT,
		_GFX_SHADER_CONSTANT

	} type;

} _GFXShaderResource;


/**
 * Internal shader.
 */
struct GFXShader
{
	_GFXDevice*  device; // Associated GPU to use as target environment.
	_GFXContext* context;
	uintptr_t    handle;

	GFXShaderStage stage;


	// Reflection metadata.
	struct
	{
		uint32_t push; // Push constant block size.
		size_t   locations;
		size_t   sets;
		size_t   bindings;
		size_t   constants;

		// Order:
		//  inputs/outputs (sorted on location).
		//  descriptor bindings (sorted on set, then binding).
		//  constants (unsorted).
		_GFXShaderResource* resources;

	} reflect;


	// Vulkan fields.
	struct
	{
		VkShaderModule module;

	} vk;
};


/****************************
 * Memory objects.
 ****************************/

/**
 * Staging buffer.
 */
typedef struct _GFXStaging
{
	GFXListNode  list;  // Base-type.
	_GFXMemAlloc alloc; // Stores the size.


	// Vulkan fields.
	struct
	{
		VkBuffer buffer;
		void*    ptr;

	} vk;

} _GFXStaging;


/**
 * Transfer operation(s).
 */
typedef struct _GFXTransfer
{
	GFXList stagings; // References _GFXStaging, automatically freed.
	bool    flushed;


	// Vulkan fields.
	struct
	{
		VkCommandBuffer cmd;
		VkFence         done; // Mostly for polling.

	} vk;

} _GFXTransfer;


/**
 * Transfer operation pool.
 */
typedef struct _GFXTransferPool
{
	GFXDeque  transfers; // Stores _GFXTransfer.
	GFXVec    deps;      // Stores GFXInject.
	_GFXQueue queue;
	_GFXMutex lock;

	struct _GFXInjection* injection;

	// #blocking threads.
	atomic_uintmax_t blocking;


	// Vulkan fields.
	struct
	{
		VkCommandPool pool;

	} vk;

} _GFXTransferPool;


/**
 * Internal heap.
 */
struct GFXHeap
{
	_GFXAllocator allocator; // Has both _GFXDevice* and _GFXContext*.
	_GFXMutex     lock;      // For allocation.

	GFXList buffers;    // References _GFXBuffer.
	GFXList images;     // References _GFXImage.
	GFXList primitives; // References _GFXPrimitive.
	GFXList groups;     // References _GFXGroup.


	// Operation resources,
	//  for both the graphics and transfer queues.
	struct
	{
		_GFXTransferPool graphics;
		_GFXTransferPool transfer;
		uint32_t         compute; // Family index only.

	} ops;
};


/**
 * Internal buffer.
 */
typedef struct _GFXBuffer
{
	GFXBuffer   base;
	GFXHeap*    heap;
	GFXListNode list;

	_GFXMemAlloc alloc;


	// Vulkan fields.
	struct
	{
		VkBuffer buffer;

	} vk;

} _GFXBuffer;


/**
 * Internal image.
 */
typedef struct _GFXImage
{
	GFXImage    base;
	GFXHeap*    heap;
	GFXListNode list;

	_GFXMemAlloc alloc;


	// Vulkan fields.
	struct
	{
		VkFormat format;
		VkImage  image;

	} vk;

} _GFXImage;


/**
 * Primitive buffer (i.e. Vulkan vertex input binding).
 */
typedef struct _GFXPrimBuffer
{
	_GFXBuffer* buffer;
	uint64_t    offset; // Offset to bind at.
	uint32_t    stride;
	uint64_t    size; // Total size (including the last attribute) in bytes.

	VkVertexInputRate rate;

} _GFXPrimBuffer;


/**
 * Internal vertex attribute.
 */
typedef struct _GFXAttribute
{
	GFXAttribute base;
	uint32_t     binding; // Vulkan input binding.


	// Vulkan fields.
	struct
	{
		VkFormat format;

	} vk;

} _GFXAttribute;


/**
 * Internal primitive geometry (superset of buffer).
 */
typedef struct _GFXPrimitive
{
	GFXPrimitive base;
	_GFXBuffer   buffer; // vk.buffer is VK_NULL_HANDLE if nothing is allocated.
	GFXBufferRef index;  // May be GFX_REF_NULL.

	size_t          numBindings;
	_GFXPrimBuffer* bindings; // Vulkan input bindings.

	size_t        numAttribs;
	_GFXAttribute attribs[]; // No reference is GFX_REF_NULL.

} _GFXPrimitive;


/**
 * Internal group binding.
 */
typedef struct _GFXBinding
{
	GFXBinding base;
	uint64_t   stride; // Element stride in bytes.

} _GFXBinding;


/**
 * Internal resource group (superset of buffer).
 */
typedef struct _GFXGroup
{
	GFXGroup   base;
	_GFXBuffer buffer; // vk.buffer is VK_NULL_HANDLE if nothing is allocated.

	size_t      numBindings;
	_GFXBinding bindings[]; // No reference is GFX_REF_NULL.

} _GFXGroup;


/****************************
 * Rendering objects.
 ****************************/

/**
 * Retrieve build generation from a _GFXImageAttach or _GFXRenderPass pointer.
 */
#define _GFX_ATTACH_GEN(attach) \
	(((_GFXAttach*)((char*)(attach) - offsetof(_GFXAttach, image)))->gen)

#define _GFX_PASS_GEN(pass) \
	(((_GFXRenderPass*)(pass))->gen)


/**
 * Attachment backing.
 */
typedef struct _GFXBacking
{
	GFXListNode  list; // Base-type.
	_GFXMemAlloc alloc;

	unsigned int purge; // If stale, index of frame to purge at.


	// Vulkan fields.
	struct
	{
		VkImage image;

	} vk;

} _GFXBacking;


/**
 * Image (implicit) attachment.
 */
typedef struct _GFXImageAttach
{
	GFXAttachment base;
	GFXList       backings; // References _GFXBacking.

	// Resolved size.
	uint32_t width;
	uint32_t height;
	uint32_t depth;

	// Set by dependency objects, signaled out of the renderer.
	bool signaled;


	// Vulkan fields.
	struct
	{
		VkFormat format;
		VkImage  image; // Most recent (for locality).

	} vk;

} _GFXImageAttach;


/**
 * Window attachment.
 */
typedef struct _GFXWindowAttach
{
	_GFXWindow*       window;
	_GFXRecreateFlags flags; // Used by virtual frames, from last submission.

	// Inherits all resources from window.

} _GFXWindowAttach;


/**
 * Internal attachment.
 */
typedef struct _GFXAttach
{
	// Build generation (to update set entries), persistent, never 0!
	uint32_t gen;


	// Attachment type.
	enum
	{
		_GFX_ATTACH_EMPTY,
		_GFX_ATTACH_IMAGE,
		_GFX_ATTACH_WINDOW

	} type;


	// Attachment data.
	union
	{
		_GFXImageAttach image;
		_GFXWindowAttach window;
	};

} _GFXAttach;


/**
 * Recording command pool.
 */
typedef struct _GFXRecorderPool
{
	size_t used; // #used buffers in cmds.


	// Vulkan fields.
	struct
	{
		VkCommandPool pool;
		GFXVec        cmds; // Stores VkCommandBuffer.

	} vk;

} _GFXRecorderPool;


/**
 * Internal recorder.
 */
struct GFXRecorder
{
	GFXListNode  list; // Base-type.
	GFXRenderer* renderer;
	_GFXContext* context; // For locality.
	_GFXPoolSub  sub;     // For descriptor access.


	// Recording input.
	struct
	{
		GFXPass*        pass;
		VkCommandBuffer cmd;

	} inp;


	// Current state.
	struct
	{
		GFXViewport    viewport;
		GFXScissor     scissor;
		_GFXCacheElem* pipeline;
		_GFXPrimitive* primitive;

	} state;


	// Recording output.
	struct
	{
		GFXVec cmds; // Stores { unsigned int, VkCommandBuffer } (sorted).

	} out;


	unsigned int     current; // Current virtual frame index.
	_GFXRecorderPool pools[]; // Two { graphics, compute } for each virtual frame.
};


/**
 * Frame synchronization (swapchain acquisition) object.
 */
typedef struct _GFXFrameSync
{
	_GFXWindow* window;
	size_t      backing; // Attachment index.
	uint32_t    image;   // Swapchain image index (or UINT32_MAX).


	// Vulkan fields.
	struct
	{
		VkSemaphore available;

	} vk;

} _GFXFrameSync;


/**
 * Frame recording pool.
 */
typedef struct _GFXFramePool
{
	// Vulkan fields.
	struct
	{
		VkCommandPool   pool;
		VkCommandBuffer cmd;
		VkFence         done;

	} vk;

} _GFXFramePool;


/**
 * Internal virtual frame.
 */
struct GFXFrame
{
	unsigned int index;

	GFXVec refs;  // Stores size_t, for each attachment; index into syncs (or SIZE_MAX).
	GFXVec syncs; // Stores _GFXFrameSync, one for each window attachment.

	_GFXFramePool graphics;
	_GFXFramePool compute;

	enum {
		_GFX_FRAME_GRAPHICS = 0x0001,
		_GFX_FRAME_COMPUTE  = 0x0002

	} submitted;


	// Vulkan fields.
	struct
	{
		VkSemaphore rendered;

	} vk;
};


/**
 * Internal renderer.
 */
struct GFXRenderer
{
	GFXHeap*  heap;  // Has both _GFXDevice* and _GFXContext*.
	_GFXCache cache; // Has _GFXContext*.
	_GFXPool  pool;  // Has _GFXContext*.
	_GFXQueue graphics;
	_GFXQueue present;
	_GFXQueue compute;

	GFXList   recorders;  // References GFXRecorder.
	GFXList   techniques; // References GFXTechnique.
	GFXList   sets;       // References GFXSet.
	_GFXMutex lock;       // For recorders, techniques & sets (and stales).

	// Current virtual frame state.
	bool recording;

	GFXFrame* public; // Public frame, if not NULL, user has access.
	GFXDeque  stales; // Stores { unsigned int, (Vk*)+ }.


	// Render backing (i.e. attachments).
	struct
	{
		GFXVec attachs; // Stores _GFXAttach.

		enum {
			_GFX_BACKING_INVALID,
			_GFX_BACKING_VALIDATED,
			_GFX_BACKING_BUILT

		} state;

	} backing;


	// Render graph (directed acyclic graph of passes).
	struct
	{
		size_t numRender; // Number of render & inline compute passes.
		GFXVec sinks;     // Stores GFXPass* (sink passes, tree roots).
		GFXVec passes;    // Stores GFXPass* (in submission order).

		size_t culledRender;
		size_t culledCompute;

		enum {
			_GFX_GRAPH_EMPTY,
			_GFX_GRAPH_INVALID, // Needs to purge.
			_GFX_GRAPH_VALIDATED,
			_GFX_GRAPH_WARMED,
			_GFX_GRAPH_BUILT

		} state;

	} graph;


	// Render frame (i.e. collection of virtual frames).
	unsigned int numFrames;
	unsigned int current; // Next frame to submit.
	GFXFrame     frames[];
};


/**
 * Internal attachment consumption.
 */
typedef struct _GFXConsume
{
	GFXAccessMask  mask;
	GFXShaderStage stage;
	GFXView        view; // index used as attachment index.

	GFXImageAspect  cleared;
	GFXBlendOpState color;
	GFXBlendOpState alpha;
	size_t          resolve; // Or SIZE_MAX.

	enum {
		_GFX_CONSUME_VIEWED = 0x0001, // Set to use view.type.
		_GFX_CONSUME_BLEND  = 0x0002  // Set to use blend operation states.

	} flags;

	union {
		// Identical definitions!
		GFXClear gfx;
		VkClearValue vk;

	} clear;


	// Graph output (relative to neighbouring passes).
	struct
	{
		VkImageLayout initial;
		VkImageLayout final;

		// Non-NULL to form a dependency.
		const struct _GFXConsume* prev;

	} out;


	// Building output (can be invalidated).
	struct
	{
		size_t view; // Index into vk.views of master pass, or SIZE_MAX.

		// Next consumption in this subpass chain.
		const struct _GFXConsume* next;

	} build;

} _GFXConsume;


/**
 * Internal pass (i.e. render/compute pass).
 */
struct GFXPass
{
	GFXPassType  type;
	GFXRenderer* renderer;
	unsigned int level;  // Determines submission order.
	unsigned int group;  // Cull group of the pass.

	unsigned int order;  // Actual submission order.
	unsigned int childs; // Number of unculled (!) passes this is a parent of.
	bool         culled;

	// Stores _GFXConsume.
	GFXVec consumes;

	// Stores GFXInject, from pass inject.
	GFXVec deps;
};


/**
 * Internal render pass.
 */
typedef struct _GFXRenderPass
{
	GFXPass  base;
	uint32_t gen; // Build generation (to invalidate pipelines).


	// Pipeline state input.
	struct
	{
		GFXRasterState  raster;
		GFXBlendState   blend;
		GFXDepthState   depth;
		GFXStencilState stencil;
		GFXViewport     viewport;
		GFXScissor      scissor;
		unsigned char   samples; // Minimum necesary, set on warmup.

		enum {
			_GFX_PASS_DEPTH   = 0x0001,
			_GFX_PASS_STENCIL = 0x0002

		} enabled; // Set on warmup.

	} state;


	// Graph output (relative to neighbouring passes).
	struct
	{
		struct _GFXRenderPass* master; // First subpass, NULL if this.
		struct _GFXRenderPass* next;   // Next subpass in the chain, NULL if last.

		uint32_t subpass; // Subpass index.
		size_t   backing; // Window attachment index (or SIZE_MAX).

	} out;


	// Building output (can be invalidated).
	struct
	{
		uint32_t fWidth;
		uint32_t fHeight;
		uint32_t fLayers;

		_GFXCacheElem* pass; // Built on warmup.

	} build;


	// Vulkan fields.
	struct
	{
		VkRenderPass pass;   // For locality.
		GFXVec       clears; // Stores VkClearValue.
		GFXVec       blends; // Stores { VkPipelineColorBlendAttachmentState, char }.
		GFXVec       views;  // Stores { _GFXConsume*, VkImageView }.
		GFXVec       frames; // Stores { VkImageView, VkFramebuffer }.

	} vk;


	// Parent passes.
	size_t   numParents;
	GFXPass* parents[];

} _GFXRenderPass;


/**
 * Internal compute pass.
 */
typedef struct _GFXComputePass
{
	GFXPass base;

	// Nothing special to do for compute passes.


	// Parent passes.
	size_t   numParents;
	GFXPass* parents[];

} _GFXComputePass;


/**
 * Internal technique (i.e. shader pipeline).
 */
struct GFXTechnique
{
	GFXListNode  list; // Base-type.
	GFXRenderer* renderer;

	GFXShader*     shaders[_GFX_NUM_SHADER_STAGES]; // May contain NULL.
	size_t         numSets;
	uint32_t       pushSize;
	GFXShaderStage pushStages;

	// Sorted on { stage, id }.
	GFXVec constants; // Stores { uint32_t stage, uint32_t id, size_t, GFXConstant }.

	// All sorted on { set, binding, index }.
	GFXVec samplers;  // Stores { size_t set, GFXSampler }, temporary!
	GFXVec immutable; // Stores { size_t set, size_t binding }.
	GFXVec dynamic;   // Stores { size_t set, size_t binding }.

	// Vulkan fields.
	struct { VkPipelineLayout layout; } vk; // For locality.

	// Locking output.
	_GFXCacheElem* layout;       // Pipeline layout, NULL until locked.
	_GFXCacheElem* setLayouts[]; // Set layouts (sorted), all NULL until locked.
};


/**
 * Set update entry (i.e. descriptor info).
 */
typedef struct _GFXSetEntry
{
	GFXReference   ref; // GFX_REF_NULL if empty or sampler.
	GFXRange       range;
	GFXViewType    viewType; // For attachment inputs ONLY!.
	_GFXCacheElem* sampler;  // May be NULL.

	// For attachment references.
	atomic_uint_least32_t gen;


	// Vulkan fields.
	struct
	{
		VkFormat format; // For texel buffers.

		union {
			VkDescriptorBufferInfo buffer;
			VkDescriptorImageInfo  image;
			VkBufferView           view;

		} update; // Named for addressability.

	} vk;

} _GFXSetEntry;


/**
 * Set binding (i.e. descriptor binding info).
 */
typedef struct _GFXSetBinding
{
	VkDescriptorType type;     // Undefined if empty.
	GFXViewType      viewType; // Undefined if not a 'non-attachment image'.

	size_t        count;   // 0 = empty binding.
	size_t        size;    // 0 = not a struct or unknown.
	_GFXSetEntry* entries; // NULL if empty or immutable samplers only.
	char*         hash;

} _GFXSetBinding;


/**
 * Internal set (i.e. descriptor set).
 */
struct GFXSet
{
	GFXListNode    list; // Base-type.
	GFXRenderer*   renderer;
	_GFXCacheElem* setLayout;
	_GFXSetEntry*  first;
	_GFXHashKey*   key;

	// If used since last modification.
	atomic_bool used;

	size_t numAttachs;  // #referenced attachments.
	size_t numDynamics; // #dynamic buffer entries.
	size_t numBindings;

	_GFXSetBinding bindings[]; // Sorted, no gaps.
};


/****************************
 * Resource reference operations.
 ****************************/

/**
 * Unpacked memory resource reference.
 * Access is not thread-safe with respect to the referenced object (!).
 */
typedef struct _GFXUnpackRef
{
	// Unpacked reference value(s),
	//  buffer offset | attachment index | 0.
	uint64_t value;


	// Referenced object (all mutually exclusive).
	struct
	{
		_GFXBuffer*  buffer;
		_GFXImage*   image;
		GFXRenderer* renderer;

	} obj;

} _GFXUnpackRef;


/**
 * Check for equality of unpacked references & getters.
 * Only checks for resource equality, offsets are ignored.
 * Getters will resolve to NULL or 0 if none found.
 */
#define _GFX_UNPACK_REF_IS_EQUAL(refa, refb) \
	(((refa).obj.buffer != NULL && \
		(refa).obj.buffer == (refb).obj.buffer) || \
	((refa).obj.image != NULL && \
		(refa).obj.image == (refb).obj.image) || \
	((refa).obj.renderer != NULL && (refa).value == (refb).value && \
		(refa).obj.renderer == (refb).obj.renderer))

#define _GFX_UNPACK_REF_CONTEXT(ref) \
	((ref).obj.buffer != NULL ? \
		(ref).obj.buffer->heap->allocator.context : \
	(ref).obj.image != NULL ? \
		(ref).obj.image->heap->allocator.context : \
	(ref).obj.renderer != NULL ? \
		(ref).obj.renderer->cache.context : NULL)

#define _GFX_UNPACK_REF_HEAP(ref) \
	((ref).obj.buffer != NULL ? (ref).obj.buffer->heap : \
	(ref).obj.image != NULL ? (ref).obj.image->heap : \
	(ref).obj.renderer != NULL ? (ref).obj.renderer->heap : NULL)

#define _GFX_UNPACK_REF_ATTACH(ref) \
	((ref).obj.renderer == NULL ? NULL : \
		&((_GFXAttach*)gfx_vec_at( \
			&(ref).obj.renderer->backing.attachs, (ref).value))->image)

/**
 * Retrieves the memory flags associated with an unpacked reference.
 * Meant for the debug build, where we validate flags and usages.
 */
#if defined (NDEBUG)
	#define _GFX_UNPACK_REF_FLAGS(ref) \
		_Static_assert(0, "Use _GFX_UNPACK_REF_FLAGS in debug mode only.")
#else
	#define _GFX_UNPACK_REF_FLAGS(ref) \
		((ref).obj.buffer != NULL ? \
			(ref).obj.buffer->base.flags : \
		(ref).obj.image != NULL ? \
			(ref).obj.image->base.flags : \
		(ref).obj.renderer != NULL ? \
			_GFX_UNPACK_REF_ATTACH(ref)->base.flags : 0)
#endif


/**
 * Calculates the remaining size of a buffer reference from its offset.
 * The size is dictated by the top-most object being referenced, not by the
 * underlying resource (e.g. the size claimed for a group buffer).
 * @return Zero if ref is not a buffer reference.
 */
uint64_t _gfx_ref_size(GFXReference ref);

/**
 * Resolves & validates a memory reference, meaning:
 * if it references a reference, it will recursively return that reference.
 * @return A user-land reference to the object actually holding the memory.
 *
 * Assumes no self-references exist!
 * Returns GFX_REF_NULL and warns when the reference is invalid.
 */
GFXReference _gfx_ref_resolve(GFXReference ref);

/**
 * Resolves & unpacks a memory resource reference, meaning:
 * if an object is composed of other memory objects internally, it will be
 * 'unpacked' into its elementary non-composed memory objects.
 *
 * Returns empty (all NULL's) and warns when the reference is invalid.
 * If in debug mode & out of bounds, it silently warns.
 */
_GFXUnpackRef _gfx_ref_unpack(GFXReference ref);


/****************************
 * Dependency injection objects & operations.
 ****************************/

/**
 * Dependency injection metadata.
 */
typedef struct _GFXInjection
{
	// Operation input, must be pre-initialized!
	struct
	{
		GFXRenderer* renderer; // To signal attachments.
		GFXPass*     pass;     // To search for access/stage flags.

		size_t               numRefs; // May be zero!
		const _GFXUnpackRef* refs;
		const GFXAccessMask* masks;
		const uint64_t*      sizes; // Must contain _gfx_ref_size(..)!

		// Vulkan family & queue index.
		struct { uint32_t family, index; } queue;

	} inp;


	// Injected (to-be-flushed) barriers.
	struct
	{
		// Barrier metadata.
		VkPipelineStageFlags srcStage;
		VkPipelineStageFlags dstStage;

		// Memory barriers.
		size_t                 numBufs;
		VkBufferMemoryBarrier* bufs;

		size_t                numImgs;
		VkImageMemoryBarrier* imgs;

	} bars;


	// Synchronization output.
	struct
	{
		size_t       numWaits;
		VkSemaphore* waits;

		size_t       numSigs;
		VkSemaphore* sigs;

		// Wait stages, of the same size as waits.
		VkPipelineStageFlags* stages;

	} out;

} _GFXInjection;


/**
 * Synchronization (metadata) object.
 */
typedef struct _GFXSync
{
	_GFXUnpackRef ref;
	GFXRange      range; // Unpacked, i.e. normalized offset & non-zero size.
	unsigned int  waits; // #wait commands left to recycle (if used).

	// For attachment references.
	uint32_t gen;

	// Claimed by (injections can be async), may be NULL.
	const _GFXInjection* inj;


	// Stage in the object's lifecycle.
	enum
	{
		_GFX_SYNC_UNUSED, // Only `flags` and `vk.signaled` are defined.
		_GFX_SYNC_PREPARE,
		_GFX_SYNC_PREPARE_CATCH, // Within the same injection.
		_GFX_SYNC_PENDING,
		_GFX_SYNC_CATCH,
		_GFX_SYNC_USED

	} stage;


	// Synchronization flags.
	enum
	{
		_GFX_SYNC_SEMAPHORE  = 0x0001, // If `vk.signaled` is used.
		_GFX_SYNC_BARRIER    = 0x0002, // Set to inject barrier on catch.
		_GFX_SYNC_MEM_HAZARD = 0x0004  // Memory barrier required if set.

	} flags;


	// Vulkan fields.
	struct
	{
		VkSemaphore signaled; // May be VK_NULL_HANDLE.

		// Barrier metadata.
		VkAccessFlags srcAccess;
		VkAccessFlags dstAccess;
		VkImageLayout oldLayout;
		VkImageLayout newLayout;
		VkPipelineStageFlags srcStage;
		VkPipelineStageFlags dstStage;
		VkPipelineStageFlags semStages; // Only set if `signaled` is used.

		// Family & queue indices.
		struct { uint32_t family; } srcQueue;
		struct { uint32_t family, index; } dstQueue;

		// Unpacked for locality.
		union {
			VkBuffer buffer;
			VkImage  image;
		};

	} vk;

} _GFXSync;


/**
 * Internal dependency object.
 */
struct GFXDependency
{
	_GFXDevice*  device;
	_GFXContext* context;

	unsigned int waitCapacity;

	size_t    sems;  // #semaphores at the front of `syncs`.
	GFXDeque  syncs; // Stores _GFXSync.
	_GFXMutex lock;

	// Vulkan family & queue indices.
	struct { uint32_t family, index; } graphics;
	struct { uint32_t family, index; } compute;
	struct { uint32_t family, index; } transfer;
};


/**
 * Starts a new dependency injection (initializes metadata).
 * The object pointed to by injection cannot be moved or copied!
 */
static inline void _gfx_injection(_GFXInjection* injection)
{
	injection->bars.srcStage = 0;
	injection->bars.dstStage = 0;
	injection->bars.numBufs = 0;
	injection->bars.bufs = NULL;
	injection->bars.numImgs = 0;
	injection->bars.imgs = NULL;

	injection->out.numWaits = 0;
	injection->out.waits = NULL;
	injection->out.numSigs = 0;
	injection->out.sigs = NULL;
	injection->out.stages = NULL;
}

/**
 * Flushes all stored barriers injected by _gfx_injection_push.
 * Automatically flushed by a successful call to _gfx_deps_(catch|prepare).
 * @param context   Cannot be NULL.
 * @param cmd       To record all barriers to, cannot be VK_NULL_HANDLE.
 * @param injection Barrier metadata to flush, cannot be NULL.
 *
 * Must be called before _gfx_deps_(abort|finish)!
 */
void _gfx_injection_flush(_GFXContext* context, VkCommandBuffer cmd,
                          _GFXInjection* injection);

/**
 * Pushes an execution/memory barrier for the next _gfx_injection_flush.
 * Should be used internally to inject barriers without using dependency objects.
 * @param injection Barrier metadata to append to, cannot be NULL.
 * @return Zero on failure.
 *
 * Can only set one of `bmb` OR `imb` to non-NULL!
 */
bool _gfx_injection_push(VkPipelineStageFlags srcStage,
                         VkPipelineStageFlags dstStage,
                         const VkBufferMemoryBarrier* bmb,
                         const VkImageMemoryBarrier* imb,
                         _GFXInjection* injection);

/**
 * Completes dependency injections by catching pending signal commands.
 * @param context   Cannot be NULL.
 * @param cmd       To record some initial barriers to, cannot be VK_NULL_HANDLE.
 * @param numInjs   Number of given injection commands.
 * @param injs      Given injection commands.
 * @param injection Input & output injection metadata, cannot be NULL.
 * @param Zero on failure, must call _gfx_deps_abort.
 *
 * Thread-safe with respect to all dependency objects!
 *
 * Can be called any number of times using the same injection metadata pointer.
 * However, after the first call to `_gfx_deps_abort` or `_gfx_deps_finish`,
 * neither `_gfx_deps_catch` nor `_gfx_deps_prepare` can be called anymore.
 *
 * Every injection command passed to _gfx_deps_(catch|prepare) must
 * subsequently be passed to a call to _gfx_deps_abort or _gfx_deps_finish.
 * These subsequent calls MUST take the same injection metadata pointer.
 *
 * Inbetween calls injection->inp may be altered.
 * In fact, they must be altered if operation references were given.
 *
 * Right before the first call to _gfx_deps_(abort|finish),
 * all output arrays in injection may be externally realloc'd,
 * they will be properly freed when aborted or finished.
 */
bool _gfx_deps_catch(_GFXContext* context, VkCommandBuffer cmd,
                     size_t numInjs, const GFXInject* injs,
                     _GFXInjection* injection);

/**
 * Starts dependency injections by preparing new signal commands.
 * @param blocking Non-zero to indicate the operation is blocking.
 * @see _gfx_deps_catch.
 *
 * Thread-safe with respect to all dependency objects!
 *
 * All commands are _always_ already visible to subsequent calls to
 * _gfx_deps_catch taking the same injection metadata pointer.
 */
bool _gfx_deps_prepare(_GFXContext* context, VkCommandBuffer cmd,
                       bool blocking,
                       size_t numInjs, const GFXInject* injs,
                       _GFXInjection* injection);

/**
 * Aborts a dependency injection, cleaning all metadata.
 * @see _gfx_deps_catch.
 *
 * Thread-safe with respect to all dependency objects!
 * The content of injection is invalidated after this call.
 *
 * Each injection metadata object must be called at least once with
 * either _gfx_deps_abort OR _gfx_deps_catch for ALL injection commands.
 * NEVER can both calls be used for the same injection metadata pointer!
 */
void _gfx_deps_abort(size_t numInjs, const GFXInject* injs,
                     _GFXInjection* injection);

/**
 * Finalizes a dependency injection, all signal commands are made visible for
 * future wait commands and all wait commands are finalized and cleaned up.
 * @see _gfx_deps_catch.
 *
 * Thread-safe with respect to all dependency objects!
 * The content of injection is invalidated after this call.
 */
void _gfx_deps_finish(size_t numInjs, const GFXInject* injs,
                      _GFXInjection* injection);


/****************************
 * Heap allocation & transfer flushing.
 ****************************/

/**
 * Allocates a backing image from a heap.
 * @param heap   Cannot be NULL.
 * @param attach Cannot be NULL, { .width, .height, .depth } > 0.
 *
 * Thread-safe with respect to the heap!
 * Leaves the `purge` index and `list` base-type uninitialized!
 */
_GFXBacking* _gfx_alloc_backing(GFXHeap* heap,
                                const _GFXImageAttach* attach);

/**
 * Frees a backing image.
 * @param heap    Cannot be NULL, same heap backing was allocated with.
 * @param backing Cannot be NULL.
 *
 * Thread-safe with respect to the heap!
 * Does not unlink itself from anything!
 */
void _gfx_free_backing(GFXHeap* heap, _GFXBacking* backing);

/**
 * Allocates a staging buffer from a heap.
 * @param heap Cannot be NULL.
 * @param size Must be > 0.
 * @return NULL on failure.
 *
 * Thread-safe with respect to the heap!
 * Leaves the `list` base-type uninitialized!
 */
_GFXStaging* _gfx_alloc_staging(GFXHeap* heap,
                                VkBufferUsageFlags usage, uint64_t size);

/**
 * Frees a staging buffer.
 * @param heap    Cannot be NULL, same heap staging was allocated with.
 * @param staging Cannot be NULL.
 *
 * Thread-safe with respect to the heap!
 * Does not unlink itself from anything!
 */
void _gfx_free_staging(GFXHeap* heap, _GFXStaging* staging);

/**
 * Frees all staging buffers of a transfer operation.
 * @param heap     Cannot be NULL.
 * @param transfer Cannot be NULL.
 *
 * Thread-safe with respect to the heap!
 * Leaves `transfer->stagings` cleared.
 */
void _gfx_free_stagings(GFXHeap* heap, _GFXTransfer* transfer);

/**
 * Flushes the last (current) transfer operation of a transfer pool.
 * The `injection` and `deps` fields of pool will be freed after this call.
 * @param heap Cannot be NULL.
 * @param pool Cannot be NULL, must be of heap.
 * @return Zero on failure, current transfer is lost.
 *
 * Not thread-safe with respect to the heap or pool!
 */
bool _gfx_flush_transfer(GFXHeap* heap, _GFXTransferPool* pool);


/****************************
 * Pipeline creation & warmup.
 ****************************/

/**
 * Retrieves a graphics pipeline from the renderer's cache (or warms it up).
 * Essentially a wrapper for _gfx_cache_(get|warmup).
 * @param renderable Cannot be NULL.
 * @param elem       Output cache element, cannot be NULL if warmup is zero.
 * @param warmup     Non-zero to only warmup and not retrieve.
 * @return Zero on failure.
 *
 * Completely thread-safe with respect to the renderable!
 */
bool _gfx_renderable_pipeline(GFXRenderable* renderable,
                              _GFXCacheElem** elem, bool warmup);

/**
 * Retrieves a compute pipeline from the renderer's cache (or warms it up).
 * Essentially a wrapper for _gfx_cache_(get|warmup).
 * @param computable Cannot be NULL.
 * @see _gfx_renderable_pipeline.
 *
 * Completely thread-safe with respect to the computable!
 */
bool _gfx_computable_pipeline(GFXComputable* computable,
                              _GFXCacheElem** elem, bool warmup);


/****************************
 * Virtual 'render' frame.
 ****************************/

/**
 * Initializes a virtual frame of a renderer.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @param index    Index of the virtual frame.
 * @return Zero on failure.
 */
bool _gfx_frame_init(GFXRenderer* renderer, GFXFrame* frame, unsigned int index);

/**
 * Clears a virtual frame of a renderer.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 *
 * This will block until the frame is done rendering!
 */
void _gfx_frame_clear(GFXRenderer* renderer, GFXFrame* frame);

/**
 * Retrieves the swapchain image index associated with an attachment.
 * @param frame Cannot be NULL.
 * @param index Attachment index.
 * @return The swapchain image index, or UINT32_MAX if none associated.
 */
uint32_t _gfx_frame_get_swapchain_index(GFXFrame* frame, size_t index);

/**
 * Blocks until all pending submissions of a virtual frame are done
 * and subsequently resets all command pools.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @param reset    Non-zero to also reset command pools.
 * @return Non-zero if successfully synchronized.
 *
 * If resetting, this cannot be called again until a call
 * to _gfx_frame_submit has been made.
 * Failure is considered fatal, frame cannot be used.
 */
bool _gfx_frame_sync(GFXRenderer* renderer, GFXFrame* frame, bool reset);

/**
 * Acquires all relevant resources for a virtual frame to be recorded.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @return Zero if the frame (or renderer) could not be built.
 *
 * This may call _gfx_sync_frames internally on-swapchain recreate!
 * Cannot be called again until a call to _gfx_frame_submit has been made.
 * Failure is considered fatal, swapchains could be left in an incomplete state.
 */
bool _gfx_frame_acquire(GFXRenderer* renderer, GFXFrame* frame);

/**
 * Records & submits a virtual frame.
 * Must be called exactly once for each call to _gfx_frame_acquire.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @return Zero if the frame could not be submitted.
 *
 * Failure is considered fatal, swapchains could be left in an incomplete state.
 */
bool _gfx_frame_submit(GFXRenderer* renderer, GFXFrame* frame);


/****************************
 * Render- backing and graph.
 ****************************/

/**
 * Initializes the render backing of a renderer.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_backing_init(GFXRenderer* renderer);

/**
 * Clears the render backing of a renderer, destroying all images.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_backing_clear(GFXRenderer* renderer);

/**
 * Builds not yet built resources of the render backing.
 * Will resolve to a no-op if everything is already built.
 * @param renderer Cannot be NULL.
 * @return Non-zero if the entire backing is in a built state.
 */
bool _gfx_render_backing_build(GFXRenderer* renderer);

/**
 * (Re)builds all relevant render backing resources.
 * Suitable for on-swapchain recreate (e.g. a window resize or smth).
 * @param renderer Cannot be NULL.
 * @param flags    Must contain the _GFX_RECREATE bit.
 */
void _gfx_render_backing_rebuild(GFXRenderer* renderer, _GFXRecreateFlags flags);

/**
 * Purges all relevant render backing resources.
 * Use to destroy stale backings to be purged at the current frame index.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_backing_purge(GFXRenderer* renderer);

/**
 * Initializes the render graph of a renderer.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_graph_init(GFXRenderer* renderer);

/**
 * Clears the render graph of a renderer, destroying all passes.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_graph_clear(GFXRenderer* renderer);

/**
 * Builds the Vulkan render passes if not present yet.
 * Can be used for potential pipeline warmups.
 * @param renderer Cannot be NULL.
 * @return Non-zero on success.
 *
 * This will call the relevant _gfx_pass_(destruct|warmup) calls.
 * Thus not thread-safe with respect to pushing stale resources!
 */
bool _gfx_render_graph_warmup(GFXRenderer* renderer);

/**
 * (Re)builds the render graph and all its resources.
 * Will resolve to a no-op if everything is already built.
 * @param renderer Cannot be NULL.
 * @return Non-zero if the entire graph is in a built state.
 *
 * This will call the relevant _gfx_pass_(destruct|build) calls.
 * Thus not thread-safe with respect to pushing stale resources!
 */
bool _gfx_render_graph_build(GFXRenderer* renderer);

/**
 * Rebuilds all relevant render graph resources.
 * Suitable for on-swapchain recreate (e.g. a window resize or smth).
 * @param renderer Cannot be NULL.
 * @param flags    Must contain the _GFX_RECREATE bit.
 *
 * This will call the relevant _gfx_pass_rebuild calls.
 * Thus not thread-safe with respect to pushing stale resources!
 */
void _gfx_render_graph_rebuild(GFXRenderer* renderer, _GFXRecreateFlags flags);

/**
 * Immediately destruct all render graph resources.
 * @param renderer Cannot be NULL.
 *
 * Must be called before detaching any attachment!
 * This will call the relevant _gfx_pass_destruct calls.
 * Thus not thread-safe with respect to pushing stale resources!
 */
void _gfx_render_graph_destruct(GFXRenderer* renderer);

/**
 * Invalidates the render graph, forcing it to first destruct everything
 * the next time _gfx_render_graph_(warmup|build) is called.
 * If _gfx_render_graph_rebuild is called before that, it is rendered a no-op.
 * Suitable for consumptions have changed or new attachments are described.
 * @param renderer Cannot be NULL.
 */
void _gfx_render_graph_invalidate(GFXRenderer* renderer);


/****************************
 * Pass (nodes in the render graph).
 ****************************/

/**
 * Creates a pass, referencing all parents.
 * Each element in parents must be associated with the same renderer.
 * @param renderer   Cannot be NULL.
 * @param group      The cull group this pass is in.
 * @param numParents Number of parents, 0 for none.
 * @param parents    Parent passes, cannot be NULL if numParents > 0.
 * @return NULL on failure.
 */
GFXPass* _gfx_create_pass(GFXRenderer* renderer, GFXPassType type,
                          unsigned int group,
                          size_t numParents, GFXPass** parents);

/**
 * Destroys a pass, unreferencing all parents.
 * @param pass Cannot be NULL.
 */
void _gfx_destroy_pass(GFXPass* pass);

/**
 * Retrieves the current framebuffer of a pass with respect to a frame.
 * @param rPass Cannot be NULL, must not be culled.
 * @param frame Cannot be NULL.
 * @return VK_NULL_HANDLE if unknown.
 */
VkFramebuffer _gfx_pass_framebuffer(_GFXRenderPass* rPass, GFXFrame* frame);

/**
 * Builds the Vulkan render pass if not present yet.
 * Can be used for potential pipeline warmups.
 * @param rPass Cannot be NULL.
 * @return Non-zero on success.
 *
 * Before the initial call to _gfx_pass_(warmup|build) and once after a call
 * to _gfx_pass_destruct, the following MUST be set to influence the build:
 *  rPass->out.*
 *  rPass->base.culled
 *  rPass->base.consumes[*]->out.*
 */
bool _gfx_pass_warmup(_GFXRenderPass* rPass);

/**
 * Builds the Vulkan framebuffer (and others) if not present yet.
 * @param rPass Cannot be NULL.
 * @return Non-zero if completely valid and built.
 *
 * @see _gfx_pass_warmup for influencing the build.
 */
bool _gfx_pass_build(_GFXRenderPass* rPass);

/**
 * Rebuilds Vulkan objects, does NOT build not yet built objects!
 * @param rPass Cannot be NULL.
 * @param flags Must contain the _GFX_RECREATE bit.
 * @return Non-zero if rebuilt successfully.
 *
 * Not thread-safe with respect to pushing stale resources!
 */
bool _gfx_pass_rebuild(_GFXRenderPass* rPass, _GFXRecreateFlags flags);

/**
 * Destructs all Vulkan objects, non-recursively.
 * @param rPass Cannot be NULL.
 *
 * Must be called before its attachments and after its consumptions are changed!
 * Not thread-safe with respect to pushing stale resources!
 */
void _gfx_pass_destruct(_GFXRenderPass* rPass);


/****************************
 * Renderer, recorder, technique and set.
 ****************************/

/**
 * Retrieves a sampler from the renderer's cache (wrapper for _gfx_cache_get).
 * @param renderer Cannot be NULL.
 * @param sampler  Sampler values to use, NULL for the default sampler.
 * @return NULL on failure.
 */
_GFXCacheElem* _gfx_get_sampler(GFXRenderer* renderer,
                                const GFXSampler* sampler);

/**
 * Pushes stale resources to the renderer, subsequently
 * destroying it the next time the previous frame is acquired again.
 * @param renderer Cannot be NULL.
 * @return Non-zero if successfully pushed.
 *
 * Not reentrant nor thread-safe with respect to any call to
 * either gfx_renderer_acquire or gfx_frame_submit.
 *
 * Any Vulkan resource handle may be VK_NULL_HANDLE, as long as one is not.
 * All handles are invalidated after this call.
 * Failure is considered fatal, resources are prematurely destroyed.
 */
bool _gfx_push_stale(GFXRenderer* renderer,
                     VkFramebuffer framebuffer,
                     VkImageView imageView,
                     VkBufferView bufferView,
                     VkCommandPool commandPool);

/**
 * Blocks until all frames in a renderer's render frame are done.
 * @param renderer Cannot be NULL.
 * @return Non-zero if successfully synchronized.
 *
 * Does not block for the publicly accessible frame!
 * Not reentrant nor thread-safe with respect to any frame.
 * Failure is considered fatal, frames cannot be used.
 */
bool _gfx_sync_frames(GFXRenderer* renderer);

/**
 * Resets a recording pool, i.e. resets all command buffers
 * and sets the new current recording pool(s) to use for recording commands.
 * @param recorder Cannot be NULL.
 * @return Non-zero if successfully reset.
 */
bool _gfx_recorder_reset(GFXRecorder* recorder);

/**
 * Records the recording output of a recorder into a given command buffer.
 * The command buffer must be in the recording state (!).
 * @param recorder Cannot be NULL.
 * @param order    Buffers that were output with this order will be recorded.
 * @param cmd      Cannot be NULL, must be in the render pass of `order` (!).
 */
void _gfx_recorder_record(GFXRecorder* recorder,
                          unsigned int order, VkCommandBuffer cmd);

/**
 * Retrieves all Vulkan specialization constant info and map entries.
 * @param technique Cannot be NULL, must be locked.
 * @param infos     `_GFX_NUM_SHADER_STAGES` VkSpecilizationInfo structs.
 * @param entries   `technique->constants.size` VkSpecializationMapEntry structs.
 *
 * All output entries are sorted on { stage, constantID }.
 */
void _gfx_tech_get_constants(GFXTechnique* technique,
                             VkSpecializationInfo* infos,
                             VkSpecializationMapEntry* entries);

/**
 * Computes the size of a specific descriptor set layout within a technique.
 * @param technique   Cannot be NULL, must be locked.
 * @param set         Must be < technique->numSets.
 * @param numBindings Outputs the number of _GFXSetBinding's necessary.
 * @param numEntries  Outputs the number of _GFXSetEntry's necessary.
 */
void _gfx_tech_get_set_size(GFXTechnique* technique,
                            size_t set, size_t* numBindings, size_t* numEntries);

/**
 * Retrieves a descriptor set binding from a technique and populates the
 * `type`, `viewType`, `count` and `size` fields of a _GFXSetBinding struct.
 * @param technique Cannot be NULL, must be locked.
 * @param set       Must be < technique->numSets.
 * @param binding   Descriptor binding number.
 * @param out       Output _GFXSetBinding to populate.
 * @return Zero if this binding DOES NOT occupy any _GFXSetEntry's!
 */
bool _gfx_tech_get_set_binding(GFXTechnique* technique,
                               size_t set, size_t binding, _GFXSetBinding* out);

/**
 * Retrieves, allocates or recycles a Vulkan descriptor set of the given set.
 * @param set Cannot be NULL.
 * @param sub Cannot be NULL, must be of the same renderer as set.
 * @return NULL on failure.
 *
 * Thread-safe with respect to the set and other subordinates,
 * essentially a wrapper for _gfx_pool_get.
 * However, can never run concurrently with other set functions.
 */
_GFXPoolElem* _gfx_set_get(GFXSet* set, _GFXPoolSub* sub);


#endif
