/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_OBJECTS_H_
#define GFX_CORE_OBJECTS_H_

#include "groufix/containers/deque.h"
#include "groufix/containers/list.h"
#include "groufix/containers/vec.h"
#include "groufix/core/mem.h"
#include "groufix/core.h"


#define GFX_GET_VK_BUFFER_USAGE_(flags, usage) \
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

#define GFX_GET_VK_IMAGE_TYPE_(type) \
	((type) == GFX_IMAGE_1D ? VK_IMAGE_TYPE_1D : \
	(type) == GFX_IMAGE_2D ? VK_IMAGE_TYPE_2D : \
	(type) == GFX_IMAGE_3D ? VK_IMAGE_TYPE_3D : \
	(type) == GFX_IMAGE_3D_SLICED ? VK_IMAGE_TYPE_3D : \
	(type) == GFX_IMAGE_CUBE ? VK_IMAGE_TYPE_2D : \
	VK_IMAGE_TYPE_2D)

#define GFX_GET_VK_IMAGE_VIEW_TYPE_(type) \
	((type) == GFX_VIEW_1D ? VK_IMAGE_VIEW_TYPE_1D : \
	(type) == GFX_VIEW_1D_ARRAY ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : \
	(type) == GFX_VIEW_2D ? VK_IMAGE_VIEW_TYPE_2D : \
	(type) == GFX_VIEW_2D_ARRAY ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : \
	(type) == GFX_VIEW_CUBE ? VK_IMAGE_VIEW_TYPE_CUBE : \
	(type) == GFX_VIEW_CUBE_ARRAY ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : \
	(type) == GFX_VIEW_3D ? VK_IMAGE_VIEW_TYPE_3D : \
	VK_IMAGE_VIEW_TYPE_2D)

#define GFX_GET_VK_IMAGE_ASPECT_(aspect) \
	(((aspect) & GFX_IMAGE_COLOR ? \
		VK_IMAGE_ASPECT_COLOR_BIT : (VkImageAspectFlags)0) | \
	((aspect) & GFX_IMAGE_DEPTH ? \
		VK_IMAGE_ASPECT_DEPTH_BIT : (VkImageAspectFlags)0) | \
	((aspect) & GFX_IMAGE_STENCIL ? \
		VK_IMAGE_ASPECT_STENCIL_BIT : (VkImageAspectFlags)0))

#define GFX_GET_VK_IMAGE_USAGE_(flags, usage, fmt) \
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

#define GFX_GET_VK_COMPONENT_SWIZZLE_(swizzle) \
	((swizzle) == GFX_SWIZZLE_ZERO ? VK_COMPONENT_SWIZZLE_ZERO : \
	(swizzle) == GFX_SWIZZLE_ONE ? VK_COMPONENT_SWIZZLE_ONE : \
	(swizzle) == GFX_SWIZZLE_R ? VK_COMPONENT_SWIZZLE_R : \
	(swizzle) == GFX_SWIZZLE_G ? VK_COMPONENT_SWIZZLE_G : \
	(swizzle) == GFX_SWIZZLE_B ? VK_COMPONENT_SWIZZLE_B : \
	(swizzle) == GFX_SWIZZLE_A ? VK_COMPONENT_SWIZZLE_A : \
	VK_COMPONENT_SWIZZLE_IDENTITY)

#define GFX_GET_VK_SAMPLE_COUNT_(count) \
	/* Vulkan sample counts are the hexadecimal equivalent. */ \
	((count) == 1 ? VK_SAMPLE_COUNT_1_BIT : \
	(count) == 2 ? VK_SAMPLE_COUNT_2_BIT : \
	(count) == 4 ? VK_SAMPLE_COUNT_4_BIT : \
	(count) == 8 ? VK_SAMPLE_COUNT_8_BIT : \
	(count) == 16 ? VK_SAMPLE_COUNT_16_BIT : \
	(count) == 32 ? VK_SAMPLE_COUNT_32_BIT : \
	(count) == 64 ? VK_SAMPLE_COUNT_64_BIT : \
	VK_SAMPLE_COUNT_1_BIT)

#define GFX_GET_VK_FORMAT_FEATURES_(flags, usage, fmt) \
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

#define GFX_GET_VK_PRIMITIVE_TOPOLOGY_(topo) \
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

#define GFX_GET_VK_CULL_MODE_(cull) \
	(((cull) == GFX_CULL_FRONT ? \
		VK_CULL_MODE_FRONT_BIT : (VkCullModeFlags)0) | \
	((cull) == GFX_CULL_BACK ? \
		VK_CULL_MODE_BACK_BIT : (VkCullModeFlags)0))

#define GFX_GET_VK_POLYGON_MODE_(mode) \
	((mode) == GFX_RASTER_POINT ? VK_POLYGON_MODE_POINT : \
	(mode) == GFX_RASTER_LINE ? VK_POLYGON_MODE_LINE : \
	(mode) == GFX_RASTER_FILL ? VK_POLYGON_MODE_FILL : \
	VK_POLYGON_MODE_FILL)

#define GFX_GET_VK_FRONT_FACE_(front) \
	((front) == GFX_FRONT_FACE_CCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : \
	(front) == GFX_FRONT_FACE_CW ? VK_FRONT_FACE_CLOCKWISE : \
	VK_FRONT_FACE_CLOCKWISE)

#define GFX_GET_VK_FILTER_(filter) \
	((filter) == GFX_FILTER_NEAREST ? VK_FILTER_NEAREST : \
	(filter) == GFX_FILTER_LINEAR ? VK_FILTER_LINEAR : \
	VK_FILTER_NEAREST)

#define GFX_GET_VK_MIPMAP_MODE_(filter) \
	((filter) == GFX_FILTER_NEAREST ? VK_SAMPLER_MIPMAP_MODE_NEAREST : \
	(filter) == GFX_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : \
	VK_SAMPLER_MIPMAP_MODE_NEAREST)

#define GFX_GET_VK_REDUCTION_MODE_(mode) \
	((mode) == GFX_FILTER_MODE_AVERAGE ? \
		VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE : \
	(mode) == GFX_FILTER_MODE_MIN ? \
		VK_SAMPLER_REDUCTION_MODE_MIN : \
	(mode) == GFX_FILTER_MODE_MAX ? \
		VK_SAMPLER_REDUCTION_MODE_MAX : \
		VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE)

#define GFX_GET_VK_ADDRESS_MODE_(wrap) \
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

#define GFX_GET_VK_LOGIC_OP_(op) \
	((op) == GFX_LOGIC_NO_OP ? VK_LOGIC_OP_COPY : \
	(op) == GFX_LOGIC_CLEAR ? VK_LOGIC_OP_CLEAR : \
	(op) == GFX_LOGIC_SET ? VK_LOGIC_OP_SET : \
	(op) == GFX_LOGIC_KEEP ? VK_LOGIC_OP_NO_OP : \
	(op) == GFX_LOGIC_KEEP_INVERSE ? VK_LOGIC_OP_INVERT : \
	(op) == GFX_LOGIC_INVERSE ? VK_LOGIC_OP_COPY_INVERTED : \
	(op) == GFX_LOGIC_AND ? VK_LOGIC_OP_AND : \
	(op) == GFX_LOGIC_AND_INVERSE ? VK_LOGIC_OP_AND_INVERTED : \
	(op) == GFX_LOGIC_AND_REVERSE ? VK_LOGIC_OP_AND_REVERSE : \
	(op) == GFX_LOGIC_NAND ? VK_LOGIC_OP_NAND : \
	(op) == GFX_LOGIC_OR ? VK_LOGIC_OP_OR : \
	(op) == GFX_LOGIC_OR_INVERSE ? VK_LOGIC_OP_OR_INVERTED : \
	(op) == GFX_LOGIC_OR_REVERSE ? VK_LOGIC_OP_OR_REVERSE : \
	(op) == GFX_LOGIC_XOR ? VK_LOGIC_OP_XOR : \
	(op) == GFX_LOGIC_NOR ? VK_LOGIC_OP_NOR : \
	(op) == GFX_LOGIC_EQUAL ? VK_LOGIC_OP_EQUIVALENT : \
	VK_LOGIC_OP_COPY)

