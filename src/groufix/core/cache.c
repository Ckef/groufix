/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/mem.h"
#include <assert.h>
#include <stdlib.h>


// Pushes a field to a hash key being built.
#define _GFX_KEY_PUSH(field) \
	do { \
		if (!_GFX_HASH_BUILDER_PUSH(&builder, sizeof(field), &(field))) \
			goto clean; \
	} while (0)

// Pushes a handle into a hash key being built.
#define _GFX_KEY_PUSH_HANDLE() \
	do { \
		if (!_GFX_HASH_BUILDER_PUSH(&builder, sizeof(*handles), &handles[currHandle++])) \
			goto clean; \
	} while (0)


/****************************
 * Allocates & builds a hashable key value from a Vk*CreateInfo struct
 * with given replace handles for non-hashable fields.
 * @return Key value, must call free() on success (NULL on failure).
 */
static _GFXHashKey* _gfx_cache_alloc_key(const VkStructureType* createInfo,
                                         const void** handles)
{
	assert(createInfo != NULL);

	// Initialize a hash key builder.
	_GFXHashBuilder builder;
	if (!_gfx_hash_builder(&builder)) goto error;

	// Based on type, push all the to-be-hashed data.
	// Here we try to minimize the data actually necessary to specify
	// a unique cache object, so everything will be packed tightly.
	// The elements of the Vk*CreateInfo struct will be pushed linearly,
	// such as the specs say, to avoid confusion.
	// Note we do not push any VkStructureType fields except for the main one.
	// Plus we insert the given handles for fields we cannot hash.
	size_t currHandle = 0;
	char temp;

	switch (*createInfo)
	{
	case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO:

		_GFX_KEY_PUSH(*createInfo);
		const VkDescriptorSetLayoutCreateInfo* dslci =
			(const VkDescriptorSetLayoutCreateInfo*)createInfo;

		// Ignore the pNext field.
		_GFX_KEY_PUSH(dslci->flags);
		_GFX_KEY_PUSH(dslci->bindingCount);

		for (size_t b = 0; b < dslci->bindingCount; ++b)
		{
			const VkDescriptorSetLayoutBinding* dslb = dslci->pBindings + b;
			_GFX_KEY_PUSH(dslb->binding);
			_GFX_KEY_PUSH(dslb->descriptorType);
			_GFX_KEY_PUSH(dslb->descriptorCount);
			_GFX_KEY_PUSH(dslb->stageFlags);

			// Insert bool 'has immutable samplers'.
			temp =
				dslb->descriptorCount > 0 &&
				dslb->pImmutableSamplers != NULL;

			_GFX_KEY_PUSH(temp);

			if (dslb->pImmutableSamplers != NULL)
				for (size_t s = 0; s < dslb->descriptorCount; ++s)
					_GFX_KEY_PUSH_HANDLE();
		}
		break;

	case VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO:

		_GFX_KEY_PUSH(*createInfo);
		const VkPipelineLayoutCreateInfo* plci =
			(const VkPipelineLayoutCreateInfo*)createInfo;

		// Ignore the pNext field.
		// Ignore pipeline layout flags.
		_GFX_KEY_PUSH(plci->setLayoutCount);

		for (size_t s = 0; s < plci->setLayoutCount; ++s)
			_GFX_KEY_PUSH_HANDLE();

		_GFX_KEY_PUSH(plci->pushConstantRangeCount);

		for (size_t p = 0; p < plci->pushConstantRangeCount; ++p)
		{
			_GFX_KEY_PUSH(plci->pPushConstantRanges[p].stageFlags);
			_GFX_KEY_PUSH(plci->pPushConstantRanges[p].offset);
			_GFX_KEY_PUSH(plci->pPushConstantRanges[p].size);
		}
		break;

	case VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO:

		_GFX_KEY_PUSH(*createInfo);
		const VkSamplerCreateInfo* sci =
			(const VkSamplerCreateInfo*)createInfo;

		// Ignore the pNext field.
		// Ignore sampler flags.
		_GFX_KEY_PUSH(sci->magFilter);
		_GFX_KEY_PUSH(sci->minFilter);
		_GFX_KEY_PUSH(sci->mipmapMode);
		_GFX_KEY_PUSH(sci->addressModeU);
		_GFX_KEY_PUSH(sci->addressModeV);
		_GFX_KEY_PUSH(sci->addressModeW);
		_GFX_KEY_PUSH(sci->mipLodBias);
		_GFX_KEY_PUSH(sci->anisotropyEnable);
		_GFX_KEY_PUSH(sci->maxAnisotropy);
		_GFX_KEY_PUSH(sci->compareEnable);
		_GFX_KEY_PUSH(sci->compareOp);
		_GFX_KEY_PUSH(sci->minLod);
		_GFX_KEY_PUSH(sci->maxLod);
		_GFX_KEY_PUSH(sci->borderColor);
		_GFX_KEY_PUSH(sci->unnormalizedCoordinates);
		break;

	case VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO:

		_GFX_KEY_PUSH(*createInfo);
		const VkRenderPassCreateInfo* rpci =
			(const VkRenderPassCreateInfo*)createInfo;

		// TODO: Push compatibility info first (with a prepended byte length),
		// so when building pipeline keys we can insert that info?

		// Ignore the pNext field.
		// Ignore render pass flags.
		_GFX_KEY_PUSH(rpci->attachmentCount);

		for (size_t a = 0; a < rpci->attachmentCount; ++a)
		{
			_GFX_KEY_PUSH(rpci->pAttachments[a].flags);
			_GFX_KEY_PUSH(rpci->pAttachments[a].format);
			_GFX_KEY_PUSH(rpci->pAttachments[a].samples);
			_GFX_KEY_PUSH(rpci->pAttachments[a].loadOp);
			_GFX_KEY_PUSH(rpci->pAttachments[a].storeOp);
			_GFX_KEY_PUSH(rpci->pAttachments[a].stencilLoadOp);
			_GFX_KEY_PUSH(rpci->pAttachments[a].stencilStoreOp);
			_GFX_KEY_PUSH(rpci->pAttachments[a].initialLayout);
			_GFX_KEY_PUSH(rpci->pAttachments[a].finalLayout);
		}

		_GFX_KEY_PUSH(rpci->subpassCount);

		for (size_t s = 0; s < rpci->subpassCount; ++s)
		{
			const VkSubpassDescription* sd = rpci->pSubpasses + s;
			// Ignore subpass flags.
			_GFX_KEY_PUSH(sd->pipelineBindPoint);
			_GFX_KEY_PUSH(sd->inputAttachmentCount);

			for (size_t i = 0; i < sd->inputAttachmentCount; ++i)
			{
				_GFX_KEY_PUSH(sd->pInputAttachments[i].attachment);
				_GFX_KEY_PUSH(sd->pInputAttachments[i].layout);
			}

			_GFX_KEY_PUSH(sd->colorAttachmentCount);

			for (size_t c = 0; c < sd->colorAttachmentCount; ++c)
			{
				_GFX_KEY_PUSH(sd->pColorAttachments[c].attachment);
				_GFX_KEY_PUSH(sd->pColorAttachments[c].layout);
			}

			// Insert bool 'has resolve attachments'.
			temp =
				sd->colorAttachmentCount > 0 &&
				sd->pResolveAttachments != NULL;

			_GFX_KEY_PUSH(temp);

			if (sd->pResolveAttachments != NULL)
				for (size_t r = 0; r < sd->colorAttachmentCount; ++r)
				{
					_GFX_KEY_PUSH(sd->pResolveAttachments[r].attachment);
					_GFX_KEY_PUSH(sd->pResolveAttachments[r].layout);
				}

			// Insert bool 'has depth stencil attachment'
			temp = sd->pDepthStencilAttachment != NULL;
			_GFX_KEY_PUSH(temp);

			if (sd->pDepthStencilAttachment != NULL)
			{
				_GFX_KEY_PUSH(sd->pDepthStencilAttachment->attachment);
				_GFX_KEY_PUSH(sd->pDepthStencilAttachment->layout);
			}

			_GFX_KEY_PUSH(sd->preserveAttachmentCount);

			for (size_t p = 0; p < sd->preserveAttachmentCount; ++p)
				_GFX_KEY_PUSH(sd->pPreserveAttachments[p]);
		}

		_GFX_KEY_PUSH(rpci->dependencyCount);

		for (size_t d = 0; d < rpci->dependencyCount; ++d)
		{
			_GFX_KEY_PUSH(rpci->pDependencies[d].srcSubpass);
			_GFX_KEY_PUSH(rpci->pDependencies[d].dstSubpass);
			_GFX_KEY_PUSH(rpci->pDependencies[d].srcStageMask);
			_GFX_KEY_PUSH(rpci->pDependencies[d].dstStageMask);
			_GFX_KEY_PUSH(rpci->pDependencies[d].srcAccessMask);
			_GFX_KEY_PUSH(rpci->pDependencies[d].dstAccessMask);
			_GFX_KEY_PUSH(rpci->pDependencies[d].dependencyFlags);
		}
		break;

	case VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO:

		_GFX_KEY_PUSH(*createInfo);
		const VkGraphicsPipelineCreateInfo* gpci =
			(const VkGraphicsPipelineCreateInfo*)createInfo;

		// Ignore the pNext field.
		_GFX_KEY_PUSH(gpci->flags);
		_GFX_KEY_PUSH(gpci->stageCount);

		for (size_t s = 0; s < gpci->stageCount; ++s)
		{
			const VkPipelineShaderStageCreateInfo* pssci = gpci->pStages + s;
			// Ignore the pNext field.
			// Ignore shader stage flags.
			_GFX_KEY_PUSH(pssci->stage);
			_GFX_KEY_PUSH_HANDLE();
			// Ignore the entry point name.

			// Insert bool 'has specialization info'.
			temp = pssci->pSpecializationInfo != NULL;
			_GFX_KEY_PUSH(temp);

			if (pssci->pSpecializationInfo != NULL)
			{
				const VkSpecializationInfo* si = pssci->pSpecializationInfo;
				_GFX_KEY_PUSH(si->mapEntryCount);

				for (size_t e = 0; e < si->mapEntryCount; ++e)
				{
					_GFX_KEY_PUSH(si->pMapEntries[e].constantID);
					_GFX_KEY_PUSH(si->pMapEntries[e].offset);
					_GFX_KEY_PUSH(si->pMapEntries[e].size);
				}

				_GFX_KEY_PUSH(si->dataSize);

				if (si->dataSize > 0)
					if (!_GFX_HASH_BUILDER_PUSH(&builder, si->dataSize, si->pData))
						goto clean;
			}
		}

		const VkPipelineVertexInputStateCreateInfo* pvisci = gpci->pVertexInputState;
		// Ignore the pNext field.
		// Ignore vertex input state flags.
		_GFX_KEY_PUSH(pvisci->vertexBindingDescriptionCount);

		for (size_t b = 0; b < pvisci->vertexBindingDescriptionCount; ++b)
		{
			const VkVertexInputBindingDescription* vibd =
				pvisci->pVertexBindingDescriptions + b;

			_GFX_KEY_PUSH(vibd->binding);
			_GFX_KEY_PUSH(vibd->stride);
			_GFX_KEY_PUSH(vibd->inputRate);
		}

		_GFX_KEY_PUSH(pvisci->vertexAttributeDescriptionCount);

		for (size_t a = 0; a < pvisci->vertexAttributeDescriptionCount; ++a)
		{
			const VkVertexInputAttributeDescription* viad =
				pvisci->pVertexAttributeDescriptions + a;

			_GFX_KEY_PUSH(viad->location);
			_GFX_KEY_PUSH(viad->binding);
			_GFX_KEY_PUSH(viad->format);
			_GFX_KEY_PUSH(viad->offset);
		}

		const VkPipelineInputAssemblyStateCreateInfo* piasci = gpci->pInputAssemblyState;
		// Ignore the pNext field.
		// Ignore input assembly state flags.
		_GFX_KEY_PUSH(piasci->topology);
		_GFX_KEY_PUSH(piasci->primitiveRestartEnable);

		// Insert bool 'has tessellation state'.
		temp = gpci->pTessellationState != NULL;
		_GFX_KEY_PUSH(temp);

		if (gpci->pTessellationState != NULL)
		{
			const VkPipelineTessellationStateCreateInfo* ptsci = gpci->pTessellationState;
			// Ignore the pNext field.
			// Ignore tessellation state flags.
			_GFX_KEY_PUSH(ptsci->patchControlPoints);
		}

		// Insert bool 'has viewport state'.
		temp = gpci->pViewportState != NULL;
		_GFX_KEY_PUSH(temp);

		if (gpci->pViewportState != NULL)
		{
			const VkPipelineViewportStateCreateInfo* pvsci = gpci->pViewportState;
			// Ignore the pNext field.
			// Ignore viewport state flags.
			_GFX_KEY_PUSH(pvsci->viewportCount);

			// Insert bool 'has viewports'.
			temp =
				pvsci->viewportCount > 0 &&
				pvsci->pViewports != NULL;

			_GFX_KEY_PUSH(temp);

			if (pvsci->pViewports != NULL)
				for (size_t v = 0; v < pvsci->viewportCount; ++v)
				{
					_GFX_KEY_PUSH(pvsci->pViewports[v].x);
					_GFX_KEY_PUSH(pvsci->pViewports[v].y);
					_GFX_KEY_PUSH(pvsci->pViewports[v].width);
					_GFX_KEY_PUSH(pvsci->pViewports[v].height);
					_GFX_KEY_PUSH(pvsci->pViewports[v].minDepth);
					_GFX_KEY_PUSH(pvsci->pViewports[v].maxDepth);
				}

			_GFX_KEY_PUSH(pvsci->scissorCount);

			// Insert bool 'has scissors'.
			temp =
				pvsci->scissorCount > 0 &&
				pvsci->pScissors != NULL;

			_GFX_KEY_PUSH(temp);

			if (pvsci->pScissors != NULL)
				for (size_t s = 0; s < pvsci->scissorCount; ++s)
				{
					_GFX_KEY_PUSH(pvsci->pScissors[s].offset);
					_GFX_KEY_PUSH(pvsci->pScissors[s].extent);
				}
		}

		const VkPipelineRasterizationStateCreateInfo* prsci = gpci->pRasterizationState;
		// Ignore the pNext field.
		// Ignore rasterization state flags.
		_GFX_KEY_PUSH(prsci->depthClampEnable);
		_GFX_KEY_PUSH(prsci->rasterizerDiscardEnable);
		_GFX_KEY_PUSH(prsci->polygonMode);
		_GFX_KEY_PUSH(prsci->cullMode);
		_GFX_KEY_PUSH(prsci->frontFace);
		_GFX_KEY_PUSH(prsci->depthBiasEnable);
		_GFX_KEY_PUSH(prsci->depthBiasConstantFactor);
		_GFX_KEY_PUSH(prsci->depthBiasClamp);
		_GFX_KEY_PUSH(prsci->depthBiasSlopeFactor);
		_GFX_KEY_PUSH(prsci->lineWidth);

		// Insert bool 'has multisample state'.
		temp = gpci->pMultisampleState != NULL;
		_GFX_KEY_PUSH(temp);

		if (gpci->pMultisampleState != NULL)
		{
			const VkPipelineMultisampleStateCreateInfo* pmsci = gpci->pMultisampleState;
			// Ignore the pNext field.
			// Ignore multisample state flags.
			_GFX_KEY_PUSH(pmsci->rasterizationSamples);
			_GFX_KEY_PUSH(pmsci->sampleShadingEnable);
			_GFX_KEY_PUSH(pmsci->minSampleShading);
			// Ignore sample masks.
			_GFX_KEY_PUSH(pmsci->alphaToCoverageEnable);
			_GFX_KEY_PUSH(pmsci->alphaToOneEnable);
		}

		// Insert bool 'has depth stencil state'.
		temp = gpci->pDepthStencilState != NULL;
		_GFX_KEY_PUSH(temp);

		if (gpci->pDepthStencilState != NULL)
		{
			const VkPipelineDepthStencilStateCreateInfo* pdssci = gpci->pDepthStencilState;
			// Ignore the pNext field.
			// Ignore depth stencil state flags.
			_GFX_KEY_PUSH(pdssci->depthTestEnable);
			_GFX_KEY_PUSH(pdssci->depthWriteEnable);
			_GFX_KEY_PUSH(pdssci->depthCompareOp);
			_GFX_KEY_PUSH(pdssci->depthBoundsTestEnable);
			_GFX_KEY_PUSH(pdssci->stencilTestEnable);
			_GFX_KEY_PUSH(pdssci->front);
			_GFX_KEY_PUSH(pdssci->back);
			_GFX_KEY_PUSH(pdssci->minDepthBounds);
			_GFX_KEY_PUSH(pdssci->maxDepthBounds);
		}

		// Insert bool 'has color blend state'.
		temp = gpci->pColorBlendState != NULL;
		_GFX_KEY_PUSH(temp);

		if (gpci->pColorBlendState != NULL)
		{
			const VkPipelineColorBlendStateCreateInfo* pcbsci = gpci->pColorBlendState;
			// Ignore the pNext field.
			// Ignore color blend state flags.
			_GFX_KEY_PUSH(pcbsci->logicOpEnable);
			_GFX_KEY_PUSH(pcbsci->logicOp);
			_GFX_KEY_PUSH(pcbsci->attachmentCount);

			for (size_t a = 0; a < pcbsci->attachmentCount; ++a)
			{
				_GFX_KEY_PUSH(pcbsci->pAttachments[a].blendEnable);
				_GFX_KEY_PUSH(pcbsci->pAttachments[a].srcColorBlendFactor);
				_GFX_KEY_PUSH(pcbsci->pAttachments[a].dstColorBlendFactor);
				_GFX_KEY_PUSH(pcbsci->pAttachments[a].colorBlendOp);
				_GFX_KEY_PUSH(pcbsci->pAttachments[a].srcAlphaBlendFactor);
				_GFX_KEY_PUSH(pcbsci->pAttachments[a].dstAlphaBlendFactor);
				_GFX_KEY_PUSH(pcbsci->pAttachments[a].alphaBlendOp);
				_GFX_KEY_PUSH(pcbsci->pAttachments[a].colorWriteMask);
			}

			if (!_GFX_HASH_BUILDER_PUSH(&builder, sizeof(pcbsci->blendConstants), pcbsci->blendConstants))
				goto clean;
		}

		// Insert bool 'has dynamic state'.
		temp = gpci->pDynamicState != NULL;
		_GFX_KEY_PUSH(temp);

		if (gpci->pDynamicState != NULL)
		{
			const VkPipelineDynamicStateCreateInfo* pdsci = gpci->pDynamicState;
			// Ignore the pNext field.
			// Ignore dynamic state flags.
			_GFX_KEY_PUSH(pdsci->dynamicStateCount);

			for (size_t d = 0; d < pdsci->dynamicStateCount; ++d)
				_GFX_KEY_PUSH(pdsci->pDynamicStates[d]);
		}

		_GFX_KEY_PUSH_HANDLE();
		_GFX_KEY_PUSH_HANDLE();
		_GFX_KEY_PUSH(gpci->subpass);
		// Ignore base pipeline.
		// Ignore pipeline index.
		break;

	case VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO:

		_GFX_KEY_PUSH(*createInfo);
		const VkComputePipelineCreateInfo* cpci =
			(const VkComputePipelineCreateInfo*)createInfo;

		// Ignore the pNext field.
		_GFX_KEY_PUSH(cpci->flags);
		// Ignore the pNext field.
		// Ignore shader stage flags.
		_GFX_KEY_PUSH(cpci->stage.stage);
		_GFX_KEY_PUSH_HANDLE();
		// Ignore the entry point name.

		// Insert bool 'has specialization info'.
		temp = cpci->stage.pSpecializationInfo != NULL;
		_GFX_KEY_PUSH(temp);

		if (cpci->stage.pSpecializationInfo != NULL)
		{
			const VkSpecializationInfo* si = cpci->stage.pSpecializationInfo;
			_GFX_KEY_PUSH(si->mapEntryCount);

			for (size_t e = 0; e < si->mapEntryCount; ++e)
			{
				_GFX_KEY_PUSH(si->pMapEntries[e].constantID);
				_GFX_KEY_PUSH(si->pMapEntries[e].offset);
				_GFX_KEY_PUSH(si->pMapEntries[e].size);
			}

			_GFX_KEY_PUSH(si->dataSize);

			if (si->dataSize > 0)
				if (!_GFX_HASH_BUILDER_PUSH(&builder, si->dataSize, si->pData))
					goto clean;
		}

		_GFX_KEY_PUSH_HANDLE();
		// Ignore base pipeline.
		// Ignore pipeline index.
		break;

	default:
		goto clean;
	}

	// Return the key data.
	return _gfx_hash_builder_get(&builder);


	// Cleanup on failure.
clean:
	free(_gfx_hash_builder_get(&builder));
error:
	gfx_log_error("Could not allocate key for cached Vulkan object.");

	return NULL;
}

