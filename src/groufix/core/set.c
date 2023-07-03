/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


// Fixed hash sizes.
#define _GFX_BUFFER_HASH_SIZE \
	(sizeof(_GFXBuffer*) + \
	sizeof(VkDeviceSize) /* offset */ + \
	sizeof(VkDeviceSize)) /* range */

#define _GFX_IMAGE_HASH_SIZE \
	(sizeof(_GFXImage*) /* NULL if an attachment */ + \
	sizeof(size_t) /* SIZE_MAX if not an attachment */ + \
	sizeof(VkImageViewType) + \
	sizeof(VkFormat) + \
	sizeof(VkImageAspectFlags) + \
	sizeof(uint32_t) /* mipmap */ + \
	sizeof(uint32_t) /* numMipmaps */ + \
	sizeof(uint32_t) /* layer */ + \
	sizeof(uint32_t) /* numLayers */ + \
	sizeof(VkImageLayout))

#define _GFX_SAMPLER_HASH_SIZE \
	(sizeof(_GFXCacheElem*))

#define _GFX_VIEW_HASH_SIZE \
	(sizeof(_GFXBuffer*) + \
	sizeof(VkFormat) + \
	sizeof(VkDeviceSize) /* offset */ + \
	sizeof(VkDeviceSize)) /* range */


// Type checkers for Vulkan update info.
#define _GFX_DESCRIPTOR_IS_BUFFER(type) \
	((type) == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || \
	(type) == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)

#define _GFX_DESCRIPTOR_IS_IMAGE(type) \
	((type) == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || \
	(type) == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || \
	(type) == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)

#define _GFX_DESCRIPTOR_IS_SAMPLER(type) \
	((type) == VK_DESCRIPTOR_TYPE_SAMPLER || \
	(type) == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)

#define _GFX_DESCRIPTOR_IS_VIEW(type) \
	((type) == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)


// Type checkers for groufix update info.
#define _GFX_BINDING_IS_BUFFER(type) \
	(_GFX_DESCRIPTOR_IS_BUFFER(type) || _GFX_DESCRIPTOR_IS_VIEW(type))

#define _GFX_BINDING_IS_IMAGE(type) \
	(_GFX_DESCRIPTOR_IS_IMAGE(type))

#define _GFX_BINDING_IS_SAMPLER(type) \
	(_GFX_DESCRIPTOR_IS_SAMPLER(type))


// Hash getters.
#define _GFX_ENTRY_HASH_SIZE(type) \
	((_GFX_DESCRIPTOR_IS_BUFFER(type) ? _GFX_BUFFER_HASH_SIZE : 0) + \
	(_GFX_DESCRIPTOR_IS_IMAGE(type) ? _GFX_IMAGE_HASH_SIZE : 0) + \
	(_GFX_DESCRIPTOR_IS_SAMPLER(type) ? _GFX_SAMPLER_HASH_SIZE : 0) + \
	(_GFX_DESCRIPTOR_IS_VIEW(type) ? _GFX_VIEW_HASH_SIZE : 0))

#define _GFX_ENTRY_GET_HASH(binding, entry) \
	(binding->hash + \
	_GFX_ENTRY_HASH_SIZE(binding->type) * (size_t)(entry - binding->entries))


/****************************
 * Makes set resources stale, i.e. pushing them to the renderer for
 * destruction when they are no longer used by any virtual frames.
 * NOT thread-safe with respect gfx_renderer_(acquire|submit)!
 */
static void _gfx_make_stale(GFXSet* set, bool lock,
                            VkImageView imageView, VkBufferView bufferView)
{
	// _gfx_push_stale expects at least one resource!
	if (imageView != VK_NULL_HANDLE || bufferView != VK_NULL_HANDLE)
	{
		// Explicitly not thread-safe, so we use the renderer's lock!
		// This should be a rare path to go down, given dynamic offsets or
		// alike are always prefered to updating sets.
		// So aggressive locking is fine.
		GFXRenderer* renderer = set->renderer;
		if (lock) _gfx_mutex_lock(&renderer->lock);

		_gfx_push_stale(renderer,
			VK_NULL_HANDLE, imageView, bufferView, VK_NULL_HANDLE);

		if (lock) _gfx_mutex_unlock(&renderer->lock);
	}
}

/****************************
 * Creates a Vulkan image view for the given image & update info.
 * Actual image and format are given instead of some reference.
 * @param view   Output image view, VK_NULL_HANDLE on failure.
 * @param layout Output image layout, VK_IMAGE_LAYOUT_UNDEFINED on failure.
 * @param ivci   Cannot be NULL, output, actual used info.
 * @return Zero on failure.
 */
