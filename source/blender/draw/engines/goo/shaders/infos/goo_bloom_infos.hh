/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Ported from Goo Engine 4.4 `eevee_legacy_bloom_info.hh`. Uniform names are kept identical to
 * the original so the GLSL diff stays minimal. */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"

#  include "gpu_shader_fullscreen_infos.hh"
#endif

#ifdef GLSL_CPP_STUBS
#  define STEP_BLIT
#  define HIGH_QUALITY
#endif

#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(goo_bloom_common)
PUSH_CONSTANT(float2, sourceBufferTexelSize)
PUSH_CONSTANT(float4, curveThreshold)
PUSH_CONSTANT(float, clampIntensity)
PUSH_CONSTANT(float, sampleScale)
PUSH_CONSTANT(float3, bloomColor)
PUSH_CONSTANT(bool, bloomAddBase)
SAMPLER(0, sampler2D, sourceBuffer)
SAMPLER(1, sampler2D, baseBuffer)
FRAGMENT_OUT(0, float4, FragColor)
ADDITIONAL_INFO(gpu_fullscreen)
FRAGMENT_SOURCE("goo_effect_bloom_frag.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_bloom_blit_hq)
DEFINE("STEP_BLIT")
DEFINE("HIGH_QUALITY")
ADDITIONAL_INFO(goo_bloom_common)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_bloom_downsample_hq)
DEFINE("STEP_DOWNSAMPLE")
DEFINE("HIGH_QUALITY")
ADDITIONAL_INFO(goo_bloom_common)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_bloom_downsample)
DEFINE("STEP_DOWNSAMPLE")
ADDITIONAL_INFO(goo_bloom_common)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_bloom_upsample_hq)
DEFINE("STEP_UPSAMPLE")
DEFINE("HIGH_QUALITY")
ADDITIONAL_INFO(goo_bloom_common)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_bloom_resolve_hq)
DEFINE("STEP_RESOLVE")
DEFINE("HIGH_QUALITY")
ADDITIONAL_INFO(goo_bloom_common)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