#define GFX_GET_VK_BLEND_OP_(op) \
	((op) == GFX_BLEND_NO_OP ? VK_BLEND_OP_ADD : \
	(op) == GFX_BLEND_ADD ? VK_BLEND_OP_ADD : \
	(op) == GFX_BLEND_SUBTRACT ? VK_BLEND_OP_SUBTRACT : \
	(op) == GFX_BLEND_SUBTRACT_REVERSE ? VK_BLEND_OP_REVERSE_SUBTRACT : \
	(op) == GFX_BLEND_MIN ? VK_BLEND_OP_MIN : \
	(op) == GFX_BLEND_MAX ? VK_BLEND_OP_MAX : \
	VK_BLEND_OP_ADD)

#define GFX_GET_VK_BLEND_FACTOR_(factor) \
	((factor) == GFX_FACTOR_ZERO ? VK_BLEND_FACTOR_ZERO : \
	(factor) == GFX_FACTOR_ONE ? VK_BLEND_FACTOR_ONE : \
	(factor) == GFX_FACTOR_SRC ? VK_BLEND_FACTOR_SRC_COLOR : \
	(factor) == GFX_FACTOR_ONE_MINUS_SRC ? VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR : \
	(factor) == GFX_FACTOR_DST ? VK_BLEND_FACTOR_DST_COLOR : \
	(factor) == GFX_FACTOR_ONE_MINUS_DST ? VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR : \
	(factor) == GFX_FACTOR_SRC_ALPHA ? VK_BLEND_FACTOR_SRC_ALPHA : \
	(factor) == GFX_FACTOR_SRC_ALPHA_SATURATE ? VK_BLEND_FACTOR_SRC_ALPHA_SATURATE : \
	(factor) == GFX_FACTOR_ONE_MINUS_SRC_ALPHA ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : \
	(factor) == GFX_FACTOR_DST_ALPHA ? VK_BLEND_FACTOR_DST_ALPHA : \
	(factor) == GFX_FACTOR_ONE_MINUS_DST_ALPHA ? VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA : \
	(factor) == GFX_FACTOR_CONSTANT ? VK_BLEND_FACTOR_CONSTANT_COLOR : \
	(factor) == GFX_FACTOR_ONE_MINUS_CONSTANT ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR : \
	(factor) == GFX_FACTOR_CONSTANT_ALPHA ? VK_BLEND_FACTOR_CONSTANT_ALPHA : \
	(factor) == GFX_FACTOR_ONE_MINUS_CONSTANT_ALPHA ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA : \
	VK_BLEND_FACTOR_ZERO)

#define GFX_GET_VK_COMPARE_OP_(op) \
	((op) == GFX_CMP_NEVER ?  VK_COMPARE_OP_NEVER : \
	(op) == GFX_CMP_LESS ? VK_COMPARE_OP_LESS : \
	(op) == GFX_CMP_LESS_EQUAL ? VK_COMPARE_OP_LESS_OR_EQUAL : \
	(op) == GFX_CMP_GREATER ? VK_COMPARE_OP_GREATER : \
	(op) == GFX_CMP_GREATER_EQUAL ? VK_COMPARE_OP_GREATER_OR_EQUAL : \
	(op) == GFX_CMP_EQUAL ? VK_COMPARE_OP_EQUAL : \
	(op) == GFX_CMP_NOT_EQUAL ? VK_COMPARE_OP_NOT_EQUAL : \
	(op) == GFX_CMP_ALWAYS ? VK_COMPARE_OP_ALWAYS : \
	VK_COMPARE_OP_ALWAYS)

#define GFX_GET_VK_STENCIL_OP_(op) \
	((op) == GFX_STENCIL_KEEP ? VK_STENCIL_OP_KEEP : \
	(op) == GFX_STENCIL_ZERO ? VK_STENCIL_OP_ZERO : \
	(op) == GFX_STENCIL_REPLACE ? VK_STENCIL_OP_REPLACE : \
	(op) == GFX_STENCIL_INVERT ? VK_STENCIL_OP_INVERT : \
	(op) == GFX_STENCIL_INCR_CLAMP ? VK_STENCIL_OP_INCREMENT_AND_CLAMP : \
	(op) == GFX_STENCIL_INCR_WRAP ? VK_STENCIL_OP_INCREMENT_AND_WRAP : \
	(op) == GFX_STENCIL_DECR_CLAMP ? VK_STENCIL_OP_DECREMENT_AND_CLAMP : \
	(op) == GFX_STENCIL_DECR_WRAP ? VK_STENCIL_OP_DECREMENT_AND_WRAP : \
	VK_STENCIL_OP_KEEP)

#define GFX_GET_VK_SHADER_STAGE_(stage) \
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

#define GFX_GET_VK_ACCESS_FLAGS_(mask, fmt) \
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

#define GFX_GET_VK_PIPELINE_STAGE_(mask, stage, fmt) \
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