static bool _gfx_make_view(_GFXContext* context,
                           const _GFXSetBinding* binding,
                           const _GFXSetEntry* entry,
                           VkImage image, VkFormat vkFmt, const GFXFormat* fmt,
                           VkImageView* view, VkImageLayout* layout,
                           VkImageViewCreateInfo* ivci)
{
	// Yeah go ahead and create an imag view...
	const GFXViewType viewType =
		// Only read the given view type if an attachment input!
		binding->type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ?
			entry->viewType : binding->viewType;

	const GFXImageAspect aspect =
		GFX_FORMAT_HAS_DEPTH_OR_STENCIL(*fmt) ?
			(GFX_FORMAT_HAS_DEPTH(*fmt) ? GFX_IMAGE_DEPTH : 0) |
			(GFX_FORMAT_HAS_STENCIL(*fmt) ? GFX_IMAGE_STENCIL : 0) :
			GFX_IMAGE_COLOR;

	*ivci = (VkImageViewCreateInfo){
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,

		.pNext    = NULL,
		.flags    = 0,
		.image    = image,
		.viewType = _GFX_GET_VK_IMAGE_VIEW_TYPE(viewType),
		.format   = vkFmt,

		.components = {
			.r = VK_COMPONENT_SWIZZLE_IDENTITY,
			.g = VK_COMPONENT_SWIZZLE_IDENTITY,
			.b = VK_COMPONENT_SWIZZLE_IDENTITY,
			.a = VK_COMPONENT_SWIZZLE_IDENTITY
		},

		.subresourceRange = {
			// Fix aspect, cause we're nice :)
			.aspectMask     = _GFX_GET_VK_IMAGE_ASPECT(entry->range.aspect & aspect),
			.baseMipLevel   = entry->range.mipmap,
			.baseArrayLayer = entry->range.layer,

			.levelCount = entry->range.numMipmaps == 0 ?
				VK_REMAINING_MIP_LEVELS : entry->range.numMipmaps,
			.layerCount = entry->range.numLayers == 0 ?
				VK_REMAINING_ARRAY_LAYERS : entry->range.numLayers
		}
	};

	_GFX_VK_CHECK(
		context->vk.CreateImageView(
			context->vk.device, ivci, NULL, view),
		{
			gfx_log_error("Could not create image view for a set.");
			*view = VK_NULL_HANDLE;
			*layout = VK_IMAGE_LAYOUT_UNDEFINED;

			return 0;
		});

	// ... and output some appropriate layout.
	*layout =
		// Guess the layout from the descriptor type.
		// TODO: Make some input somewhere so we can force a general layout?
		binding->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ?
			VK_IMAGE_LAYOUT_GENERAL :
			GFX_FORMAT_HAS_DEPTH_OR_STENCIL(*fmt) ?
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL :
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	return 1;
}

/****************************
 * Overwrites the Vulkan update info with the current groufix update info.
 * Assumes all relevant data is initialized and valid.
 * Will ignore valid empty values in the groufix update info.
 */
static void _gfx_set_update(GFXSet* set,
                            _GFXSetBinding* binding, _GFXSetEntry* entry)
{
	char* hash = _GFX_ENTRY_GET_HASH(binding, entry);

	// Update buffer info.
	if (_GFX_DESCRIPTOR_IS_BUFFER(binding->type))
	{
		_GFXUnpackRef unp = _gfx_ref_unpack(entry->ref);
		if (unp.obj.buffer != NULL)
		{
			const uint64_t range =
				_gfx_ref_size(entry->ref) - entry->range.offset;
			const uint64_t maxRange =
				(binding->size == 0) ? range : binding->size;

			entry->vk.update.buffer = (VkDescriptorBufferInfo){
				.buffer = unp.obj.buffer->vk.buffer,
				.offset = unp.value + entry->range.offset,
				.range =
					// Resolve zero buffer size.
					(entry->range.size == 0) ?
						GFX_MIN(range, maxRange) : entry->range.size
			};

			// Update hash.
			memcpy(hash, &unp.obj.buffer, sizeof(_GFXBuffer*));
			hash += sizeof(_GFXBuffer*);
			memcpy(hash, &entry->vk.update.buffer.offset, sizeof(VkDeviceSize));
			hash += sizeof(VkDeviceSize);
			memcpy(hash, &entry->vk.update.buffer.range, sizeof(VkDeviceSize));
			hash += sizeof(VkDeviceSize);
		}
	}

	// Update image info.
	if (_GFX_DESCRIPTOR_IS_IMAGE(binding->type))
	{
		_GFXContext* context = set->renderer->cache.context;

		// Make the previous image view stale.
		_gfx_make_stale(set, 1, entry->vk.update.image.imageView, VK_NULL_HANDLE);
		entry->vk.update.image.imageView = VK_NULL_HANDLE;
		entry->vk.update.image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		// Create new image view.
		// If referencing an attachment, leave empty values,
		// to be updated when used!
		_GFXUnpackRef unp = _gfx_ref_unpack(entry->ref);
		if (unp.obj.image != NULL)
		{
			VkImageView view;
			VkImageLayout layout;
			VkImageViewCreateInfo ivci;

			if (_gfx_make_view(context,
				binding, entry,
				unp.obj.image->vk.image, unp.obj.image->vk.format,
				&unp.obj.image->base.format, &view, &layout,
				&ivci))
			{
				entry->vk.update.image.imageView = view;
				entry->vk.update.image.imageLayout = layout;
			}

			// Update hash.
			const size_t noIndex = SIZE_MAX;

			memcpy(hash, &unp.obj.image, sizeof(_GFXImage*));
			hash += sizeof(_GFXImage*);
			memcpy(hash, &noIndex, sizeof(size_t));
			hash += sizeof(size_t);
			memcpy(hash, &ivci.viewType, sizeof(VkImageViewType));
			hash += sizeof(VkImageViewType);
			memcpy(hash, &ivci.format, sizeof(VkFormat));
			hash += sizeof(VkFormat);
			memcpy(hash, &ivci.subresourceRange.aspectMask, sizeof(VkImageAspectFlags));
			hash += sizeof(VkImageAspectFlags);
			memcpy(hash, &ivci.subresourceRange.baseMipLevel, sizeof(uint32_t));
			hash += sizeof(uint32_t);
			memcpy(hash, &ivci.subresourceRange.levelCount, sizeof(uint32_t));
			hash += sizeof(uint32_t);
			memcpy(hash, &ivci.subresourceRange.baseArrayLayer, sizeof(uint32_t));
			hash += sizeof(uint32_t);
			memcpy(hash, &ivci.subresourceRange.layerCount, sizeof(uint32_t));
			hash += sizeof(uint32_t);
			memcpy(hash, &layout, sizeof(VkImageLayout));
			hash += sizeof(VkImageLayout);
		}
	}

	// Update sampler info.
	if (_GFX_DESCRIPTOR_IS_SAMPLER(binding->type))
	{
		_GFXCacheElem* sampler = entry->sampler;

		if (sampler == NULL)
		{
			// Get the default sampler.
			sampler = _gfx_get_sampler(set->renderer, NULL);
			if (sampler == NULL)
				gfx_log_error("Could not create default sampler for a set.");
		}

		if (sampler != NULL)
		{
			entry->vk.update.image.sampler = sampler->vk.sampler;

			// Update hash.
			memcpy(hash, &entry->sampler, sizeof(_GFXCacheElem*));
			hash += sizeof(_GFXCacheElem*);
		}
	}

	// Update buffer view info.
	if (_GFX_DESCRIPTOR_IS_VIEW(binding->type))
	{
		_GFXContext* context = set->renderer->cache.context;

		// Make the previous buffer view stale.
		_gfx_make_stale(set, 1, VK_NULL_HANDLE, entry->vk.update.view);
		entry->vk.update.view = VK_NULL_HANDLE;

		// Create a new buffer view.
		_GFXUnpackRef unp = _gfx_ref_unpack(entry->ref);
		if (unp.obj.buffer != NULL && entry->vk.format != VK_FORMAT_UNDEFINED)
		{
			VkBufferViewCreateInfo bvci = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,

				.pNext  = NULL,
				.flags  = 0,
				.buffer = unp.obj.buffer->vk.buffer,
				.format = entry->vk.format,
				.offset = unp.value + entry->range.offset,
				.range =
					// Resolve zero buffer size.
					(entry->range.size == 0) ?
						_gfx_ref_size(entry->ref) - entry->range.offset :
						entry->range.size
			};

			VkBufferView view;
			_GFX_VK_CHECK(
				context->vk.CreateBufferView(
					context->vk.device, &bvci, NULL, &view),
				{
					gfx_log_error("Could not create buffer view for a set.");
					view = VK_NULL_HANDLE;
				});

			entry->vk.update.view = view;

			// Update hash.
			memcpy(hash, &unp.obj.buffer, sizeof(_GFXBuffer*));
			hash += sizeof(_GFXBuffer*);
			memcpy(hash, &bvci.format, sizeof(VkFormat));
			hash += sizeof(VkFormat);
			memcpy(hash, &bvci.offset, sizeof(VkDeviceSize));
			hash += sizeof(VkDeviceSize);
			memcpy(hash, &bvci.range, sizeof(VkDeviceSize));
			hash += sizeof(VkDeviceSize);
		}
	}
}