/****************************
 * Creates a new Vulkan object using the given Vk*CreateInfo struct and
 * outputs to the given _GFXCacheElem struct.
 * @return Non-zero on success.
 */
static int _gfx_cache_create_elem(_GFXCache* cache, _GFXCacheElem* elem,
                                  const VkStructureType* createInfo)
{
	assert(cache != NULL);
	assert(elem != NULL);
	assert(createInfo != NULL);

	_GFXContext* context = cache->context;

	// Firstly, set type.
	elem->type = *createInfo;

	// Then call the appropriate create function.
	switch (elem->type)
	{
	case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreateDescriptorSetLayout(context->vk.device,
				(const VkDescriptorSetLayoutCreateInfo*)createInfo, NULL,
				&elem->setLayout),
			goto error);
		break;

	case VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreatePipelineLayout(context->vk.device,
				(const VkPipelineLayoutCreateInfo*)createInfo, NULL,
				&elem->layout),
			goto error);
		break;

	case VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreateSampler(context->vk.device,
				(const VkSamplerCreateInfo*)createInfo, NULL,
				&elem->sampler),
			goto error);
		break;

	case VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreateRenderPass(context->vk.device,
				(const VkRenderPassCreateInfo*)createInfo, NULL,
				&elem->pass),
			goto error);
		break;

	case VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreateGraphicsPipelines(context->vk.device,
				cache->vk.cache, 1,
				(const VkGraphicsPipelineCreateInfo*)createInfo, NULL,
				&elem->pipeline),
			goto error);
		break;

	case VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO:
		_GFX_VK_CHECK(
			context->vk.CreateComputePipelines(context->vk.device,
				cache->vk.cache, 1,
				(const VkComputePipelineCreateInfo*)createInfo, NULL,
				&elem->pipeline),
			goto error);
		break;

	default:
		goto error;
	}

	return 1;


	// Error on failure.
