/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_ASSETS_IMAGE_H
#define GFX_ASSETS_IMAGE_H

#include "groufix/containers/io.h"
#include "groufix/core/deps.h"
#include "groufix/core/heap.h"
#include "groufix/def.h"


/**
 * Parses a JPG/PNG/BMP/TGA/GIF/HDR stream into a groufix image.
 * @param heap Heap to allocate images from, cannot be NULL.
 * @param dep  Dependency to inject signal commands in, cannot be NULL.
 * @param src  Source stream, cannot be NULL.
 * @return NULL on failure.
 */
GFX_API GFXImage* gfx_load_image(GFXHeap* heap, GFXDependency* dep,
                                 const GFXReader* src);


#endif
