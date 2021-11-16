/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_DEPS_H
#define GFX_CORE_DEPS_H

#include "groufix/core/device.h"
#include "groufix/core/refs.h"
#include "groufix/def.h"


/**
 * Synchronization access mask.
 */
typedef enum GFXAccessMask
{
	GFX_ACCESS_VERTEX_READ   = 0x000001,
	GFX_ACCESS_INDEX_READ    = 0x000002,
	GFX_ACCESS_UNIFORM_READ  = 0x000004,
	GFX_ACCESS_INDIRECT_READ = 0x000008,
	GFX_ACCESS_SAMPLED_READ  = 0x000010,

	GFX_ACCESS_STORAGE_READ     = 0x000020,
	GFX_ACCESS_STORAGE_WRITE    = 0x000040,
	GFX_ACCESS_ATTACHMENT_INPUT = 0x000080,
	GFX_ACCESS_ATTACHMENT_READ  = 0x000100, // Necessary for blending.
	GFX_ACCESS_ATTACHMENT_WRITE = 0x000200, // Necessary for depth/stencil testing.
	GFX_ACCESS_TRANSFER_READ    = 0x000400,
	GFX_ACCESS_TRANSFER_WRITE   = 0x000800,
	GFX_ACCESS_HOST_READ        = 0x001000,
	GFX_ACCESS_HOST_WRITE       = 0x002000,

	// Modifiers, meaningless without other flags.
	GFX_ACCESS_COMPUTE_ASYNC  = 0x004000,
	GFX_ACCESS_TRANSFER_ASYNC = 0x008000

} GFXAccessMask;


/**
 * Dependency object definition.
 * Stores transition & synchronization metadata.
 */
typedef struct GFXDependency GFXDependency;


/**
 * Creates a dependency object.
 * @param device NULL is equivalent to gfx_get_primary_device().
 * @return NULL on failure.
 */
GFX_API GFXDependency* gfx_create_dep(GFXDevice* device);

/**
 * Destroys a dependency object.
 * Undefined behaviour if destroyed when it holds metadata
 * about pairs of GPU operations that have not yet completed!
 */
GFX_API void gfx_destroy_dep(GFXDependency* dep);


/****************************
 * Dependency (synchronization) injection.
 ****************************/

/**
 * Dependency 'command' argument.
 */
typedef struct GFXDepArg
{
	// Synchronization type.
	enum
	{
		GFX_DEP_SIGNAL,
		GFX_DEP_SIGNAL_RANGE,
		GFX_DEP_WAIT,

	} type;


	// Object to inject a dependency in.
	GFXDependency* dep;

	// To-be synchronized resource (may be GFX_REF_NULL).
	GFXReference ref;

	// Affected resource range (ignored if not GFX_DEP_SIGNAL_RANGE).
	GFXRange range;

	// Access scope that will be signaled.
	GFXAccessMask mask;

} GFXDepArg;


/**
 * Dependency argument macros. Dependency objects can be signaled or waited for
 * with respect to (a set of) resources on the GPU, the CPU is never blocked!
 *
 * In order for resources to transition between different operations performed
 * on them, a dependency must be injected inbetween the two operations.
 * A dependency is formed by a pair of signal/wait commands.
 * If this is ignored, caches might not be flushed or invalidated, or worse,
 * the contents may be discarded by the engine and/or GPU when they see fit.
 *
 * To force synchronization on a specific resource, use
 *  gfx_dep_sigr and gfx_dep_waitr
 *
 * To limit synchronization to a range (area) of a resource, use
 *  gfx_dep_siga
 *
 * To apply both of the above simultaneously, use
 *  gfx_dep_sigra
 *
 * Functions that take a dependency argument are _always_ thread-safe with
 * respect to the dependency objects being referenced!
 */
#define gfx_dep_sig(dep_, mask_) \
	(GFXDepArg){ \
		.type = GFX_DEP_SIGNAL, \
		.dep = dep_, \
		.ref = GFX_REF_NULL, \
		.mask = mask_ \
	}

#define gfx_dep_sigr(dep_, mask_, ref_) \
	(GFXDepArg){ \
		.type = GFX_DEP_SIGNAL, \
		.dep = dep_, \
		.ref = ref_, \
		.mask = mask_ \
	}

#define gfx_dep_siga(dep_, mask_, range_) \
	(GFXDepArg){ \
		.type = GFX_DEP_SIGNAL_RANGE, \
		.dep = dep_, \
		.ref = GFX_REF_NULL, \
		.range = range_, \
		.mask = mask_ \
	}

#define gfx_dep_sigra(dep_, mask_, ref_, range_) \
	(GFXDepArg){ \
		.type = GFX_DEP_SIGNAL_RANGE, \
		.dep = dep_, \
		.ref = ref_, \
		.range = range_, \
		.mask = mask_ \
	}

#define gfx_dep_wait(dep_) \
	(GFXDepArg){ \
		.type = GFX_DEP_WAIT, \
		.dep = dep_, \
		.ref = GFX_REF_NULL \
	}

#define gfx_dep_waitr(dep_, ref_) \
	(GFXDepArg){ \
		.type = GFX_DEP_WAIT, \
		.dep = dep_, \
		.ref = ref_ \
	}


#endif