error:
	gfx_log_error("Could not create cached Vulkan object.");
	return 0;
}

/****************************
 * Destroys the Vulkan object stored in the given _GFXCacheElem struct.
 */
static void _gfx_cache_destroy_elem(_GFXCache* cache, _GFXCacheElem* elem)
{
	assert(cache != NULL);
	assert(elem != NULL);

	_GFXContext* context = cache->context;

	// Call the appropriate destroy function from type.
	switch (elem->type)
	{
	case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO:
		context->vk.DestroyDescriptorSetLayout(
			context->vk.device, elem->setLayout, NULL);
		break;

	case VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO:
		context->vk.DestroyPipelineLayout(
			context->vk.device, elem->layout, NULL);
		break;

	case VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO:
		context->vk.DestroySampler(
			context->vk.device, elem->sampler, NULL);
		break;

	case VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO:
		context->vk.DestroyRenderPass(
			context->vk.device, elem->pass, NULL);
		break;

	case VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO:
	case VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO:
		context->vk.DestroyPipeline(
			context->vk.device, elem->pipeline, NULL);
		break;

	default:
		break;
	}
}

/****************************/
int _gfx_cache_init(_GFXCache* cache, _GFXDevice* device)
{
	assert(cache != NULL);
	assert(device != NULL);
	assert(device->context != NULL);

	cache->context = device->context;
	cache->vk.cache = VK_NULL_HANDLE;

	// Initialize the locks.
	if (!_gfx_mutex_init(&cache->lookupLock))
		return 0;

	if (!_gfx_mutex_init(&cache->createLock))
	{
		_gfx_mutex_clear(&cache->lookupLock);
		return 0;
	}

	// Initialize the hashtables.
	// Take the largest alignment of the key and element types.
	size_t align =
		GFX_MAX(_Alignof(_GFXHashKey), _Alignof(_GFXCacheElem));

	gfx_map_init(&cache->immutable,
		sizeof(_GFXCacheElem), align, _gfx_hash_murmur3, _gfx_hash_cmp);

	gfx_map_init(&cache->mutable,
		sizeof(_GFXCacheElem), align, _gfx_hash_murmur3, _gfx_hash_cmp);

	return 1;
}

