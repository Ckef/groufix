/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_PARSERS_GLTF_H
#define GFX_PARSERS_GLTF_H

#include "groufix/containers/io.h"
#include "groufix/def.h"


/**
 * TODO: Define more.
 * Parses a glTF 2.0 stream into groufix objects.
 * @param src Source stream, cannot be NULL.
 * @return Non-zero on success.
 */
GFX_API bool gfx_load_gltf(const GFXReader* src);


#endif