#define GFX_GET_VK_IMAGE_LAYOUT_(mask, fmt) \
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
#define GFX_MOD_VK_PIPELINE_STAGE_(vkStage, context) \
	((vkStage) & \
		~((!((context)->features & GFX_SUPPORT_GEOMETRY_SHADER_) ? \
			VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT : \
			(VkPipelineStageFlags)0) | \
		(!((context)->features & GFX_SUPPORT_TESSELLATION_SHADER_) ? \
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
#define GFX_GET_SHADER_STAGE_INDEX_(stage) \
	((stage) == GFX_STAGE_VERTEX ? 0 : \
	(stage) == GFX_STAGE_TESS_CONTROL ? 1 : \
	(stage) == GFX_STAGE_TESS_EVALUATION ? 2 : \
	(stage) == GFX_STAGE_GEOMETRY ? 3 : \
	(stage) == GFX_STAGE_FRAGMENT ? 4 : \
	(stage) == GFX_STAGE_COMPUTE ? 5 : \
	6) /* Should not happen. */

#define GFX_NUM_SHADER_STAGES_ 6


/**
 * Reflected shader resource.
 */
typedef struct GFXShaderResource_
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
		GFX_SHADER_VERTEX_INPUT_,
		GFX_SHADER_FRAGMENT_OUTPUT_,
		GFX_SHADER_BUFFER_UNIFORM_, // Can be dynamic.
		GFX_SHADER_BUFFER_STORAGE_, // Can be dynamic.
		GFX_SHADER_BUFFER_UNIFORM_TEXEL_,
		GFX_SHADER_BUFFER_STORAGE_TEXEL_,
		GFX_SHADER_IMAGE_AND_SAMPLER_,
		GFX_SHADER_IMAGE_SAMPLED_,
		GFX_SHADER_IMAGE_STORAGE_,
		GFX_SHADER_SAMPLER_,
		GFX_SHADER_ATTACHMENT_INPUT_,
		GFX_SHADER_CONSTANT_

	} type;

} GFXShaderResource_;


/**
 * Internal shader.
 */
struct GFXShader
{
	GFXDevice_*  device; // Associated GPU to use as target environment.
	GFXContext_* context;
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
		GFXShaderResource_* resources;

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
typedef struct GFXStaging_
{
	GFXListNode  list;  // Base-type.
	GFXMemAlloc_ alloc; // Stores the size.


	// Vulkan fields.
	struct
	{
		VkBuffer buffer;
		void*    ptr;

	} vk;

} GFXStaging_;


/**
 * Transfer operation(s).
 */
typedef struct GFXTransfer_
{
	GFXList stagings; // References GFXStaging_, automatically freed.
	bool    flushed;


	// Vulkan fields.
	struct
	{
		VkCommandBuffer cmd;
		VkFence         done; // Mostly for polling.

	} vk;

} GFXTransfer_;


/**
 * Transfer operation pool.
 */
typedef struct GFXTransferPool_
{
	GFXDeque  transfers; // Stores GFXTransfer_.
	GFXVec    injs;      // Stores GFXInject.
	GFXQueue_ queue;
	GFXMutex_ lock;

	struct GFXInjection_* injection;

	// #blocking threads.
	atomic_uintmax_t blocking;


	// Vulkan fields.
	struct
	{
		VkCommandPool pool;

	} vk;

} GFXTransferPool_;


/**
 * Internal heap.
 */
struct GFXHeap
{
	GFXAllocator_ allocator; // Has both GFXDevice_* and GFXContext_*.
	GFXMutex_     lock;      // For allocation.

	GFXList buffers;    // References GFXBuffer_.
	GFXList images;     // References GFXImage_.
	GFXList primitives; // References GFXPrimitive_.
	GFXList groups;     // References GFXGroup_.


	// Operation resources,
	//  for both the graphics and transfer queues.
	struct
	{
		GFXTransferPool_ graphics;
		GFXTransferPool_ transfer;
		uint32_t         compute; // Family index only.

	} ops;
};


/**
 * Internal buffer.
 */
typedef struct GFXBuffer_
{
	GFXBuffer   base;
	GFXHeap*    heap;
	GFXListNode list;

	GFXMemAlloc_ alloc;


	// Vulkan fields.
	struct
	{
		VkBuffer buffer;

	} vk;

} GFXBuffer_;


/**
 * Internal image.
 */
typedef struct GFXImage_
{
	GFXImage    base;
	GFXHeap*    heap;
	GFXListNode list;

	GFXMemAlloc_ alloc;


	// Vulkan fields.
	struct
	{
		VkFormat format;
		VkImage  image;

	} vk;

} GFXImage_;


/**
 * Primitive buffer (i.e. Vulkan vertex input binding).
 */
typedef struct GFXPrimBuffer_
{
	GFXBuffer_* buffer;
	uint64_t    offset; // Offset to bind at.
	uint32_t    stride;
	uint64_t    size; // Total size (including the last attribute) in bytes.

	VkVertexInputRate rate;

} GFXPrimBuffer_;


/**
 * Internal vertex attribute.
 */
typedef struct GFXAttribute_
{
	GFXAttribute base;
	uint32_t     binding; // Vulkan input binding.


	// Vulkan fields.
	struct
	{
		VkFormat format;

	} vk;

} GFXAttribute_;


/**
 * Internal primitive geometry (superset of buffer).
 */
typedef struct GFXPrimitive_
{
	GFXPrimitive base;
	GFXBuffer_   buffer; // vk.buffer is VK_NULL_HANDLE if nothing is allocated.
	GFXBufferRef index;  // May be GFX_REF_NULL.

	size_t          numBindings;
	GFXPrimBuffer_* bindings; // Vulkan input bindings.

	size_t        numAttribs;
	GFXAttribute_ attribs[]; // No reference is GFX_REF_NULL.

} GFXPrimitive_;


/**
 * Internal group binding.
 */
typedef struct GFXBinding_
{
	GFXBinding base;
	uint64_t   stride; // Element stride in bytes.

} GFXBinding_;


/**
 * Internal resource group (superset of buffer).
 */
typedef struct GFXGroup_
{
	GFXGroup   base;
	GFXBuffer_ buffer; // vk.buffer is VK_NULL_HANDLE if nothing is allocated.

	size_t      numBindings;
	GFXBinding_ bindings[]; // No reference is GFX_REF_NULL.

} GFXGroup_;


/****************************
 * Rendering objects.
 ****************************/

/**
 * Retrieve build generation from a GFXImageAttach_ or GFXRenderPass_ pointer.
 */
#define GFX_ATTACH_GEN_(attach) \
	(((GFXAttach_*)((char*)(attach) - offsetof(GFXAttach_, image)))->gen)

#define GFX_PASS_GEN_(pass) \
	(((GFXRenderPass_*)(pass))->gen)


/**
 * Attachment backing.
 */
typedef struct GFXBacking_
{
	GFXListNode  list; // Base-type.
	GFXMemAlloc_ alloc;

	unsigned int purge; // If stale, index of frame to purge at.


	// Vulkan fields.
	struct
	{
		VkImage image;

	} vk;

} GFXBacking_;


/**
 * Image (implicit) attachment.
 */
typedef struct GFXImageAttach_
{
	GFXAttachment base;
	GFXList       backings; // References GFXBacking_.

	// Resolved size.
	uint32_t width;
	uint32_t height;
	uint32_t depth;

	// Set by dependency injections, signaled out of the renderer.
	bool signaled;


	// Vulkan fields.
	struct
	{
		VkFormat format;
		VkImage  image; // Most recent (for locality).

	} vk;

} GFXImageAttach_;


/**
 * Window attachment.
 */
typedef struct GFXWindowAttach_
{
	GFXWindow_*       window;
	GFXRecreateFlags_ flags; // Used by virtual frames, from last submission.

	// Inherits all resources from window.

} GFXWindowAttach_;


/**
 * Internal attachment.
 */
typedef struct GFXAttach_
{
	// Build generation (to update set entries), persistent, never 0!
	uint32_t gen;


	// Attachment type.
	enum
	{
		GFX_ATTACH_EMPTY_,
		GFX_ATTACH_IMAGE_,
		GFX_ATTACH_WINDOW_

	} type;


	// Attachment data.
	union
	{
		GFXImageAttach_ image;
		GFXWindowAttach_ window;
	};

} GFXAttach_;


/**
 * Recording command pool.
 */
typedef struct GFXRecorderPool_
{
	size_t used; // #used buffers in cmds.


	// Vulkan fields.
	struct
	{
		VkCommandPool pool;
		GFXVec        cmds; // Stores VkCommandBuffer.

	} vk;

} GFXRecorderPool_;


/**
 * Internal recorder.
 */
struct GFXRecorder
{
	GFXListNode  list; // Base-type.
	GFXRenderer* renderer;
	GFXContext_* context; // For locality.
	GFXPoolSub_  sub;     // For descriptor access.


	// Recording input.
	struct
	{
		GFXPass*        pass;
		VkCommandBuffer cmd;

	} inp;


	// Current state.
	struct
	{
		GFXViewport viewport;
		GFXScissor  scissor;
		float       lineWidth;

		GFXCacheElem_* pipeline;
		GFXPrimitive_* primitive;
		uint32_t       pushSize;
		GFXShaderStage pushStages;

		GFXVec sets;    // Stores { GFXCacheElem_*, GFXPoolElem_*, size_t }.
		GFXVec offsets; // Stores uint32_t.

	} state;


	// Recording output.
	struct
	{
		GFXVec cmds; // Stores { unsigned int, VkCommandBuffer } (sorted).

	} out;


	unsigned int     current; // Current virtual frame index.
	GFXRecorderPool_ pools[]; // Two { graphics, compute } for each virtual frame.
};


/**
 * Frame synchronization (swapchain acquisition) object.
 */
typedef struct GFXFrameSync_
{
	GFXWindow_* window;
	size_t      backing; // Attachment index.
	uint32_t    image;   // Swapchain image index (or UINT32_MAX).


	// Vulkan fields.
	struct
	{
		VkSemaphore available;

	} vk;

} GFXFrameSync_;


/**
 * Frame recording pool.
 */
typedef struct GFXFramePool_
{
	// Vulkan fields.
	struct
	{
		VkCommandPool   pool;
		VkCommandBuffer cmd;
		VkFence         done;

	} vk;

} GFXFramePool_;


/**
 * Internal virtual frame.
 */
struct GFXFrame
{
	unsigned int index;

	GFXVec refs;  // Stores size_t, for each attachment; index into syncs (or SIZE_MAX).
	GFXVec syncs; // Stores GFXFrameSync_, one for each window attachment.

	GFXFramePool_ graphics;
	GFXFramePool_ compute;

	enum {
		GFX_FRAME_GRAPHICS_ = 0x0001,
		GFX_FRAME_COMPUTE_  = 0x0002

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
	GFXHeap*  heap;  // Has both GFXDevice_* and GFXContext_*.
	GFXCache_ cache; // Has GFXContext_*.
	GFXPool_  pool;  // Has GFXContext_*.
	GFXQueue_ graphics;
	GFXQueue_ present;
	GFXQueue_ compute;

	GFXList   recorders;  // References GFXRecorder.
	GFXList   techniques; // References GFXTechnique.
	GFXList   sets;       // References GFXSet.
	GFXMutex_ lock;       // For recorders, techniques, sets & the pool.

	// Current virtual frame state.
	bool recording;

	GFXFrame* public; // Public frame, if not NULL, user has access.
	GFXDeque  stales; // Stores { unsigned int, (Vk*)+ }.
	GFXMutex_ staleLock;

	// Lock used for gfx_renderable_warmup reentrancy +
	// updating set attachment references during recording.
	GFXMutex_ reentrantLock;


	// Render backing (i.e. attachments).
	struct
	{
		GFXVec attachs; // Stores GFXAttach_.

		enum {
			GFX_BACKING_INVALID_,
			GFX_BACKING_VALIDATED_,
			GFX_BACKING_BUILT_

		} state;

	} backing;


	// Render graph (directed acyclic graph of passes).
	struct
	{
		GFXList  passes;       // References GFXPass (in submission order).
		GFXPass* firstCompute; // First async compute pass.

		size_t numRender;  // Number of render & inline compute passes.
		size_t numCompute; // Number of async compute passes.
		size_t culledRender;
		size_t culledCompute;

		enum {
			GFX_GRAPH_EMPTY_,
			GFX_GRAPH_INVALID_, // Needs to purge.
			GFX_GRAPH_VALIDATED_,
			GFX_GRAPH_WARMED_,
			GFX_GRAPH_BUILT_

		} state;


		// Graph output (relative to neighbouring passes).
		struct
		{
			// First 'master' (or to be built/recorded) pass.
			// This chain is guaranteed to contain firstCompute.
			GFXPass* firstMaster;

		} out;

	} graph;


	// Render frame (i.e. collection of virtual frames).
	unsigned int numFrames;
	unsigned int current; // Next frame to submit.
	GFXFrame     frames[];
};


/**
 * Internal attachment consumption.
 */
typedef struct GFXConsume_
{
	GFXAccessMask  mask;
	GFXShaderStage stage;
	GFXView        view; // index used as attachment index.

	GFXImageAspect  cleared;
	GFXBlendOpState color;
	GFXBlendOpState alpha;
	size_t          resolve; // Or SIZE_MAX.

	enum {
		GFX_CONSUME_VIEWED_ = 0x0001, // Set to use view.type.
		GFX_CONSUME_BLEND_  = 0x0002  // Set to use blend operation states.

	} flags;

	union {
		// Identical definitions!
		GFXClear gfx;
		VkClearValue vk;

	} clear;


	// Graph output (relative to neighbouring passes).
	struct
	{
		uint32_t      subpass; // Subpass index.
		VkImageLayout initial;
		VkImageLayout final;

		enum {
			GFX_CONSUME_IS_FIRST_ = 0x0001,
			GFX_CONSUME_IS_LAST_  = 0x0002

		} state; // Subpass chain state.


		// Non-NULL to form a dependency.
		const struct GFXConsume_* prev;

		// Non-NULL regardless of dependencies.
		const struct GFXConsume_* next;

	} out;

} GFXConsume_;


/**
 * Internal pass-dependency injection.
 */
typedef struct GFXDepend_
{
	GFXInject inj;
	GFXPass*  source;
	GFXPass*  target;

	unsigned int waits; // #times this wait command is signaled.


	// Graph output (relative to neighbouring passes).
	struct
	{
		bool subpass;    // Is a subpass dependency (i.e. same subpass chain).
		bool transition; // Is a layout transition.

		GFXFormat fmt;

	} out;

} GFXDepend_;


/**
 * Internal pass (i.e. render/compute pass).
 */
struct GFXPass
{
	GFXListNode  list; // Base-type.
	GFXPassType  type;
	GFXRenderer* renderer;

	GFXVec       parents; // Stores GFXPass*.
	unsigned int level;   // Determines submission order.
	unsigned int order;   // Actual submission order.
	unsigned int childs;  // Number of unculled (!) passes this is a parent of.
	bool         culled;

	// Stores GFXConsume_.
	GFXVec consumes;

	// Stores GFXDepend_, from pass depend.
	GFXVec deps;

	// Stores GFXInject, from pass inject.
	GFXVec injs;


	// Graph output (relative to neighbouring passes).
	struct
	{
		// Next 'master' (or to be built/recorded) pass.
		GFXPass* nextMaster;

	} out;
};


/**
 * Internal render pass.
 */
typedef struct GFXRenderPass_
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
			GFX_PASS_DEPTH_   = 0x0001,
			GFX_PASS_STENCIL_ = 0x0002

		} enabled; // Set on warmup.

	} state;


	// Graph output (relative to neighbouring passes).
	struct
	{
		struct GFXRenderPass_* master; // First subpass, NULL if this.
		struct GFXRenderPass_* next;   // Next subpass in the chain, NULL if last.

		uint32_t subpass;   // Subpass index.
		uint32_t subpasses; // Number of subpasses (undefined if not master).
		size_t   backing;   // Window attachment index (or SIZE_MAX).

	} out;


	// Building output.
	struct
	{
		uint32_t fWidth;
		uint32_t fHeight;
		uint32_t fLayers;

		GFXCacheElem_* pass; // Built on warmup.

	} build;


	// Vulkan fields.
	struct
	{
		VkRenderPass pass;   // For locality.
		GFXVec       clears; // Stores VkClearValue.
		GFXVec       blends; // Stores { GFXBlendOpState (x2), char }.
		GFXVec       views;  // Stores { GFXConsume_*, VkImageView }.
		GFXVec       frames; // Stores { VkImageView, VkFramebuffer }.

	} vk;

} GFXRenderPass_;


