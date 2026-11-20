/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Shared constants and resource slot indices for C++ and GLSL shaders.
 */

#pragma once

/* UBO slots (0 = NodeTree, 5-11 = draw_*). */
#define GOO_LIGHT_UBO_SLOT 3
#define GOO_SHADOW_UBO_SLOT 4

/* Pass-level sampler slots (2 = UtilityTexture, 3 = shadow atlas, 4 = prepass depth, 5 = color). */
#define GOO_SHADOW_TEX_SLOT 3
#define GOO_DEPTH_TEX_SLOT 4
#define GOO_SCENE_COLOR_TEX_SLOT 5

#define GOO_MAX_LIGHTS 64
#define GOO_MAX_SHADOW_SLICES 32
#define GOO_SHADOW_RES 1024

#define GOO_LIGHT_SUN 0
#define GOO_LIGHT_POINT 1
#define GOO_LIGHT_SPOT 2
#define GOO_LIGHT_AREA 3
