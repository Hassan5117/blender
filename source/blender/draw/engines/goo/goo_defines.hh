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
#define GOO_PROBE_UBO_SLOT 1
#define GOO_PASS_UBO_SLOT 2
#define GOO_LIGHT_UBO_SLOT 3
#define GOO_SHADOW_UBO_SLOT 4

/* SSBO slots (8-11 = draw_*). */
#define GOO_CRYPTO_BUF_SLOT 0
#define GOO_SPHERE_PROBE_BUF_SLOT 1
#define GOO_PLANAR_PROBE_BUF_SLOT 2
#define GOO_VOLUME_PROBE_BUF_SLOT 3
#define GOO_VOLUME_SH_BUF_SLOT 4

/* Image slots for render-pass / AOV / Cryptomatte & SSR G-buffer fragment writes. */
#define GOO_RP_COLOR_IMG_SLOT 0
#define GOO_RP_VALUE_IMG_SLOT 1
#define GOO_RP_CRYPTO_IMG_SLOT 2
#define GOO_SSR_DATA_IMG_SLOT 3
#define GOO_SSR_SPEC_IMG_SLOT 4
#define GOO_SSR_FALLBACK_IMG_SLOT 5

/* Pass-level sampler slots (2 = UtilityTexture, 3 = shadow atlas, 4 = prepass depth, 5 = color, 6 = shadow id, 7 = GTAO horizon, 8 = env probes, 9 = planar probes). */
#define GOO_SHADOW_TEX_SLOT 3
#define GOO_DEPTH_TEX_SLOT 4
#define GOO_SCENE_COLOR_TEX_SLOT 5
#define GOO_SHADOW_ID_TEX_SLOT 6
#define GOO_HORIZON_TEX_SLOT 7
#define GOO_ENV_PROBE_TEX_SLOT 8
#define GOO_PLANAR_TEX_SLOT 9

/* Render pass, AOV, and Cryptomatte limits & flags. */
#define GOO_AOV_MAX 16
#define GOO_CRYPTO_LEVELS_MAX 16
#define GOO_CRYPTO_FLAG_OBJECT (1 << 0)
#define GOO_CRYPTO_FLAG_ASSET (1 << 1)
#define GOO_CRYPTO_FLAG_MATERIAL (1 << 2)

#define GOO_DISPLAY_MODE_COMBINED 0
#define GOO_DISPLAY_MODE_COLOR_PASS 1
#define GOO_DISPLAY_MODE_VALUE_PASS 2
#define GOO_DISPLAY_MODE_DEPTH_PASS 3
#define GOO_DISPLAY_MODE_CRYPTO_PASS 4

/* Scene GTAO flags packed into `gtao_params.w`. */
#define GOO_GTAO_FLAG_ENABLED (1 << 0)
#define GOO_GTAO_FLAG_BENT_NORMALS (1 << 1)
#define GOO_GTAO_FLAG_BOUNCE (1 << 2)

/* Per-material pipeline flags pushed to shaders via `goo_material_flags`. */
#define GOO_MAT_FLAG_ALPHA_CLIP (1 << 0)
#define GOO_MAT_FLAG_ALPHA_HASHED (1 << 1)
#define GOO_MAT_FLAG_ALPHA_BLEND (1 << 2)
#define GOO_MAT_FLAG_CHECK_SHADOW_ID (1 << 3)
#define GOO_MAT_FLAG_NO_PASS_WRITE (1 << 4)
#define GOO_MAT_FLAG_SS_REFRACTION (1 << 5)
#define GOO_MAT_FLAG_IS_REFRACT_PASS (1 << 6)

/* Light probe, World IBL, LookDev, and SSR limits & flags. */
#define GOO_MAX_SPHERE_PROBES 16
#define GOO_MAX_PLANAR_PROBES 8
#define GOO_MAX_VOLUME_PROBES 8
#define GOO_MAX_VOLUME_SH_CELLS 1024

#define GOO_ENV_OCT_RES 256
#define GOO_ENV_MIPS 6
#define GOO_PLANAR_MIPS 5
#define GOO_COLOR_MIPS 6
#define GOO_HIZ_MIPS 6

#define GOO_CAPTURE_WORLD (1 << 0)
#define GOO_CAPTURE_SPHERE (1 << 1)
#define GOO_CAPTURE_PLANAR (1 << 2)
#define GOO_CAPTURE_VOLUME (1 << 3)
#define GOO_CAPTURE_CLIP_PLANE (1 << 4)
#define GOO_CAPTURE_NO_PASS_WRITE (1 << 5)
#define GOO_CAPTURE_NO_SSR (1 << 6)

#define GOO_SSR_FLAG_ENABLED (1 << 0)
#define GOO_SSR_FLAG_REFRACTION (1 << 1)
#define GOO_SSR_FLAG_HALFRES (1 << 2)

#define GOO_PROBE_SHAPE_ELLIPSOID 0
#define GOO_PROBE_SHAPE_BOX 1

#define GOO_MAX_LIGHTS 64
#define GOO_MAX_SHADOW_SLICES 32
#define GOO_SHADOW_RES 1024

#define GOO_LIGHT_SUN 0
#define GOO_LIGHT_POINT 1
#define GOO_LIGHT_SPOT 2
#define GOO_LIGHT_AREA 3