/**
 * Internal compute pass.
 */
typedef struct GFXComputePass_
{
	GFXPass base;

	// Nothing special to do for compute passes.

} GFXComputePass_;


/**
 * Technique set (i.e. descriptor layout info).
 */
typedef struct GFXTechniqueSet_
{
	GFXCacheElem_* setLayout;   // NULL until locked.
	size_t         numBindings; // Including empty, set before locked!

	// All undefined until locked.
	size_t numEntries;
	size_t numDynamics; // #dynamic buffer entries.

} GFXTechniqueSet_;


/**
 * Internal technique (i.e. shader pipeline layout).
 */
struct GFXTechnique
{
	GFXListNode  list; // Base-type.
	GFXRenderer* renderer;

	GFXShader*     shaders[GFX_NUM_SHADER_STAGES_]; // May contain NULL.
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
	GFXCacheElem_*   layout; // Pipeline layout, NULL until locked.
	GFXTechniqueSet_ sets[]; // Sorted, no gaps.
};


/**
 * Set update entry (i.e. descriptor info).
 */
typedef struct GFXSetEntry_
{
	GFXReference   ref; // GFX_REF_NULL if empty or sampler.
	GFXRange       range;
	GFXSwizzleMap  swizzle;
	GFXViewType    viewType; // For attachment inputs ONLY!.
	GFXCacheElem_* sampler;  // May be NULL.

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

} GFXSetEntry_;


