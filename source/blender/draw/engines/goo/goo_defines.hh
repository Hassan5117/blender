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

/* Pass-level sampler slots (2 = UtilityTexture, 3 = shadow atlas, 4 = prepass depth, 5 = color, 6 = shadow id, 7 = GTAO horizon). */
#define GOO_SHADOW_TEX_SLOT 3
#define GOO_DEPTH_TEX_SLOT 4
#define GOO_SCENE_COLOR_TEX_SLOT 5
#define GOO_SHADOW_ID_TEX_SLOT 6
#define GOO_HORIZON_TEX_SLOT 7

/* Scene GTAO flags packed into `gtao_params.w`. */
#define GOO_GTAO_FLAG_ENABLED (1 << 0)
#define GOO_GTAO_FLAG_BENT_NORMALS (1 << 1)
#define GOO_GTAO_FLAG_BOUNCE (1 << 2)

/* Per-material pipeline flags pushed to shaders via `goo_material_flags`. */
#define GOO_MAT_FLAG_ALPHA_CLIP (1 << 0)
#define GOO_MAT_FLAG_ALPHA_HASHED (1 << 1)
#define GOO_MAT_FLAG_ALPHA_BLEND (1 << 2)
#define GOO_MAT_FLAG_CHECK_SHADOW_ID (1 << 3)

#define GOO_MAX_LIGHTS 64
#define GOO_MAX_SHADOW_SLICES 32
#define GOO_SHADOW_RES 1024

#define GOO_LIGHT_SUN 0
#define GOO_LIGHT_POINT 1
#define GOO_LIGHT_SPOT 2
#define GOO_LIGHT_AREA 3
