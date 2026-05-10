/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <stdlib.h>
#include <string.h>


// Fixed hash sizes.
#define GFX_BUFFER_HASH_SIZE_ \
	(sizeof(GFXBuffer_*) + \
	sizeof(VkDeviceSize) /* offset */ + \
	sizeof(VkDeviceSize)) /* range */

#define GFX_IMAGE_HASH_SIZE_ \
	(sizeof(GFXImage_*) /* NULL if an attachment */ + \
	sizeof(size_t) /* SIZE_MAX if not an attachment */ + \
	sizeof(VkImageViewType) + \
	sizeof(VkFormat) + \
	sizeof(uint8_t) /* swizzle.r */ + \
	sizeof(uint8_t) /* swizzle.g */ + \
	sizeof(uint8_t) /* swizzle.b */ + \
	sizeof(uint8_t) /* swizzle.a */ + \
	sizeof(VkImageAspectFlags) + \
	sizeof(uint32_t) /* mipmap */ + \
	sizeof(uint32_t) /* numMipmaps */ + \
	sizeof(uint32_t) /* layer */ + \
	sizeof(uint32_t) /* numLayers */ + \
	sizeof(VkImageLayout))

#define GFX_SAMPLER_HASH_SIZE_ \
	(sizeof(GFXCacheElem_*))

#define GFX_VIEW_HASH_SIZE_ \
	(sizeof(GFXBuffer_*) + \
	sizeof(VkFormat) + \
	sizeof(VkDeviceSize) /* offset */ + \
	sizeof(VkDeviceSize)) /* range */


// Type checkers for Vulkan update info.
#define GFX_DESCRIPTOR_IS_BUFFER_(type) \
	((type) == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || \
	(type) == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)

#define GFX_DESCRIPTOR_IS_IMAGE_(type) \
	((type) == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || \
	(type) == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || \
	(type) == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)

#define GFX_DESCRIPTOR_IS_SAMPLER_(type) \
	((type) == VK_DESCRIPTOR_TYPE_SAMPLER || \
	(type) == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)

#define GFX_DESCRIPTOR_IS_VIEW_(type) \
	((type) == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || \
	(type) == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)


// Type checkers for groufix update info.
#define GFX_BINDING_IS_BUFFER_(type) \
	(GFX_DESCRIPTOR_IS_BUFFER_(type) || GFX_DESCRIPTOR_IS_VIEW_(type))

#define GFX_BINDING_IS_IMAGE_(type) \
	(GFX_DESCRIPTOR_IS_IMAGE_(type))

#define GFX_BINDING_IS_SAMPLER_(type) \
	(GFX_DESCRIPTOR_IS_SAMPLER_(type))


// Hash getters.
#define GFX_ENTRY_HASH_SIZE_(type) \
	((GFX_DESCRIPTOR_IS_BUFFER_(type) ? GFX_BUFFER_HASH_SIZE_ : 0) + \
	(GFX_DESCRIPTOR_IS_IMAGE_(type) ? GFX_IMAGE_HASH_SIZE_ : 0) + \
	(GFX_DESCRIPTOR_IS_SAMPLER_(type) ? GFX_SAMPLER_HASH_SIZE_ : 0) + \
	(GFX_DESCRIPTOR_IS_VIEW_(type) ? GFX_VIEW_HASH_SIZE_ : 0))

#define GFX_ENTRY_GET_HASH_(binding, entry) \
	(binding->hash + \
	GFX_ENTRY_HASH_SIZE_(binding->type) * (size_t)(entry - binding->entries))


// Hash writer.
#define GFX_WRITE_HASH_(hash, value) \
	do { \
		memcpy(hash, &(value), sizeof(value)); \
		hash += sizeof(value); \
	} while (0)


/****************************
 * Makes set resources stale, i.e. pushing them to the renderer for
 * destruction when they are no longer used by any virtual frames.
 * NOT thread-safe with respect gfx_renderer_(acquire|submit)!
 */