/****************************
 * Check if any Vulkan update info has become outdated because the referenced
 * attachment got rebuilt, and overwrites the current groufix update info.
 * @see _gfx_set_update, equivalent assumptions.
 */
static void _gfx_set_update_attachs(GFXSet* set)
{
	GFXRenderer* renderer = set->renderer;
	_GFXContext* context = renderer->cache.context;

	// Super early exit!
	if (set->numAttachs == 0)
		return;

	// Keep track of the number of attachments we encountered
	// so we can early exit slightly further on.
	size_t attachCount = 0;

	// Loop over all descriptors and filter out the attachment images.
	// We want to do as little work as possible here because this happens
	// every time we bind a set to a recorder.
	for (size_t b = 0; b < set->numBindings; ++b)
	{
		_GFXSetBinding* binding = &set->bindings[b];

		if (!_GFX_DESCRIPTOR_IS_IMAGE(binding->type))
			continue;

		if (binding->entries == NULL)
			continue;

		for (size_t e = 0; e < binding->count; ++e)
		{
			// We check against the packed reference type,
			// so we do not unnecessarily unpack.
			_GFXSetEntry* entry = &binding->entries[e];
			if (entry->ref.type != GFX_REF_ATTACHMENT)
				continue;

			// Ok we have an attachment descriptor.
			++attachCount;

			// Go check if we need to update.
			_GFXUnpackRef unp = _gfx_ref_unpack(entry->ref);
			_GFXImageAttach* attach = _GFX_UNPACK_REF_ATTACH(unp);

			uint_least32_t gen =
				atomic_load_explicit(&entry->gen, memory_order_relaxed);

			if (attach == NULL || gen == _GFX_ATTACH_GEN(attach))
				goto next;

			// Ok at this point we have an attachment that is to be updated.
			// So let's first create a new image view, before locking.
			VkImageView view;
			VkImageLayout layout;
			VkImageViewCreateInfo ivci;

			bool success = _gfx_make_view(context,
				binding, entry,
				attach->vk.image, attach->vk.format,
				&attach->base.format, &view, &layout,
				&ivci);

			char* hash = _GFX_ENTRY_GET_HASH(binding, entry);

			// Ok we created a view, now we want to write it to the
			// Vulkan update info of the set.
			// Unfortunately multiple recorders could be recording with this
			// set that all try to simultaneously update attachments...
			// So we need to use the renderer's lock.
			// This is why we use the atomic generation, to skip this lock.
			// Unfortunately we want the info and generation update to be
			// one atomic operation, so we need to lock before updating gen.
			_gfx_mutex_lock(&renderer->lock);

			// Check again in case another thread just finished updating.
			if (gen == _GFX_ATTACH_GEN(attach))
			{
				_gfx_make_stale(set, 0, view, VK_NULL_HANDLE);
				goto unlock;
			}

			// Let's first make the previous image view stale.
			_gfx_make_stale(set, 0, entry->vk.update.image.imageView, VK_NULL_HANDLE);
			entry->vk.update.image.imageView = view;
			entry->vk.update.image.imageLayout = layout;

			// Update hash.
			const _GFXImage* noImage = NULL;
			const size_t backingInd = (size_t)unp.value;

			memcpy(hash, &noImage, sizeof(_GFXImage*));
			hash += sizeof(_GFXImage*);
			memcpy(hash, &backingInd, sizeof(size_t));
			hash += sizeof(size_t);
			memcpy(hash, &ivci.viewType, sizeof(VkImageViewType));
			hash += sizeof(VkImageViewType);
			memcpy(hash, &ivci.format, sizeof(VkFormat));
			hash += sizeof(VkFormat);
			memcpy(hash, &ivci.subresourceRange.aspectMask, sizeof(VkImageAspectFlags));
			hash += sizeof(VkImageAspectFlags);
			memcpy(hash, &ivci.subresourceRange.baseMipLevel, sizeof(uint32_t));
			hash += sizeof(uint32_t);
			memcpy(hash, &ivci.subresourceRange.levelCount, sizeof(uint32_t));
			hash += sizeof(uint32_t);
			memcpy(hash, &ivci.subresourceRange.baseArrayLayer, sizeof(uint32_t));
			hash += sizeof(uint32_t);
			memcpy(hash, &ivci.subresourceRange.layerCount, sizeof(uint32_t));
			hash += sizeof(uint32_t);
			memcpy(hash, &layout, sizeof(VkImageLayout));
			hash += sizeof(VkImageLayout);

			// Update the stored build generation last!
			gen = success ? _GFX_ATTACH_GEN(attach) : 0;
			atomic_store_explicit(&entry->gen, gen, memory_order_relaxed);

		unlock:
			_gfx_mutex_unlock(&renderer->lock);

			// Early exit when all attachments are found!
		next:
			if (attachCount >= set->numAttachs)
				return;
		}
	}
}