/****************************/
void _gfx_cache_clear(_GFXCache* cache)
{
	assert(cache != NULL);

	// Destroy all objects in the mutable cache.
	for (
		_GFXCacheElem* elem = gfx_map_first(&cache->mutable);
		elem != NULL;
		elem = gfx_map_next(&cache->mutable, elem))
	{
		_gfx_cache_destroy_elem(cache, elem);
	}

	// Destroy all objects in the immutable cache.
	for (
		_GFXCacheElem* elem = gfx_map_first(&cache->immutable);
		elem != NULL;
		elem = gfx_map_next(&cache->immutable, elem))
	{
		_gfx_cache_destroy_elem(cache, elem);
	}

	// Clear all other things.
	gfx_map_clear(&cache->immutable);
	gfx_map_clear(&cache->mutable);

	_gfx_mutex_clear(&cache->createLock);
	_gfx_mutex_clear(&cache->lookupLock);
}

/****************************/
int _gfx_cache_flush(_GFXCache* cache)
{
	assert(cache != NULL);

	// No need to lock anything, we just merge the tables.
	return gfx_map_merge(&cache->immutable, &cache->mutable);
}

/****************************/
int _gfx_cache_warmup(_GFXCache* cache,
                      const VkStructureType* createInfo,
                      const void** handles)
{
	assert(cache != NULL);
	assert(createInfo != NULL);

	// Firstly we create a key value & hash it.
	_GFXHashKey* key = _gfx_cache_alloc_key(createInfo, handles);
	if (key == NULL) return 0;

	const uint64_t hash = cache->immutable.hash(key);

	// Here we do need to lock the immutable cache, as we want the function
	// to be reentrant. However we have no dedicated lock.
	// Luckily this function _does not_ need to be able to run concurrently
	// with _gfx_cache_get, so we abuse the lookup lock :)
	_gfx_mutex_lock(&cache->lookupLock);

	// Try to find a matching element first.
	_GFXCacheElem* elem = gfx_map_hsearch(&cache->immutable, key, hash);
	if (elem != NULL)
		// Found one, done, we do not care if it is completely built yet.
		_gfx_mutex_unlock(&cache->lookupLock);
	else
	{
		// If not found, insert a new element.
		// Then immediately unlock so other warmups can be performed.
		elem = gfx_map_hinsert(
			&cache->immutable, NULL, _GFX_HASH_KEY_SIZE(key), key, hash);

		_gfx_mutex_unlock(&cache->lookupLock);

		// THEN create it :)
		if (elem == NULL || !_gfx_cache_create_elem(cache, elem, createInfo))
		{
			// Failed.. I suppose we erase the element.
			if (elem != NULL)
			{
				_gfx_mutex_lock(&cache->lookupLock);
				gfx_map_erase(&cache->immutable, elem);
				_gfx_mutex_unlock(&cache->lookupLock);
			}

			free(key);
			return 0;
		}
	}

	// Free data & return.
	free(key);
	return 1;
}