static void gfx_make_stale_(GFXSet* set, bool lock,
                            VkImageView imageView, VkBufferView bufferView)
{
	// gfx_push_stale_ expects at least one resource!
	if (imageView != VK_NULL_HANDLE || bufferView != VK_NULL_HANDLE)
	{
		// Explicitly not thread-safe, so we use the renderer's lock!
		// This should be a rare path to go down, given dynamic offsets or
		// alike are always prefered to updating sets.
		// So aggressive locking is fine.
		GFXRenderer* renderer = set->renderer;
		if (lock) gfx_mutex_lock_(&renderer->lock);

		gfx_push_stale_(renderer,
			VK_NULL_HANDLE, imageView, bufferView, VK_NULL_HANDLE);

		if (lock) gfx_mutex_unlock_(&renderer->lock);
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
static bool gfx_make_view_(GFXContext_* context,
                           const GFXSetBinding_* binding,
                           const GFXSetEntry_* entry,
                           VkImage image, VkFormat vkFmt, const GFXFormat* fmt,
                           VkImageView* view, VkImageLayout* layout,
                           VkImageViewCreateInfo* ivci)
{
	// Yeah go ahead and create an imag view...
	const GFXViewType viewType =
		// Only read the given view type if an attachment input!
		binding->type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ?
			entry->viewType : binding->viewType;

	// Fix aspect, cause we're nice :)
	const GFXImageAspect aspect =
		entry->range.aspect & GFX_IMAGE_ASPECT_FROM_FORMAT(*fmt);

	*ivci = (VkImageViewCreateInfo){
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,

		.pNext    = NULL,
		.flags    = 0,
		.image    = image,
		.viewType = GFX_GET_VK_IMAGE_VIEW_TYPE_(viewType),
		.format   = vkFmt,

		.components = {
			.r = GFX_GET_VK_COMPONENT_SWIZZLE_(entry->swizzle.r),
			.g = GFX_GET_VK_COMPONENT_SWIZZLE_(entry->swizzle.g),
			.b = GFX_GET_VK_COMPONENT_SWIZZLE_(entry->swizzle.b),
			.a = GFX_GET_VK_COMPONENT_SWIZZLE_(entry->swizzle.a)
		},

		.subresourceRange = {
			.aspectMask     = GFX_GET_VK_IMAGE_ASPECT_(aspect),
			.baseMipLevel   = entry->range.mipmap,
			.baseArrayLayer = entry->range.layer,

			.levelCount = entry->range.numMipmaps == 0 ?
				VK_REMAINING_MIP_LEVELS : entry->range.numMipmaps,
			.layerCount = entry->range.numLayers == 0 ?
				VK_REMAINING_ARRAY_LAYERS : entry->range.numLayers
		}
	};

	GFX_VK_CHECK_(
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
static void gfx_set_update_(GFXSet* set,
                            GFXSetBinding_* binding, GFXSetEntry_* entry)
{
	char* hash = GFX_ENTRY_GET_HASH_(binding, entry);

	// Update buffer info.
	if (GFX_DESCRIPTOR_IS_BUFFER_(binding->type))
	{
		GFXUnpackRef_ unp = gfx_ref_unpack_(entry->ref);
		if (unp.obj.buffer != NULL)
		{
			const uint64_t range =
				gfx_ref_size_(entry->ref) - entry->range.offset;
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
			GFX_WRITE_HASH_(hash, unp.obj.buffer);
			GFX_WRITE_HASH_(hash, entry->vk.update.buffer.offset);
			GFX_WRITE_HASH_(hash, entry->vk.update.buffer.range);
		}
	}

	// Update image info.
	if (GFX_DESCRIPTOR_IS_IMAGE_(binding->type))
	{
		GFXContext_* context = set->renderer->cache.context;

		// Make the previous image view stale.
		gfx_make_stale_(set, 1, entry->vk.update.image.imageView, VK_NULL_HANDLE);
		entry->vk.update.image.imageView = VK_NULL_HANDLE;
		entry->vk.update.image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		// Create new image view.
		// If referencing an attachment, leave empty values,
		// to be updated when used!
		GFXUnpackRef_ unp = gfx_ref_unpack_(entry->ref);
		if (unp.obj.image != NULL)
		{
			VkImageView view;
			VkImageLayout layout;
			VkImageViewCreateInfo ivci;

			if (gfx_make_view_(context,
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
			const uint8_t swizzleR = (uint8_t)ivci.components.r;
			const uint8_t swizzleG = (uint8_t)ivci.components.g;
			const uint8_t swizzleB = (uint8_t)ivci.components.b;
			const uint8_t swizzleA = (uint8_t)ivci.components.a;

			GFX_WRITE_HASH_(hash, unp.obj.image);
			GFX_WRITE_HASH_(hash, noIndex);
			GFX_WRITE_HASH_(hash, ivci.viewType);
			GFX_WRITE_HASH_(hash, ivci.format);
			GFX_WRITE_HASH_(hash, swizzleR);
			GFX_WRITE_HASH_(hash, swizzleG);
			GFX_WRITE_HASH_(hash, swizzleB);
			GFX_WRITE_HASH_(hash, swizzleA);
			GFX_WRITE_HASH_(hash, ivci.subresourceRange.aspectMask);
			GFX_WRITE_HASH_(hash, ivci.subresourceRange.baseMipLevel);
			GFX_WRITE_HASH_(hash, ivci.subresourceRange.levelCount);
			GFX_WRITE_HASH_(hash, ivci.subresourceRange.baseArrayLayer);
			GFX_WRITE_HASH_(hash, ivci.subresourceRange.layerCount);
			GFX_WRITE_HASH_(hash, layout);
		}
	}

	// Update sampler info.
	if (GFX_DESCRIPTOR_IS_SAMPLER_(binding->type))
	{
		GFXCacheElem_* sampler = entry->sampler;

		if (sampler == NULL)
		{
			// Get the default sampler.
			sampler = gfx_get_sampler_(set->renderer, NULL);
			if (sampler == NULL)
				gfx_log_error("Could not create default sampler for a set.");
		}

		if (sampler != NULL)
		{
			entry->vk.update.image.sampler = sampler->vk.sampler;

			// Update hash.
			GFX_WRITE_HASH_(hash, entry->sampler);
		}
	}

	// Update buffer view info.
	if (GFX_DESCRIPTOR_IS_VIEW_(binding->type))
	{
		GFXContext_* context = set->renderer->cache.context;

		// Make the previous buffer view stale.
		gfx_make_stale_(set, 1, VK_NULL_HANDLE, entry->vk.update.view);
		entry->vk.update.view = VK_NULL_HANDLE;

		// Create a new buffer view.
		GFXUnpackRef_ unp = gfx_ref_unpack_(entry->ref);
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
						gfx_ref_size_(entry->ref) - entry->range.offset :
						entry->range.size
			};

			VkBufferView view;
			GFX_VK_CHECK_(
				context->vk.CreateBufferView(
					context->vk.device, &bvci, NULL, &view),
				{
					gfx_log_error("Could not create buffer view for a set.");
					view = VK_NULL_HANDLE;
				});

			entry->vk.update.view = view;

			// Update hash.
			GFX_WRITE_HASH_(hash, unp.obj.buffer);
			GFX_WRITE_HASH_(hash, bvci.format);
			GFX_WRITE_HASH_(hash, bvci.offset);
			GFX_WRITE_HASH_(hash, bvci.range);
		}
	}
}

/****************************
 * Check if any Vulkan update info has become outdated because the referenced
 * attachment got rebuilt, and overwrites the current groufix update info.
 * @see gfx_set_update_, equivalent assumptions.
 */
static void gfx_set_update_attachs_(GFXSet* set)
{
	GFXRenderer* renderer = set->renderer;
	GFXContext_* context = renderer->cache.context;

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
		GFXSetBinding_* binding = &set->bindings[b];

		if (!GFX_DESCRIPTOR_IS_IMAGE_(binding->type))
			continue;

		if (binding->entries == NULL)
			continue;

		for (size_t e = 0; e < binding->count; ++e)
		{
			// We check against the packed reference type,
			// so we do not unnecessarily unpack.
			GFXSetEntry_* entry = &binding->entries[e];
			if (entry->ref.type != GFX_REF_ATTACHMENT)
				continue;

			// Ok we have an attachment descriptor.
			++attachCount;

			// Go check if we need to update.
			GFXUnpackRef_ unp = gfx_ref_unpack_(entry->ref);
			GFXImageAttach_* attach = GFX_UNPACK_REF_ATTACH_(unp);

			uint_least32_t gen =
				atomic_load_explicit(&entry->gen, memory_order_relaxed);

			if (attach == NULL || gen == GFX_ATTACH_GEN_(attach))
				goto next;

			// Ok at this point we have an attachment that is to be updated.
			// So let's first create a new image view, before locking.
			VkImageView view;
			VkImageLayout layout;
			VkImageViewCreateInfo ivci;

			bool success = gfx_make_view_(context,
				binding, entry,
				attach->vk.image, attach->vk.format,
				&attach->base.format, &view, &layout,
				&ivci);

			char* hash = GFX_ENTRY_GET_HASH_(binding, entry);

			// Ok we created a view, now we want to write it to the
			// Vulkan update info of the set.
			// Unfortunately multiple recorders could be recording with this
			// set that all try to simultaneously update attachments...
			// So we need to use the renderer's lock.
			// This is why we use the atomic generation, to skip this lock.
			// Unfortunately we want the info and generation update to be
			// one atomic operation, so we need to lock before updating gen.
			gfx_mutex_lock_(&renderer->lock);

			// Check again in case another thread just finished updating.
			gen = atomic_load_explicit(&entry->gen, memory_order_relaxed);

			if (gen == GFX_ATTACH_GEN_(attach))
			{
				gfx_make_stale_(set, 0, view, VK_NULL_HANDLE);
				goto unlock;
			}

			// Let's first make the previous image view stale.
			gfx_make_stale_(set, 0, entry->vk.update.image.imageView, VK_NULL_HANDLE);
			entry->vk.update.image.imageView = view;
			entry->vk.update.image.imageLayout = layout;

			// Update hash.
			const GFXImage_* noImage = NULL;
			const size_t backingInd = (size_t)unp.value;
			const uint8_t swizzleR = (uint8_t)ivci.components.r;
			const uint8_t swizzleG = (uint8_t)ivci.components.g;
			const uint8_t swizzleB = (uint8_t)ivci.components.b;
			const uint8_t swizzleA = (uint8_t)ivci.components.a;

			GFX_WRITE_HASH_(hash, noImage);
			GFX_WRITE_HASH_(hash, backingInd);
			GFX_WRITE_HASH_(hash, ivci.viewType);
			GFX_WRITE_HASH_(hash, ivci.format);
			GFX_WRITE_HASH_(hash, swizzleR);
			GFX_WRITE_HASH_(hash, swizzleG);
			GFX_WRITE_HASH_(hash, swizzleB);
			GFX_WRITE_HASH_(hash, swizzleA);
			GFX_WRITE_HASH_(hash, ivci.subresourceRange.aspectMask);
			GFX_WRITE_HASH_(hash, ivci.subresourceRange.baseMipLevel);
			GFX_WRITE_HASH_(hash, ivci.subresourceRange.levelCount);
			GFX_WRITE_HASH_(hash, ivci.subresourceRange.baseArrayLayer);
			GFX_WRITE_HASH_(hash, ivci.subresourceRange.layerCount);
			GFX_WRITE_HASH_(hash, layout);

			// Update the stored build generation last!
			gen = success ? GFX_ATTACH_GEN_(attach) : 0;
			atomic_store_explicit(&entry->gen, gen, memory_order_relaxed);

		unlock:
			gfx_mutex_unlock_(&renderer->lock);

			// Early exit when all attachments are found!
		next:
			if (attachCount >= set->numAttachs)
				return;
		}
	}
}

/****************************/
GFXPoolElem_* gfx_set_get_(GFXSet* set, GFXPoolSub_* sub)
{
	assert(set != NULL);
	assert(sub != NULL);

	// Update referenced renderer attachments!
	gfx_set_update_attachs_(set);

	// Get the descriptor set.
	GFXPoolElem_* elem = gfx_pool_get_(
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
static void gfx_set_recycle_(GFXSet* set)
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
		gfx_mutex_lock_(&renderer->lock);
		gfx_pool_recycle_(&renderer->pool, set->key, renderer->numFrames);
		gfx_mutex_unlock_(&renderer->lock);
	}
}

/****************************
 * Stand-in function for setting descriptor binding resources of the set.
 * @see gfx_set_resources.
 * @param update  Non-zero to update & recycle on change.
 * @param changed Outputs whether update info was actually changed.
 */
static bool gfx_set_resources_(GFXSet* set, bool update, bool* changed,
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
	bool recycled = 0;

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
		GFXSetBinding_* binding = &set->bindings[res->binding];
		if (
			GFX_REF_IS_NULL(res->ref) ||
			(GFX_REF_IS_BUFFER(res->ref) && !GFX_BINDING_IS_BUFFER_(binding->type)) ||
			(GFX_REF_IS_IMAGE(res->ref) && !GFX_BINDING_IS_IMAGE_(binding->type)))
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
		GFXSetEntry_* entry = &binding->entries[res->index];
		GFXUnpackRef_ cur = gfx_ref_unpack_(entry->ref);
		GFXUnpackRef_ new = gfx_ref_unpack_(res->ref);

		// Also a good place to do a quick context check.
		if (GFX_UNPACK_REF_CONTEXT_(new) != renderer->cache.context)
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
			GFX_UNPACK_REF_IS_EQUAL_(cur, new) &&
			cur.value == new.value &&
			gfx_ref_size_(entry->ref) == gfx_ref_size_(res->ref))
		{
			continue;
		}

		// Update the `numAttachs` field of the set.
		// Check the packed reference just like in gfx_set_update_attachs_.
		if (entry->ref.type == GFX_REF_ATTACHMENT) --set->numAttachs;
		if (res->ref.type == GFX_REF_ATTACHMENT) ++set->numAttachs;

		// Set the new reference & update.
		*changed = 1;
		entry->ref = res->ref;
		atomic_store_explicit(&entry->gen, 0, memory_order_relaxed);

		if (update)
		{
			// If not yet recycled, recycle the set,
			// we're possibly referencing resources that may be freed or sm.
			if (!recycled)
				gfx_set_recycle_(set),
				recycled = 1;

			gfx_set_update_(set, binding, entry);
		}
	}

	return success;
}

/****************************
 * Stand-in function for setting resource views of the set.
 * @see gfx_set_views.
 * @param update  Non-zero to update on change.
 * @param changed Outputs whether update info was actually changed.
 */
static bool gfx_set_views_(GFXSet* set, bool update, bool* changed,
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
	bool recycled = 0;

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
		GFXSetBinding_* binding = &set->bindings[view->binding];
		if (
			!GFX_BINDING_IS_BUFFER_(binding->type) &&
			!GFX_BINDING_IS_IMAGE_(binding->type))
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
		GFXSetEntry_* entry = &binding->entries[view->index];
		if (GFX_DESCRIPTOR_IS_VIEW_(binding->type))
		{
			VkFormat vkFmt = VK_FORMAT_UNDEFINED;
			GFXFormat gfxFmt = view->format;
			GFX_RESOLVE_FORMAT_(gfxFmt, vkFmt, renderer->heap->allocator.device,
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
		entry->swizzle = view->swizzle;
		entry->viewType = view->type;
		atomic_store_explicit(&entry->gen, 0, memory_order_relaxed);

		if (update)
		{
			// If not yet recycled, recycle the set,
			if (!recycled)
				gfx_set_recycle_(set),
				recycled = 1;

			gfx_set_update_(set, binding, entry);
		}
	}

	return success;
}

/****************************
 * Stand-in function for setting descriptor binding resources from groups.
 * @see gfx_set_groups.
 * @param update Non-zero to update on change.
 */
static bool gfx_set_groups_(GFXSet* set, bool update,
                            size_t numGroups, const GFXSetGroup* groups)
{
	assert(set != NULL);
	assert(!set->renderer->recording);
	assert(numGroups > 0);
	assert(groups != NULL);

	// Keep track of success.
	bool success = 1;
	bool recycled = 0;

	for (size_t g = 0; g < numGroups; ++g)
	{
		const GFXSetGroup* sGroup = &groups[g];
		GFXGroup_* group = (GFXGroup_*)sGroup->group;

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
			GFXSetBinding_* sBinding = &set->bindings[sGroup->binding + b];
			GFXBinding* gBinding = &group->bindings[sGroup->offset + b].base;

			// Check if the types match.
			// Note that we only have images and not-images.
			if (
				(gBinding->type == GFX_BINDING_IMAGE &&
					!GFX_BINDING_IS_IMAGE_(sBinding->type)) ||
				(gBinding->type != GFX_BINDING_IMAGE &&
					!GFX_BINDING_IS_BUFFER_(sBinding->type)))
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
				// gfx_set_resources_ and gfx_set_views_ validate it all.
				// Not possible from API to start index at > 0, but meh.
				GFXSetEntry_* entry = &sBinding->entries[i];

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
					if (!gfx_set_views_(set, 0, &vChanged, 1, &view))
						success = 0;

				if (!gfx_set_resources_(set, 0, &rChanged, 1, &setRes))
					success = 0;

				if (update && (vChanged || rChanged))
				{
					// If not yet recycled, recycle the set,
					if (!recycled)
						gfx_set_recycle_(set),
						recycled = 1;

					gfx_set_update_(set, sBinding, entry);
				}
			}
		}
	}

	return success;
}