/****************************/
_GFXPoolElem* _gfx_set_get(GFXSet* set, _GFXPoolSub* sub)
{
	assert(set != NULL);
	assert(sub != NULL);

	// Update referenced renderer attachments!
	_gfx_set_update_attachs(set);

	// Get the descriptor set.
	_GFXPoolElem* elem = _gfx_pool_get(
		&set->renderer->pool, sub,
		set->setLayout, set->key,
		set->first != NULL ? &set->first->vk.update : NULL);

	// Make sure to set the used flag on success.
	// This HAS to be atomic so multiple threads can record using this set!
	if (elem != NULL) atomic_store_explicit(&set->used, 1, memory_order_relaxed);
	return elem;
}

/****************************
 * Recycles all possible matching descriptor sets
 * that a set has queried from the renderer's pool.
 * Thread-safe outside recording!
 */
static void _gfx_set_recycle(GFXSet* set)
{
	// Only recycle if the set has been used & reset used flag.
	if (atomic_exchange_explicit(&set->used, 0, memory_order_relaxed))
	{
		GFXRenderer* renderer = set->renderer;

		// For the #flushes after which the set can be truly recycled,
		// note that the associated descriptor pool might be freed on
		// recycling as well.
		// Meaning: we are allowed to do this after all frames have synced.
		// This means the set itself is recycled 1 frame late because the
		// pool is only flushed at the end of a frame; acceptable.

		// Recycle all matching descriptor sets, this is explicitly NOT
		// thread-safe, so we use the renderer's lock!
		// Just like making the views stale, this should be a rare path to
		// go down to and aggressive locking is fine.
		_gfx_mutex_lock(&renderer->lock);
		_gfx_pool_recycle(&renderer->pool, set->key, renderer->numFrames);
		_gfx_mutex_unlock(&renderer->lock);
	}
}

/****************************
 * Stand-in function for setting descriptor binding resources of the set.
 * @see gfx_set_resources.
 * @param update  Non-zero to update & recycle on change.
 * @param changed Outputs whether update info was actually changed.
 */
static bool _gfx_set_resources(GFXSet* set, bool update, bool* changed,
                               size_t numResources, const GFXSetResource* resources)
{
	assert(set != NULL);
	assert(!set->renderer->recording);
	assert(changed != NULL);
	assert(numResources > 0);
	assert(resources != NULL);

	*changed = 0;
	GFXRenderer* renderer = set->renderer;

	// Keep track of success, much like the technique,
	// we skip over failures.
	bool success = 1;
	bool recycle = 0;

	for (size_t r = 0; r < numResources; ++r)
	{
		const GFXSetResource* res = &resources[r];

		// Check if the resource exists.
		if (
			res->binding >= set->numBindings ||
			res->index >= set->bindings[res->binding].count)
		{
			// Skip it if not.
			gfx_log_warn(
				"Could not set descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") of a set, "
				"does not exist.",
				res->binding, res->index);

			success = 0;
			continue;
		}

		// Check if the types match.
		_GFXSetBinding* binding = &set->bindings[res->binding];
		if (
			GFX_REF_IS_NULL(res->ref) ||
			(GFX_REF_IS_BUFFER(res->ref) && !_GFX_BINDING_IS_BUFFER(binding->type)) ||
			(GFX_REF_IS_IMAGE(res->ref) && !_GFX_BINDING_IS_IMAGE(binding->type)))
		{
			gfx_log_warn(
				"Could not set descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") of a set, "
				"incompatible resource type.",
				res->binding, res->index);

			success = 0;
			continue;
		}

		// Check if it is even a different reference.
		// For this we want to unpack the reference, as we want to check the
		// underlying resource, not the top-level reference.
		_GFXSetEntry* entry = &binding->entries[res->index];
		_GFXUnpackRef cur = _gfx_ref_unpack(entry->ref);
		_GFXUnpackRef new = _gfx_ref_unpack(res->ref);

		// Also a good place to do a quick context check.
		if (_GFX_UNPACK_REF_CONTEXT(new) != renderer->cache.context)
		{
			gfx_log_warn(
				"Could not set descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") of a set, "
				"resource must be built on the same logical Vulkan device.",
				res->binding, res->index);

			success = 0;
			continue;
		}

		// And a renderer check.
		if (new.obj.renderer != NULL && new.obj.renderer != renderer)
		{
			gfx_log_warn(
				"Could not set descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") of a set, "
				"renderer attachment reference cannot be used in another "
				"renderer.",
				res->binding, res->index);

			success = 0;
			continue;
		}

		// If equal (including offset & size), just skip it, not a failure.
		if (
			_GFX_UNPACK_REF_IS_EQUAL(cur, new) &&
			cur.value == new.value &&
			_gfx_ref_size(entry->ref) == _gfx_ref_size(res->ref))
		{
			continue;
		}

		// Update the `numAttachs` field of the set.
		// Check the packed reference just like in _gfx_set_update_attachs.
		if (entry->ref.type == GFX_REF_ATTACHMENT) --set->numAttachs;
		if (res->ref.type == GFX_REF_ATTACHMENT) ++set->numAttachs;

		// Set the new reference & update.
		*changed = 1;
		entry->ref = res->ref;
		atomic_store_explicit(&entry->gen, 0, memory_order_relaxed);

		if (update)
			_gfx_set_update(set, binding, entry),
			recycle = 1;
	}

	// If anything was updated, recycle the set,
	// we're possibily referencing resources that may be freed or smth.
	if (recycle) _gfx_set_recycle(set);

	return success;
}