/**
 * Set binding (i.e. descriptor binding info).
 */
typedef struct GFXSetBinding_
{
	VkDescriptorType type;     // Undefined if empty.
	GFXViewType      viewType; // Undefined if not a 'non-attachment image'.

	size_t        count;   // 0 = empty binding.
	size_t        size;    // 0 = not a struct, empty or unknown.
	GFXSetEntry_* entries; // NULL if empty or immutable samplers only.
	char*         hash;

} GFXSetBinding_;


/**
 * Internal set (i.e. descriptor set).
 */
struct GFXSet
{
	GFXListNode    list; // Base-type.
	GFXRenderer*   renderer;
	GFXCacheElem_* setLayout;
	GFXSetEntry_*  first;
	GFXHashKey_*   key;

	// If used since last modification.
	atomic_bool used;

	size_t numAttachs;  // #referenced attachments.
	size_t numDynamics; // #dynamic buffer entries.
	size_t numBindings;

	GFXSetBinding_ bindings[]; // Sorted, no gaps.
};


/****************************
 * Resource reference operations.
 ****************************/

/**
 * Unpacked memory resource reference.
 * Access is not thread-safe with respect to the referenced object (!).
 */
typedef struct GFXUnpackRef_
{
	// Unpacked reference value(s),
	//  buffer offset | attachment index | 0.
	uint64_t value;


	// Referenced object (all mutually exclusive).
	struct
	{
		GFXBuffer_*  buffer;
		GFXImage_*   image;
		GFXRenderer* renderer;

	} obj;

} GFXUnpackRef_;


/**
 * Check for equality of unpacked references & getters.
 * Only checks for resource equality, offsets are ignored.
 * Getters will resolve to NULL or 0 if none found.
 */
#define GFX_UNPACK_REF_IS_EQUAL_(refa, refb) \
	(((refa).obj.buffer != NULL && \
		(refa).obj.buffer == (refb).obj.buffer) || \
	((refa).obj.image != NULL && \
		(refa).obj.image == (refb).obj.image) || \
	((refa).obj.renderer != NULL && (refa).value == (refb).value && \
		(refa).obj.renderer == (refb).obj.renderer))

#define GFX_UNPACK_REF_CONTEXT_(ref) \
	((ref).obj.buffer != NULL ? \
		(ref).obj.buffer->heap->allocator.context : \
	(ref).obj.image != NULL ? \
		(ref).obj.image->heap->allocator.context : \
	(ref).obj.renderer != NULL ? \
		(ref).obj.renderer->cache.context : NULL)

#define GFX_UNPACK_REF_HEAP_(ref) \
	((ref).obj.buffer != NULL ? (ref).obj.buffer->heap : \
	(ref).obj.image != NULL ? (ref).obj.image->heap : \
	(ref).obj.renderer != NULL ? (ref).obj.renderer->heap : NULL)

#define GFX_UNPACK_REF_ATTACH_(ref) \
	((ref).obj.renderer == NULL ? NULL : \
		&((GFXAttach_*)gfx_vec_at( \
			&(ref).obj.renderer->backing.attachs, (ref).value))->image)

/**
 * Retrieves the memory flags associated with an unpacked reference.
 * Meant for the debug build, where we validate flags and usages.
 */
#if defined (NDEBUG)
	#define GFX_UNPACK_REF_FLAGS_(ref) \
		static_assert(0, "Use GFX_UNPACK_REF_FLAGS_ in debug mode only.")
#else
	#define GFX_UNPACK_REF_FLAGS_(ref) \
		((ref).obj.buffer != NULL ? \
			(ref).obj.buffer->base.flags : \
		(ref).obj.image != NULL ? \
			(ref).obj.image->base.flags : \
		(ref).obj.renderer != NULL ? \
			GFX_UNPACK_REF_ATTACH_(ref)->base.flags : 0)
#endif


/**
 * Calculates the remaining size of a buffer reference from its offset.
 * The size is dictated by the top-most object being referenced, not by the
 * underlying resource (e.g. the size claimed for a group buffer).
 * @return Zero if ref is not a buffer reference.
 */
uint64_t gfx_ref_size_(GFXReference ref);

/**
 * Resolves & validates a memory reference, meaning:
 * if it references a reference, it will recursively return that reference.
 * @return A user-land reference to the object actually holding the memory.
 *
 * Assumes no self-references exist!
 * Returns GFX_REF_NULL and warns when the reference is invalid.
 */
GFXReference gfx_ref_resolve_(GFXReference ref);

/**
 * Resolves & unpacks a memory resource reference, meaning:
 * if an object is composed of other memory objects internally, it will be
 * 'unpacked' into its elementary non-composed memory objects.
 *
 * Returns empty (all NULL's) and warns when the reference is invalid.
 * If in debug mode & out of bounds, it silently warns.
 */
GFXUnpackRef_ gfx_ref_unpack_(GFXReference ref);


/****************************
 * Dependency injection objects & operations.
 ****************************/

/**
 * Injection type checkers for a GFXInject.
 */
#define GFX_INJ_IS_SIGNAL_(inj) \
	((inj).type == GFX_INJ_SIGNAL || \
	(inj).type == GFX_INJ_SIGNAL_RANGE || \
	(inj).type == GFX_INJ_SIGNAL_FROM || \
	(inj).type == GFX_INJ_SIGNAL_RANGE_FROM)

#define GFX_INJ_IS_RANGED_(inj) \
	((inj).type == GFX_INJ_SIGNAL_RANGE || \
	(inj).type == GFX_INJ_SIGNAL_RANGE_FROM)

#define GFX_INJ_IS_SOURCED_(inj) \
	((inj).type == GFX_INJ_SIGNAL_FROM || \
	(inj).type == GFX_INJ_SIGNAL_RANGE_FROM)