/****************************
 * Stand-in function for setting immutable samplers of the set.
 * @see gfx_set_samplers.
 * @param update Non-zero to update on change.
 */
static bool gfx_set_samplers_(GFXSet* set, bool update,
                              size_t numSamplers, const GFXSampler* samplers)
{
	assert(set != NULL);
	assert(!set->renderer->recording);
	assert(numSamplers > 0);
	assert(samplers != NULL);

	GFXRenderer* renderer = set->renderer;

	// Keep track of success.
	bool success = 1;
	bool recycled = 0;

	for (size_t s = 0; s < numSamplers; ++s)
	{
		const GFXSampler* samp = &samplers[s];

		// Check if the sampler exists.
		if (
			samp->binding >= set->numBindings ||
			samp->index >= set->bindings[samp->binding].count ||
			!GFX_BINDING_IS_SAMPLER_(set->bindings[samp->binding].type))
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
		GFXSetBinding_* binding = &set->bindings[samp->binding];
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
		GFXCacheElem_* sampler = gfx_get_sampler_(renderer, samp);
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
		GFXSetEntry_* entry = &binding->entries[samp->index];
		if (entry->sampler == sampler)
			continue;

		// Set the new sampler & update.
		entry->sampler = sampler;

		if (update)
		{
			// If not yet recycled, recycle the set,
			if (!recycled)
				gfx_set_recycle_(set),
				recycled = 1;

			gfx_set_update_(set, binding, entry);
		}
	}

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
	gfx_tech_get_set_size_(technique, set, &numBindings, &numEntries);

	// Allocate a new set.
	const size_t structSize = GFX_ALIGN_UP(
		sizeof(GFXSet) + sizeof(GFXSetBinding_) * numBindings,
		alignof(GFXSetEntry_));

	const size_t updateSize = GFX_ALIGN_UP(
		structSize + sizeof(GFXSetEntry_) * numEntries,
		alignof(GFXHashKey_));

	const size_t maxHashSize =
		GFX_MAX(GFX_MAX(GFX_MAX(GFX_MAX(
			GFX_BUFFER_HASH_SIZE_,
			GFX_IMAGE_HASH_SIZE_),
			GFX_SAMPLER_HASH_SIZE_),
			(GFX_IMAGE_HASH_SIZE_ + GFX_SAMPLER_HASH_SIZE_)),
			GFX_VIEW_HASH_SIZE_);

	GFXSet* aset = malloc(
		updateSize +
		sizeof(GFXHashKey_) +
		sizeof(GFXCacheElem_*) + numEntries * maxHashSize);

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
	GFXHashKey_* key = (GFXHashKey_*)((char*)aset + updateSize);
	aset->key = key;
	key->len = sizeof(GFXCacheElem_*);
	memcpy(key->bytes, &aset->setLayout, sizeof(GFXCacheElem_*));
	memset(key->bytes + sizeof(GFXCacheElem_*), 0, numEntries * maxHashSize);

	// Get all the bindings.
	aset->first = numEntries > 0 ?
		(GFXSetEntry_*)((char*)aset + structSize) : NULL;

	GFXSetEntry_* entryPtr = aset->first;
	char* hashPtr = key->bytes + sizeof(GFXCacheElem_*);

	for (size_t b = 0; b < numBindings; ++b)
	{
		GFXSetBinding_* binding = &aset->bindings[b];

		size_t entries = 0;
		// If this returns 0, we do not use any update entries,
		// even though binding->count might be > 0!
		if (gfx_tech_get_set_binding_(technique, set, b, binding))
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

		const size_t hashLen = GFX_ENTRY_HASH_SIZE_(binding->type) * entries;
		entryPtr += entries;
		hashPtr += hashLen;
		key->len += hashLen;

		// Initialize entries to empty.
		for (size_t e = 0; e < entries; ++e)
		{
			GFXSetEntry_* entry = &binding->entries[e];
			entry->ref = GFX_REF_NULL;
			entry->swizzle = GFX_SWIZZLE_IDENTITY;
			entry->viewType = GFX_VIEW_2D;
			entry->sampler = NULL;
			entry->vk.format = VK_FORMAT_UNDEFINED;
			atomic_store_explicit(&entry->gen, 0, memory_order_relaxed);

			// Set range, leave undefined if only a sampler.
			if (GFX_BINDING_IS_BUFFER_(binding->type))
				entry->range = GFX_RANGE_WHOLE_BUFFER;

			else if (GFX_BINDING_IS_IMAGE_(binding->type))
				// Can specify all aspect flags, will be filtered later on.
				entry->range = GFX_RANGE_WHOLE_IMAGE;

			// Set update info.
			if (GFX_DESCRIPTOR_IS_BUFFER_(binding->type))
				entry->vk.update.buffer = (VkDescriptorBufferInfo){
					.buffer = VK_NULL_HANDLE,
					.offset = 0,
					.range = 0
				};

			else if(GFX_DESCRIPTOR_IS_VIEW_(binding->type))
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
		gfx_set_groups_(aset, 0, numGroups, groups);
	if (numResources > 0)
		gfx_set_resources_(aset, 0, &changed, numResources, resources);
	if (numViews > 0)
		gfx_set_views_(aset, 0, &changed, numViews, views);
	if (numSamplers > 0)
		gfx_set_samplers_(aset, 0, numSamplers, samplers);

	// And then loop over all things to manually update them.
	// Because all current handles are VK_NULL_HANDLE,
	// we do not push stales and we're still thread-safe :)
	for (size_t b = 0; b < numBindings; ++b)
	{
		GFXSetBinding_* binding = &aset->bindings[b];
		if (binding->entries != NULL) for (size_t e = 0; e < binding->count; ++e)
			gfx_set_update_(aset, binding, &binding->entries[e]);
	}

	// Link the set into the renderer.
	// Modifying the renderer, lock!
	gfx_mutex_lock_(&renderer->lock);
	gfx_list_insert_after(&renderer->sets, &aset->list, NULL);
	gfx_mutex_unlock_(&renderer->lock);

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
	gfx_mutex_lock_(&renderer->lock);

	// Unlink itself from the renderer.
	gfx_list_erase(&renderer->sets, &set->list);

	// We have to loop over all descriptors and
	// make the image and buffer views stale.
	// Keep the lock so gfx_make_stale_ doesn't repeatedly re-acquire.
	for (size_t b = 0; b < set->numBindings; ++b)
	{
		GFXSetBinding_* binding = &set->bindings[b];
		if (binding->entries != NULL) for (size_t e = 0; e < binding->count; ++e)
		{
			GFXSetEntry_* entry = &binding->entries[e];
			if (GFX_DESCRIPTOR_IS_IMAGE_(binding->type))
				gfx_make_stale_(set, 0, entry->vk.update.image.imageView, VK_NULL_HANDLE);
			else if (GFX_DESCRIPTOR_IS_VIEW_(binding->type))
				gfx_make_stale_(set, 0, VK_NULL_HANDLE, entry->vk.update.view);
		}
	}

	gfx_mutex_unlock_(&renderer->lock);

	// And recycle all matching descriptor sets,
	// none of the resources may be referenced anymore!
	gfx_set_recycle_(set);

	free(set);
}

/****************************/
GFX_API GFXRenderer* gfx_set_get_renderer(GFXSet* set)
{
	assert(set != NULL);

	return set->renderer;
}

/****************************/
GFX_API size_t gfx_set_get_num_bindings(GFXSet* set)
{
	assert(set != NULL);

	return set->numBindings;
}

/****************************/
GFX_API GFXShaderResourceType gfx_set_get_resource_type(GFXSet* set, size_t binding)
{
	assert(set != NULL);

	if (binding >= set->numBindings)
		return GFX_RESOURCE_UNKNOWN;

	// Report empty as unknown.
	if (set->bindings[binding].count == 0)
		return GFX_RESOURCE_UNKNOWN;

	switch (set->bindings[binding].type)
	{
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		return GFX_RESOURCE_BUFFER_UNIFORM;
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		return GFX_RESOURCE_BUFFER_STORAGE;
	case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		return GFX_RESOURCE_BUFFER_UNIFORM_TEXEL;
	case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
		return GFX_RESOURCE_BUFFER_STORAGE_TEXEL;
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		return GFX_RESOURCE_IMAGE_AND_SAMPLER;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		return GFX_RESOURCE_IMAGE_SAMPLED;
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		return GFX_RESOURCE_IMAGE_STORAGE;
	case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
		return GFX_RESOURCE_IMAGE_ATTACHMENT;
	case VK_DESCRIPTOR_TYPE_SAMPLER:
		return GFX_RESOURCE_SAMPLER;

	default:
		return GFX_RESOURCE_UNKNOWN;
	}
}

/****************************/
GFX_API size_t gfx_set_get_binding_size(GFXSet* set, size_t binding)
{
	assert(set != NULL);

	if (binding >= set->numBindings)
		return 0;

	return set->bindings[binding].count;
}

/****************************/
GFX_API size_t gfx_set_get_binding_block_size(GFXSet* set, size_t binding)
{
	assert(set != NULL);

	if (binding >= set->numBindings)
		return 0;

	return set->bindings[binding].size;
}

/****************************/
GFX_API bool gfx_set_is_binding_immutable(GFXSet* set, size_t binding)
{
	assert(set != NULL);

	if (binding >= set->numBindings)
		return 0;

	// If it is empty, do not report it as immutable.
	return
		set->bindings[binding].count > 0 &&
		set->bindings[binding].entries == NULL;
}

/****************************/
GFX_API bool gfx_set_is_binding_dynamic(GFXSet* set, size_t binding)
{
	assert(set != NULL);

	if (binding >= set->numBindings)
		return 0;

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
	return gfx_set_resources_(set, 1, &changed, numResources, resources);
}

/****************************/
GFX_API bool gfx_set_groups(GFXSet* set,
                            size_t numGroups, const GFXSetGroup* groups)
{
	// Relies on stand-in function for asserts.

	return gfx_set_groups_(set, 1, numGroups, groups);
}

/****************************/
GFX_API bool gfx_set_views(GFXSet* set,
                           size_t numViews, const GFXView* views)
{
	// Relies on stand-in function for asserts.

	bool changed; // Placeholder.
	return gfx_set_views_(set, 1, &changed, numViews, views);
}

/****************************/
GFX_API bool gfx_set_samplers(GFXSet* set,
                              size_t numSamplers, const GFXSampler* samplers)
{
	// Relies on stand-in function for asserts.

	return gfx_set_samplers_(set, 1, numSamplers, samplers);
}