/****************************
 * Stand-in function for setting resource views of the set.
 * @see gfx_set_views.
 * @param update  Non-zero to update on change.
 * @param changed Outputs whether update info was actually changed.
 */
static bool _gfx_set_views(GFXSet* set, bool update, bool* changed,
                           size_t numViews, const GFXView* views)
{
	assert(set != NULL);
	assert(!set->renderer->recording);
	assert(numViews > 0);
	assert(views != NULL);

	*changed = 0;
	GFXRenderer* renderer = set->renderer;

	// Keep track of success.
	bool success = 1;
	bool recycle = 0;

	for (size_t v = 0; v < numViews; ++v)
	{
		const GFXView* view = &views[v];

		// Check if the resource exists.
		if (
			view->binding >= set->numBindings ||
			view->index >= set->bindings[view->binding].count)
		{
			// Skip it if not.
			gfx_log_warn(
				"Could not set view of descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") of a set, "
				"does not exist.",
				view->binding, view->index);

			success = 0;
			continue;
		}

		// Check if is viewable (i.e. a buffer or image).
		_GFXSetBinding* binding = &set->bindings[view->binding];
		if (
			!_GFX_BINDING_IS_BUFFER(binding->type) &&
			!_GFX_BINDING_IS_IMAGE(binding->type))
		{
			gfx_log_warn(
				"Could not set view of descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") of a set, "
				"not a buffer or image.",
				view->binding, view->index);

			success = 0;
			continue;
		}

		// Resolve format here, as we do not store the groufix format.
		// Do not modify the entry before succesfully resolved!
		_GFXSetEntry* entry = &binding->entries[view->index];
		if (_GFX_DESCRIPTOR_IS_VIEW(binding->type))
		{
			VkFormat vkFmt = VK_FORMAT_UNDEFINED;
			GFXFormat gfxFmt = view->format;
			_GFX_RESOLVE_FORMAT(gfxFmt, vkFmt, renderer->heap->allocator.device,
				((VkFormatProperties){
					.linearTilingFeatures = 0,
					.optimalTilingFeatures = 0,
					.bufferFeatures =
						// Can only be a texel buffer at this point.
						binding->type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ?
							VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT :
							VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT
				}), {}); // Manually do the check so we can continue.

			if (vkFmt == VK_FORMAT_UNDEFINED)
			{
				gfx_log_warn(
					"Could not set view of descriptor resource "
					"(binding=%"GFX_PRIs", index=%"GFX_PRIs") of a set, "
					"texel buffer format is not supported.",
					view->binding, view->index);

				success = 0;
				continue;
			}

			// Set Vulkan format of entry :)
			entry->vk.format = vkFmt;
		}

		// Set the new values & update.
		*changed = 1;
		entry->range = view->range;
		entry->viewType = view->type;
		atomic_store_explicit(&entry->gen, 0, memory_order_relaxed);

		if (update)
			_gfx_set_update(set, binding, entry),
			recycle = 1;
	}

	// If anything was updated, recycle the set.
	if (recycle) _gfx_set_recycle(set);

	return success;
}

/****************************
 * Stand-in function for setting descriptor binding resources from groups.
 * @see gfx_set_groups.
 * @param update Non-zero to update on change.
 */