/****************************/
_GFXCacheElem* _gfx_cache_get(_GFXCache* cache,
                              const VkStructureType* createInfo,
                              const void** handles)
{
	assert(cache != NULL);
	assert(createInfo != NULL);

	// Again, create a key value & hash it.
	_GFXHashKey* key = _gfx_cache_alloc_key(createInfo, handles);
	if (key == NULL) return NULL;

	const uint64_t hash = cache->immutable.hash(key);

	// First we check the immutable cache.
	// Since this function is only allowed to run concurrently with itself,
	// we do not modify and therefore do not lock this cache :)
	_GFXCacheElem* elem = gfx_map_hsearch(&cache->immutable, key, hash);
	if (elem != NULL) goto found;

	// If not found in the immutable cache, check the mutable cache.
	// For this lookup we obviously do lock.
	_gfx_mutex_lock(&cache->lookupLock);
	elem = gfx_map_hsearch(&cache->mutable, key, hash);
	_gfx_mutex_unlock(&cache->lookupLock);

	if (elem != NULL) goto found;

	// If we did not find it yet, we need to insert a new element in the
	// mutable cache. We want other threads to still be able to query while
	// creating, so we lock for 'creation' separately.
	// But then we need to immediately check if the element already exists.
	// This because multiple threads could simultaneously decide to create
	// the same new element.
	// TODO: Have a more fine grained mechanism for locking, so we only
	// block when trying to create the SAME object, and not for ALL objects?
	_gfx_mutex_lock(&cache->createLock);

	_gfx_mutex_lock(&cache->lookupLock);
	elem = gfx_map_hsearch(&cache->mutable, key, hash);
	_gfx_mutex_unlock(&cache->lookupLock);

	if (elem != NULL)
	{
		_gfx_mutex_unlock(&cache->createLock);
		goto found;
	}

	// At this point we are the thread to actually create the new element.
	// We first create, then insert, so other threads don't accidentally
	// pick an incomplete element.
	_GFXCacheElem newElem;
	if (!_gfx_cache_create_elem(cache, &newElem, createInfo))
	{
		// Uh oh failed to create :(
		_gfx_mutex_unlock(&cache->createLock);
		free(key);
		return NULL;
	}

	// We created the thing, now insert the thing.
	// For this we block any lookups again.
	// When we're done we can also unlock for creation tho :)
	_gfx_mutex_lock(&cache->lookupLock);

	elem = gfx_map_hinsert(
		&cache->mutable, &newElem, _GFX_HASH_KEY_SIZE(key), key, hash);

	_gfx_mutex_unlock(&cache->lookupLock);
	_gfx_mutex_unlock(&cache->createLock);

	if (elem != NULL) goto found;

	// Ah, well, it is not in the map, away with it then...
	_gfx_cache_destroy_elem(cache, &newElem);
	free(key);
	return NULL;


	// Free data & return when found.
found:
	free(key);
	return elem;
}