#define GFX_INJ_IS_WAIT_(inj) \
	((inj).type == GFX_INJ_WAIT)


/**
 * Dependency injection metadata.
 */
typedef struct GFXInjection_
{
	// Operation input, must be pre-initialized!
	struct
	{
		GFXRenderer* renderer; // To signal attachments.
		size_t       numRefs;  // May be zero!

		const GFXUnpackRef_* refs;
		const GFXAccessMask* masks;
		const uint64_t*      sizes; // Must contain gfx_ref_size_(..)!

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
		size_t numMems;
		size_t numBufs;
		size_t numImgs;

		VkMemoryBarrier*       mems;
		VkBufferMemoryBarrier* bufs;
		VkImageMemoryBarrier*  imgs;

	} bars;


	// Synchronization output.
	struct
	{
		size_t numWaits;
		size_t numSigs;

		VkSemaphore* waits;
		VkSemaphore* sigs;

		// Wait stages, of the same size as waits.
		VkPipelineStageFlags* stages;

	} out;

} GFXInjection_;


/**
 * Dependency signal (metadata) object.
 */
typedef struct GFXSignal_
{
	GFXUnpackRef_ ref;
	GFXRange      range; // Unpacked, i.e. normalized offset & non-zero size.
	unsigned int  waits; // #wait commands left to recycle (if used).

	// For attachment references.
	uint32_t gen;

	// Claimed by (injections can be async), may be NULL.
	const GFXInjection_* inj;


	// Stage in the object's lifecycle.
	enum
	{
		GFX_SIGNAL_UNUSED_, // Only `flags` and `vk.signaled` are defined.
		GFX_SIGNAL_PREPARE_,
		GFX_SIGNAL_PREPARE_CATCH_, // Within the same injection.
		GFX_SIGNAL_PENDING_,
		GFX_SIGNAL_CATCH_,
		GFX_SIGNAL_USED_

	} stage;


	// Synchronization flags.
	enum
	{
		GFX_SIGNAL_SEMAPHORE_  = 0x0001, // If `vk.signaled` is used.
		GFX_SIGNAL_BARRIER_    = 0x0002, // Set to inject barrier on catch.
		GFX_SIGNAL_MEM_HAZARD_ = 0x0004  // Memory barrier required if set.

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

} GFXSignal_;


/**
 * Internal semaphore.
 */
struct GFXSemaphore
{
	GFXDevice_*  device;
	GFXContext_* context;

	unsigned int waitCapacity;

	size_t    sems; // #used vk.signaled semaphores at the front of `sigs`.
	GFXDeque  sigs; // Stores GFXSignal_.
	GFXMutex_ lock;

	// Vulkan family & queue indices.
	struct { uint32_t family, index; } graphics;
	struct { uint32_t family, index; } compute;
	struct { uint32_t family, index; } transfer;
};


/**
 * Starts a new dependency injection (initializes metadata).
 * The object pointed to by injection cannot be moved or copied!
 */
static inline void gfx_injection_(GFXInjection_* injection)
{
	injection->bars.srcStage = 0;
	injection->bars.dstStage = 0;
	injection->bars.numMems = 0;
	injection->bars.numBufs = 0;
	injection->bars.numImgs = 0;
	injection->bars.mems = NULL;
	injection->bars.bufs = NULL;
	injection->bars.imgs = NULL;

	injection->out.numWaits = 0;
	injection->out.numSigs = 0;
	injection->out.waits = NULL;
	injection->out.sigs = NULL;
	injection->out.stages = NULL;
}

/**
 * Flushes all stored barriers injected by gfx_injection_push_.
 * Automatically flushed by a successful call to gfx_sems_(catch|prepare)_.
 * @param context   Cannot be NULL.
 * @param cmd       To record all barriers to, cannot be VK_NULL_HANDLE.
 * @param injection Barrier metadata to flush, cannot be NULL.
 *
 * Must be called before gfx_sems_(abort|finish)_!
 */
void gfx_injection_flush_(GFXContext_* context, VkCommandBuffer cmd,
                          GFXInjection_* injection);

/**
 * Pushes an execution/memory barrier for the next gfx_injection_flush_.
 * Should be used internally to inject barriers without using GFXSemaphores.
 * @param injection Barrier metadata to append to, cannot be NULL.
 * @return Zero on failure.
 *
 * Can only set one of `mb`, `bmb` and `imb` to non-NULL!
 */
bool gfx_injection_push_(VkPipelineStageFlags srcStage,
                         VkPipelineStageFlags dstStage,
                         const VkMemoryBarrier* mb,
                         const VkBufferMemoryBarrier* bmb,
                         const VkImageMemoryBarrier* imb,
                         GFXInjection_* injection);

/**
 * Completes dependency injections by catching pending signal commands.
 * Only operates on commands that reference a GFXSemaphore.
 * @param context   Cannot be NULL.
 * @param cmd       To record some initial barriers to, cannot be VK_NULL_HANDLE.
 * @param numInjs   Number of given injection commands.
 * @param injs      Given injection commands.
 * @param injection Input & output injection metadata, cannot be NULL.
 * @param Zero on failure, must call gfx_sems_abort_.
 *
 * Thread-safe with respect to all GFXSemaphores!
 *
 * Can be called any number of times using the same injection metadata pointer.
 * However, after the first call to `gfx_sems_abort_` or `gfx_sems_finish_`,
 * neither `gfx_sems_catch_` nor `gfx_sems_prepare_` can be called anymore.
 *
 * Every injection command passed to gfx_sems_(catch|prepare)_ must
 * subsequently be passed to a call to gfx_sems_abort_ or gfx_sems_finish_.
 * These subsequent calls MUST take the same injection metadata pointer.
 *
 * Inbetween calls injection->inp may be altered.
 * In fact, they must be altered if operation references were given.
 *
 * Right before the first call to gfx_sems_(abort|finish)_,
 * all output arrays in injection may be externally realloc'd,
 * they will be properly freed when aborted or finished.
 */
bool gfx_sems_catch_(GFXContext_* context, VkCommandBuffer cmd,
                     size_t numInjs, const GFXInject* injs,
                     GFXInjection_* injection);

/**
 * Starts dependency injections by preparing new signal commands.
 * Only operates on commands that reference a GFXSemaphore.
 * @param blocking Non-zero to indicate the operation is blocking.
 * @see gfx_sems_catch_.
 *
 * Thread-safe with respect to all GFXSemaphores!
 *
 * All commands are _always_ already visible to subsequent calls to
 * gfx_sems_catch_ taking the same injection metadata pointer.
 */
bool gfx_sems_prepare_(GFXContext_* context, VkCommandBuffer cmd,
                       bool blocking,
                       size_t numInjs, const GFXInject* injs,
                       GFXInjection_* injection);

/**
 * Aborts a dependency injection, cleaning all metadata.
 * @see gfx_sems_catch_.
 *
 * Thread-safe with respect to all GFXSemaphores!
 * The content of injection is invalidated after this call.
 *
 * Each injection metadata object must be called at least once with
 * either gfx_sems_abort_ OR gfx_sems_catch_ for ALL injection commands.
 * If no injection commands were given, one of these functions must still
 * be called with numInjs == 0!
 * NEVER can both calls be used for the same injection metadata pointer!
 */
void gfx_sems_abort_(size_t numInjs, const GFXInject* injs,
                     GFXInjection_* injection);

/**
 * Finalizes a dependency injection, all signal commands are made visible for
 * future wait commands and all wait commands are finalized and cleaned up.
 * @see gfx_sems_catch_.
 *
 * Thread-safe with respect to all GFXSemaphores!
 * The content of injection is invalidated after this call.
 */
void gfx_sems_finish_(size_t numInjs, const GFXInject* injs,
                      GFXInjection_* injection);


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
GFXBacking_* gfx_alloc_backing_(GFXHeap* heap,
                                const GFXImageAttach_* attach);

/**
 * Frees a backing image.
 * @param heap    Cannot be NULL, same heap backing was allocated with.
 * @param backing Cannot be NULL.
 *
 * Thread-safe with respect to the heap!
 * Does not unlink itself from anything!
 */
void gfx_free_backing_(GFXHeap* heap, GFXBacking_* backing);

/**
 * Allocates a staging buffer from a heap.
 * @param heap Cannot be NULL.
 * @param size Must be > 0.
 * @return NULL on failure.
 *
 * Thread-safe with respect to the heap!
 * Leaves the `list` base-type uninitialized!
 */
GFXStaging_* gfx_alloc_staging_(GFXHeap* heap,
                                VkBufferUsageFlags usage, uint64_t size);

/**
 * Frees a staging buffer.
 * @param heap    Cannot be NULL, same heap staging was allocated with.
 * @param staging Cannot be NULL.
 *
 * Thread-safe with respect to the heap!
 * Does not unlink itself from anything!
 */
void gfx_free_staging_(GFXHeap* heap, GFXStaging_* staging);

/**
 * Frees all staging buffers of a transfer operation.
 * @param heap     Cannot be NULL.
 * @param transfer Cannot be NULL.
 *
 * Thread-safe with respect to the heap!
 * Leaves `transfer->stagings` cleared.
 */
void gfx_free_stagings_(GFXHeap* heap, GFXTransfer_* transfer);

/**
 * Flushes the last (current) transfer operation of a transfer pool.
 * The `injection` and `injs` fields of pool will be freed after this call.
 * @param heap Cannot be NULL.
 * @param pool Cannot be NULL, must be of heap.
 * @return Zero on failure, current transfer is lost.
 *
 * Not thread-safe with respect to the heap or pool!
 */
bool gfx_flush_transfer_(GFXHeap* heap, GFXTransferPool_* pool);


/****************************
 * Pipeline creation & warmup.
 ****************************/

/**
 * Retrieves a graphics pipeline from the renderer's cache (or warms it up).
 * Essentially a wrapper for gfx_cache_(get|warmup)_.
 * @param renderable Cannot be NULL.
 * @param elem       Output cache element, cannot be NULL if warmup is zero.
 * @param warmup     Non-zero to only warmup and not retrieve.
 * @return Zero on failure.
 *
 * Completely thread-safe with respect to the renderable!
 */
bool gfx_renderable_pipeline_(GFXRenderable* renderable,
                              GFXCacheElem_** elem, bool warmup);

/**
 * Retrieves a compute pipeline from the renderer's cache (or warms it up).
 * Essentially a wrapper for gfx_cache_(get|warmup)_.
 * @param computable Cannot be NULL.
 * @see gfx_renderable_pipeline_.
 *
 * Completely thread-safe with respect to the computable!
 */
bool gfx_computable_pipeline_(GFXComputable* computable,
                              GFXCacheElem_** elem, bool warmup);


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
bool gfx_frame_init_(GFXRenderer* renderer, GFXFrame* frame, unsigned int index);

/**
 * Clears a virtual frame of a renderer.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 *
 * This will block until the frame is done rendering!
 */
void gfx_frame_clear_(GFXRenderer* renderer, GFXFrame* frame);

/**
 * Retrieves the swapchain image index associated with an attachment.
 * @param frame Cannot be NULL.
 * @param index Attachment index.
 * @return The swapchain image index, or UINT32_MAX if none associated.
 */
uint32_t gfx_frame_get_swapchain_index_(GFXFrame* frame, size_t index);

/**
 * Blocks until all pending submissions of a virtual frame are done
 * and subsequently resets all command pools.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @param reset    Non-zero to also reset command pools.
 * @return Non-zero if successfully synchronized.
 *
 * If resetting, this cannot be called again until a call
 * to gfx_frame_submit_ has been made.
 * Failure is considered fatal, frame cannot be used.
 */
bool gfx_frame_sync_(GFXRenderer* renderer, GFXFrame* frame, bool reset);

/**
 * Acquires all relevant resources for a virtual frame to be recorded.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @return Zero if the frame (or renderer) could not be built.
 *
 * This may call gfx_sync_frames_ internally on-swapchain recreate!
 * Cannot be called again until a call to gfx_frame_submit_ has been made.
 * Failure is considered fatal, swapchains could be left in an incomplete state.
 */
bool gfx_frame_acquire_(GFXRenderer* renderer, GFXFrame* frame);

/**
 * Records & submits a virtual frame.
 * Must be called exactly once for each call to gfx_frame_acquire_.
 * @param renderer Cannot be NULL.
 * @param frame    Cannot be NULL.
 * @return Zero if the frame could not be submitted.
 *
 * Failure is considered fatal, swapchains could be left in an incomplete state.
 */
bool gfx_frame_submit_(GFXRenderer* renderer, GFXFrame* frame);


/****************************
 * Render- backing and graph.
 ****************************/

/**
 * Initializes the render backing of a renderer.
 * @param renderer Cannot be NULL.
 */
void gfx_render_backing_init_(GFXRenderer* renderer);

/**
 * Clears the render backing of a renderer, destroying all images.
 * @param renderer Cannot be NULL.
 */
void gfx_render_backing_clear_(GFXRenderer* renderer);

/**
 * Builds not yet built resources of the render backing.
 * Will resolve to a no-op if everything is already built.
 * @param renderer Cannot be NULL.
 * @return Non-zero if the entire backing is in a built state.
 */
bool gfx_render_backing_build_(GFXRenderer* renderer);

/**
 * (Re)builds all relevant render backing resources.
 * Suitable for on-swapchain recreate (e.g. a window resize or smth).
 * @param renderer Cannot be NULL.
 * @param flags    Must contain the GFX_RECREATE_ bit.
 */
void gfx_render_backing_rebuild_(GFXRenderer* renderer, GFXRecreateFlags_ flags);

/**
 * Purges all relevant render backing resources.
 * Use to destroy stale backings to be purged at the current frame index.
 * @param renderer Cannot be NULL.
 */
void gfx_render_backing_purge_(GFXRenderer* renderer);

/**
 * Initializes the render graph of a renderer.
 * @param renderer Cannot be NULL.
 */
void gfx_render_graph_init_(GFXRenderer* renderer);

/**
 * Clears the render graph of a renderer, destroying all passes.
 * @param renderer Cannot be NULL.
 */
void gfx_render_graph_clear_(GFXRenderer* renderer);

/**
 * Builds the Vulkan render passes if not present yet.
 * Can be used for potential pipeline warmups.
 * @param renderer Cannot be NULL.
 * @return Non-zero on success.
 *
 * This will call the relevant gfx_pass_(destruct|warmup)_ calls.
 */
bool gfx_render_graph_warmup_(GFXRenderer* renderer);

/**
 * (Re)builds the render graph and all its resources.
 * Will resolve to a no-op if everything is already built.
 * @param renderer Cannot be NULL.
 * @return Non-zero if the entire graph is in a built state.
 *
 * This will call the relevant gfx_pass_(destruct|build)_ calls.
 */
bool gfx_render_graph_build_(GFXRenderer* renderer);

/**
 * Rebuilds all relevant render graph resources.
 * Suitable for on-swapchain recreate (e.g. a window resize or smth).
 * @param renderer Cannot be NULL.
 * @param flags    Must contain the GFX_RECREATE_ bit.
 *
 * This will call the relevant gfx_pass_rebuild_ calls.
 */
void gfx_render_graph_rebuild_(GFXRenderer* renderer, GFXRecreateFlags_ flags);

/**
 * Immediately destruct all render graph resources.
 * @param renderer Cannot be NULL.
 *
 * Must be called before detaching any attachment!
 * This will call the relevant gfx_pass_destruct_ calls.
 */
void gfx_render_graph_destruct_(GFXRenderer* renderer);

/**
 * Invalidates the render graph, forcing it to first destruct everything
 * the next time gfx_render_graph_(warmup|build)_ is called.
 * If gfx_render_graph_rebuild_ is called before that, it is rendered a no-op.
 * Suitable for consumptions have changed or new attachments are described.
 * @param renderer Cannot be NULL.
 */
void gfx_render_graph_invalidate_(GFXRenderer* renderer);


/****************************
 * Pass (nodes in the render graph).
 ****************************/

/**
 * Creates a pass, referencing all parents.
 * Each element in parents must be associated with the same renderer.
 * @param renderer   Cannot be NULL.
 * @param culled     Whether or not this pass is culled.
 * @param numParents Number of parents, 0 for none.
 * @param parents    Parent passes, cannot be NULL if numParents > 0.
 * @return NULL on failure.
 *
 * Leaves the `list` base-type uninitialized!
 * Additionally, the `level` field is left at zero.
 */
GFXPass* gfx_create_pass_(GFXRenderer* renderer, GFXPassType type,
                          bool culled,
                          size_t numParents, GFXPass** parents);

/**
 * Destroys a pass.
 * @param pass Cannot be NULL.
 *
 * MUST first destruct render graph (except when destroying all passes)!
 * Does not unlink itself from anything!
 */
void gfx_destroy_pass_(GFXPass* pass);

/**
 * Retrieves the current framebuffer of a pass with respect to a frame.
 * @param rPass Cannot be NULL, must not be culled.
 * @param frame Cannot be NULL.
 * @return VK_NULL_HANDLE if unknown.
 */
VkFramebuffer gfx_pass_framebuffer_(GFXRenderPass_* rPass, GFXFrame* frame);

/**
 * Builds the Vulkan render pass if not present yet.
 * Can be used for potential pipeline warmups.
 * @param rPass Cannot be NULL, cannot be culled and must be a master pass!
 * @return Non-zero on success.
 *
 * Before the initial call to gfx_pass_(warmup|build)_ and once after a call
 * to gfx_pass_destruct_, the following MUST be set of ALL passes and
 * consumptions to influence the build:
 *  rPass->out.*
 *  rPass->base.consumes[*]->out.*
 */
bool gfx_pass_warmup_(GFXRenderPass_* rPass);

/**
 * Builds the Vulkan framebuffer (and others) if not present yet.
 * @param rPass Cannot be NULL, cannot be culled and must be a master pass!
 * @return Non-zero if completely valid and built.
 *
 * @see gfx_pass_warmup_ for influencing the build.
 */
bool gfx_pass_build_(GFXRenderPass_* rPass);

/**
 * Rebuilds Vulkan objects, does NOT build not yet built objects!
 * @param rPass Cannot be NULL, cannot be culled and must be a master pass!
 * @param flags Must contain the GFX_RECREATE_ bit.
 * @return Non-zero if rebuilt successfully.
 */
bool gfx_pass_rebuild_(GFXRenderPass_* rPass, GFXRecreateFlags_ flags);

/**
 * Destructs all Vulkan objects, non-recursively. As opposed to
 * gfx_pass_(warmup|build|rebuild)_, this must be called for all passes!
 * @param rPass Cannot be NULL.
 *
 * Must be called before its attachments are changed!
 */
void gfx_pass_destruct_(GFXRenderPass_* rPass);


/****************************
 * Renderer, recorder, technique and set.
 ****************************/

/**
 * Retrieves a sampler from the renderer's cache (wrapper for gfx_cache_get_).
 * @param renderer Cannot be NULL.
 * @param sampler  Sampler values to use, NULL for the default sampler.
 * @return NULL on failure.
 */
GFXCacheElem_* gfx_get_sampler_(GFXRenderer* renderer,
                                const GFXSampler* sampler);

/**
 * Pushes stale resources to the renderer, subsequently
 * destroying it the next time the previous frame is acquired again.
 * @param renderer Cannot be NULL.
 * @return Non-zero if successfully pushed.
 *
 * Completely thread-safe and reentrant!
 *
 * Any Vulkan resource handle may be VK_NULL_HANDLE, as long as one is not.
 * All handles are invalidated after this call.
 * Failure is considered fatal, resources are prematurely destroyed.
 */
bool gfx_push_stale_(GFXRenderer* renderer,
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
bool gfx_sync_frames_(GFXRenderer* renderer);

/**
 * Resets a recording pool, i.e. resets all command buffers
 * and sets the new current recording pool(s) to use for recording commands.
 * @param recorder Cannot be NULL.
 * @return Non-zero if successfully reset.
 */
bool gfx_recorder_reset_(GFXRecorder* recorder);

/**
 * Records the recording output of a recorder into a given command buffer.
 * The command buffer must be in the recording state (!).
 * @param recorder Cannot be NULL.
 * @param order    Buffers that were output with this order will be recorded.
 * @param cmd      Cannot be NULL, must be in the render pass of `order` (!).
 */
void gfx_recorder_record_(GFXRecorder* recorder,
                          unsigned int order, VkCommandBuffer cmd);

/**
 * Retrieves all Vulkan specialization constant info and map entries.
 * @param technique Cannot be NULL, must be locked.
 * @param infos     `GFX_NUM_SHADER_STAGES_` VkSpecilizationInfo structs.
 * @param entries   `technique->constants.size` VkSpecializationMapEntry structs.
 *
 * All output entries are sorted on { stage, constantID }.
 */
void gfx_tech_get_constants_(GFXTechnique* technique,
                             VkSpecializationInfo* infos,
                             VkSpecializationMapEntry* entries);

/**
 * Retrieves a descriptor set binding from a technique and populates the
 * `type`, `viewType`, `count` and `size` fields of a GFXSetBinding_ struct.
 * @param technique Cannot be NULL, must be locked.
 * @param set       Must be < technique->numSets.
 * @param binding   Descriptor binding number.
 * @param out       Output GFXSetBinding_ to populate.
 * @return Zero if this binding DOES NOT occupy any GFXSetEntry_'s!
 */
bool gfx_tech_get_set_binding_(GFXTechnique* technique,
                               size_t set, size_t binding, GFXSetBinding_* out);

/**
 * Retrieves, allocates or recycles a Vulkan descriptor set of the given set.
 * @param set Cannot be NULL.
 * @param sub Cannot be NULL, must be of the same renderer as set.
 * @return NULL on failure.
 *
 * Thread-safe with respect to the set and other subordinates,
 * essentially a wrapper for gfx_pool_get_.
 * However, can never run concurrently with other set functions.
 */
GFXPoolElem_* gfx_set_get_(GFXSet* set, GFXPoolSub_* sub);


#endif