static bool _gfx_set_groups(GFXSet* set, bool update,
                            size_t numGroups, const GFXSetGroup* groups)
{
	assert(set != NULL);
	assert(!set->renderer->recording);
	assert(numGroups > 0);
	assert(groups != NULL);

	// Keep track of success.
	bool success = 1;
	bool recycle = 0;

	for (size_t g = 0; g < numGroups; ++g)
	{
		const GFXSetGroup* sGroup = &groups[g];
		_GFXGroup* group = (_GFXGroup*)sGroup->group;

		// Check if the resource exists (in both the set and group).
		if (
			sGroup->binding >= set->numBindings ||
			sGroup->offset >= group->numBindings)
		{
			// Skip it if not.
			gfx_log_warn(
				"Could not set descriptor resources (binding=%"GFX_PRIs") "
				"of a set from a resource group, does not exist.",
				sGroup->binding);

			success = 0;
			continue;
		}

		// Calculate how many bindings we can set from this group.
		size_t maxBindings =
			sGroup->numBindings == 0 ? SIZE_MAX : sGroup->numBindings;
		maxBindings =
			GFX_MIN(GFX_MIN(
				set->numBindings - sGroup->binding,
				group->numBindings - sGroup->offset),
				maxBindings);

		for (size_t b = 0; b < maxBindings; ++b)
		{
			_GFXSetBinding* sBinding = &set->bindings[sGroup->binding + b];
			GFXBinding* gBinding = &group->bindings[sGroup->offset + b];

			// Check if the types match.
			// Note that we only have images and not-images.
			if (
				(gBinding->type == GFX_BINDING_IMAGE &&
					!_GFX_BINDING_IS_IMAGE(sBinding->type)) ||
				(gBinding->type != GFX_BINDING_IMAGE &&
					!_GFX_BINDING_IS_BUFFER(sBinding->type)))
			{
				gfx_log_warn(
					"Could not set descriptor resources "
					"(binding=%"GFX_PRIs") of a set from a resource group, "
					"incompatible resource types.",
					sGroup->binding + b);

				success = 0;
				continue;
			}

			// Calculate how many descriptors we can set.
			size_t maxDescriptors =
				GFX_MIN(sBinding->count, gBinding->count);

			for (size_t i = 0; i < maxDescriptors; ++i)
			{
				// Try to set the resource.
				// And a view if we want to set a texel format!
				// Copy values from the group's binding, and let
				// _gfx_set_resources and _gfx_set_views validate it all.
				// Not possible from API to start index at > 0, but meh.
				_GFXSetEntry* entry = &sBinding->entries[i];

				GFXSetResource setRes = {
					.binding = sGroup->binding + b,
					.index = i,
					// Take the ref so size calculations are correct!
					.ref = gBinding->type == GFX_BINDING_IMAGE ?
						gfx_ref_group_image(group, sGroup->offset + b, i) :
						gfx_ref_group_buffer(group, sGroup->offset + b, i)
				};

				GFXView view = {
					.binding = sGroup->binding + b,
					.index = i,
					.format = gBinding->format,
					.range = entry->range // Don't modify the range!
				};

				// We do manually update and/or recycle, mostly
				// to avoid unnecessary re-recreation of Vulkan views.
				bool vChanged = 0, rChanged = 0;

				if (gBinding->type == GFX_BINDING_BUFFER_TEXEL)
					if (!_gfx_set_views(set, 0, &vChanged, 1, &view))
						success = 0;

				if (!_gfx_set_resources(set, 0, &rChanged, 1, &setRes))
					success = 0;

				if (update && (vChanged || rChanged))
					_gfx_set_update(set, sBinding, entry),
					recycle = 1;
			}
		}
	}

	// If anything was updated, recycle the set.
	if (recycle) _gfx_set_recycle(set);

	return success;
}

/****************************
 * Stand-in function for setting immutable samplers of the set.
 * @see gfx_set_samplers.
 * @param update Non-zero to update on change.
 */
static bool _gfx_set_samplers(GFXSet* set, bool update,
                              size_t numSamplers, const GFXSampler* samplers)
{
	assert(set != NULL);
	assert(!set->renderer->recording);
	assert(numSamplers > 0);
	assert(samplers != NULL);

	GFXRenderer* renderer = set->renderer;

	// Keep track of success.
	bool success = 1;
	bool recycle = 0;

	for (size_t s = 0; s < numSamplers; ++s)
	{
		const GFXSampler* samp = &samplers[s];

		// Check if the sampler exists.
		if (
			samp->binding >= set->numBindings ||
			samp->index >= set->bindings[samp->binding].count ||
			!_GFX_BINDING_IS_SAMPLER(set->bindings[samp->binding].type))
		{
			// Skip it if not.
			gfx_log_warn(
				"Could not set sampler of descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") of a set, "
				"does not exist.",
				samp->binding, samp->index);

			success = 0;
			continue;
		}

		// Check if the sampler is not immutable.
		// Note: It may still be immutable if it is a combined image/sampler,
		// in this case Vulkan should ignore the sampler handle anyway...
		_GFXSetBinding* binding = &set->bindings[samp->binding];
		if (binding->entries == NULL)
		{
			gfx_log_warn(
				"Could not set sampler of descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") of a set, "
				"is immutable.",
				samp->binding, samp->index);

			success = 0;
			continue;
		}

		// Create/get the sampler.
		_GFXCacheElem* sampler = _gfx_get_sampler(renderer, samp);
		if (sampler == NULL)
		{
			gfx_log_warn(
				"Failed to create sampler for descriptor resource "
				"(binding=%"GFX_PRIs", index=%"GFX_PRIs") of a set.",
				samp->binding, samp->index);

			success = 0;
			continue;
		}

		// If equal just skip it, not a failure.
		_GFXSetEntry* entry = &binding->entries[samp->index];
		if (entry->sampler == sampler)
			continue;

		// Set the new sampler & update manually.
		// We do it manually so we do not make any image view stale.
		entry->sampler = sampler;

		if (update)
			entry->vk.update.image.sampler = sampler->vk.sampler,
			recycle = 1;
	}

	// If anything was updated, recycle the set.
	if (recycle) _gfx_set_recycle(set);

	return success;
}

/****************************/
GFX_API GFXSet* gfx_renderer_add_set(GFXRenderer* renderer,
                                     GFXTechnique* technique, size_t set,
                                     size_t numResources, size_t numGroups,
                                     size_t numViews, size_t numSamplers,
                                     const GFXSetResource* resources,
                                     const GFXSetGroup* groups,
                                     const GFXView* views,
                                     const GFXSampler* samplers)
{
	assert(renderer != NULL);
	assert(!renderer->recording);
	assert(technique != NULL);
	assert(technique->renderer == renderer);
	assert(set < technique->numSets);
	assert(numResources == 0 || resources != NULL);
	assert(numGroups == 0 || groups != NULL);
	assert(numViews == 0 || views != NULL);
	assert(numSamplers == 0 || samplers != NULL);

	// First of all, make sure the technique is locked.
	if (!gfx_tech_lock(technique))
		goto error;

	// Get the number of bindings & entries to allocate.
	size_t numBindings;
	size_t numEntries;
	_gfx_tech_get_set_size(technique, set, &numBindings, &numEntries);

	// Allocate a new set.
	const size_t structSize = GFX_ALIGN_UP(
		sizeof(GFXSet) + sizeof(_GFXSetBinding) * numBindings,
		_Alignof(_GFXSetEntry));

	const size_t updateSize = GFX_ALIGN_UP(
		structSize + sizeof(_GFXSetEntry) * numEntries,
		_Alignof(_GFXHashKey));

	const size_t maxHashSize =
		GFX_MAX(GFX_MAX(GFX_MAX(GFX_MAX(
			_GFX_BUFFER_HASH_SIZE,
			_GFX_IMAGE_HASH_SIZE),
			_GFX_SAMPLER_HASH_SIZE),
			(_GFX_IMAGE_HASH_SIZE + _GFX_SAMPLER_HASH_SIZE)),
			_GFX_VIEW_HASH_SIZE);

	GFXSet* aset = malloc(
		updateSize +
		sizeof(_GFXHashKey) +
		sizeof(_GFXCacheElem*) + numEntries * maxHashSize);

	if (aset == NULL)
		goto error;

	// Initialize the set.
	aset->renderer = renderer;
	aset->setLayout = technique->setLayouts[set];
	aset->numAttachs = 0;
	aset->numDynamics = 0;
	aset->numBindings = numBindings;
	atomic_store_explicit(&aset->used, 0, memory_order_relaxed);

	// Setup hash key.
	_GFXHashKey* key = (_GFXHashKey*)((char*)aset + updateSize);
	aset->key = key;
	key->len = sizeof(_GFXCacheElem*);
	memcpy(key->bytes, &aset->setLayout, sizeof(_GFXCacheElem*));

	// Get all the bindings.
	aset->first = numEntries > 0 ?
		(_GFXSetEntry*)((char*)aset + structSize) : NULL;

	_GFXSetEntry* entryPtr = aset->first;
	char* hashPtr = key->bytes + sizeof(_GFXCacheElem*);

	for (size_t b = 0; b < numBindings; ++b)
	{
		_GFXSetBinding* binding = &aset->bindings[b];

		size_t entries = 0;
		// If this returns 0, we do not use any update entries,
		// even though binding->count might be > 0!
		if (_gfx_tech_get_set_binding(technique, set, b, binding))
			entries = binding->count;

		// Count number of dynamic buffers.
		if (
			binding->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
			binding->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
		{
			aset->numDynamics += entries;
		}

		binding->entries = entries > 0 ? entryPtr : NULL;
		binding->hash = entries > 0 ? hashPtr : NULL;

		const size_t hashLen = _GFX_ENTRY_HASH_SIZE(binding->type) * entries;
		entryPtr += entries;
		hashPtr += hashLen;
		key->len += hashLen;

		// Initialize entries to empty.
		for (size_t e = 0; e < entries; ++e)
		{
			_GFXSetEntry* entry = &binding->entries[e];
			entry->ref = GFX_REF_NULL;
			entry->viewType = GFX_VIEW_2D;
			entry->sampler = NULL;
			entry->vk.format = VK_FORMAT_UNDEFINED;
			atomic_store_explicit(&entry->gen, 0, memory_order_relaxed);

			// Set range, leave undefined if only a sampler.
			if (_GFX_BINDING_IS_BUFFER(binding->type))
				entry->range = (GFXRange){
					.offset = 0,
					.size = 0
				};

			else if (_GFX_BINDING_IS_IMAGE(binding->type))
				entry->range = (GFXRange){
					// Specify all aspect flags, will be filtered later on.
					.aspect = GFX_IMAGE_COLOR | GFX_IMAGE_DEPTH | GFX_IMAGE_STENCIL,
					.mipmap = 0,
					.numMipmaps = 0,
					.layer = 0,
					.numLayers = 0
				};

			// Set update info.
			if (_GFX_DESCRIPTOR_IS_BUFFER(binding->type))
				entry->vk.update.buffer = (VkDescriptorBufferInfo){
					.buffer = VK_NULL_HANDLE,
					.offset = 0,
					.range = 0
				};

			else if(_GFX_DESCRIPTOR_IS_VIEW(binding->type))
				entry->vk.update.view = VK_NULL_HANDLE;

			else
				// Else it's an image and/or sampler.
				entry->vk.update.image = (VkDescriptorImageInfo){
					.sampler = VK_NULL_HANDLE,
					.imageView = VK_NULL_HANDLE,
					.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED
				};
		}
	}

	// And finally, before finishing up, set all initial resources, groups,
	// views and samplers. Let individual resources and views overwrite groups.
	bool changed; // Placeholder.

	if (numGroups > 0)
		_gfx_set_groups(aset, 0, numGroups, groups);
	if (numResources > 0)
		_gfx_set_resources(aset, 0, &changed, numResources, resources);
	if (numViews > 0)
		_gfx_set_views(aset, 0, &changed, numViews, views);
	if (numSamplers > 0)
		_gfx_set_samplers(aset, 0, numSamplers, samplers);

	// And then loop over all things to manually update them.
	// Because all current handles are VK_NULL_HANDLE,
	// we do not push stales and we're still thread-safe :)
	for (size_t b = 0; b < numBindings; ++b)
	{
		_GFXSetBinding* binding = &aset->bindings[b];
		if (binding->entries != NULL) for (size_t e = 0; e < binding->count; ++e)
			_gfx_set_update(aset, binding, &binding->entries[e]);
	}

	// Link the set into the renderer.
	// Modifying the renderer, lock!
	_gfx_mutex_lock(&renderer->lock);
	gfx_list_insert_after(&renderer->sets, &aset->list, NULL);
	_gfx_mutex_unlock(&renderer->lock);

	return aset;


	// Error on failure.
error:
	gfx_log_error("Could not add a new set to a renderer.");
	return NULL;
}

/****************************/
GFX_API void gfx_erase_set(GFXSet* set)
{
	assert(set != NULL);
	assert(!set->renderer->recording);

	GFXRenderer* renderer = set->renderer;

	// Modifying the renderer, lock!
	_gfx_mutex_lock(&renderer->lock);

	// Unlink itself from the renderer.
	gfx_list_erase(&renderer->sets, &set->list);

	// We have to loop over all descriptors and
	// make the image and buffer views stale.
	// Keep the lock so _gfx_make_stale doesn't repeatedly re-acquire.
	for (size_t b = 0; b < set->numBindings; ++b)
	{
		_GFXSetBinding* binding = &set->bindings[b];
		if (binding->entries != NULL) for (size_t e = 0; e < binding->count; ++e)
		{
			_GFXSetEntry* entry = &binding->entries[e];
			if (_GFX_DESCRIPTOR_IS_IMAGE(binding->type))
				_gfx_make_stale(set, 0, entry->vk.update.image.imageView, VK_NULL_HANDLE);
			else if (_GFX_DESCRIPTOR_IS_VIEW(binding->type))
				_gfx_make_stale(set, 0, VK_NULL_HANDLE, entry->vk.update.view);
		}
	}

	_gfx_mutex_unlock(&renderer->lock);

	// And recycle all matching descriptor sets,
	// none of the resources may be referenced anymore!
	_gfx_set_recycle(set);

	free(set);
}

/****************************/
GFX_API size_t gfx_set_get_num_bindings(GFXSet* set)
{
	assert(set != NULL);

	return set->numBindings;
}

/****************************/
GFX_API GFXBindingType gfx_set_get_binding_type(GFXSet* set, size_t binding)
{
	assert(set != NULL);
	assert(binding < set->numBindings);

	VkDescriptorType type = set->bindings[binding].type;

	return
		_GFX_DESCRIPTOR_IS_BUFFER(type) ?
			GFX_BINDING_BUFFER :
		_GFX_DESCRIPTOR_IS_VIEW(type) ?
			GFX_BINDING_BUFFER_TEXEL :
		_GFX_DESCRIPTOR_IS_IMAGE(type) && _GFX_DESCRIPTOR_IS_SAMPLER(type) ?
			GFX_BINDING_IMAGE_AND_SAMPLER :
		_GFX_DESCRIPTOR_IS_IMAGE(type) ?
			GFX_BINDING_IMAGE :
			GFX_BINDING_SAMPLER;
}

/****************************/
GFX_API size_t gfx_set_get_binding_size(GFXSet* set, size_t binding)
{
	assert(set != NULL);
	assert(binding < set->numBindings);

	return set->bindings[binding].count;
}

/****************************/
GFX_API size_t gfx_set_get_binding_block_size(GFXSet* set, size_t binding)
{
	assert(set != NULL);
	assert(binding < set->numBindings);

	return set->bindings[binding].size;
}

/****************************/
GFX_API bool gfx_set_is_binding_immutable(GFXSet* set, size_t binding)
{
	assert(set != NULL);
	assert(binding < set->numBindings);

	// If it is empty, do not report it as immutable.
	return
		set->bindings[binding].count > 0 &&
		set->bindings[binding].entries == NULL;
}

/****************************/
GFX_API bool gfx_set_is_binding_dynamic(GFXSet* set, size_t binding)
{
	assert(set != NULL);
	assert(binding < set->numBindings);

	// If it is empty, do not report it as dynamic.
	return
		// Only place we retrieve this, so just do it inline..
		set->bindings[binding].count > 0 &&
		(set->bindings[binding].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
		set->bindings[binding].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
}

/****************************/
GFX_API size_t gfx_set_get_num_dynamics(GFXSet* set)
{
	assert(set != NULL);

	return set->numDynamics;
}

/****************************/
GFX_API bool gfx_set_resources(GFXSet* set,
                               size_t numResources, const GFXSetResource* resources)
{
	// Relies on stand-in function for asserts.

	bool changed; // Placeholder.
	return _gfx_set_resources(set, 1, &changed, numResources, resources);
}

/****************************/
GFX_API bool gfx_set_groups(GFXSet* set,
                            size_t numGroups, const GFXSetGroup* groups)
{
	// Relies on stand-in function for asserts.

	return _gfx_set_groups(set, 1, numGroups, groups);
}

/****************************/
GFX_API bool gfx_set_views(GFXSet* set,
                           size_t numViews, const GFXView* views)
{
	// Relies on stand-in function for asserts.

	bool changed; // Placeholder.
	return _gfx_set_views(set, 1, &changed, numViews, views);
}

/****************************/
GFX_API bool gfx_set_samplers(GFXSet* set,
                              size_t numSamplers, const GFXSampler* samplers)
{
	// Relies on stand-in function for asserts.

	return _gfx_set_samplers(set, 1, numSamplers, samplers);
}
