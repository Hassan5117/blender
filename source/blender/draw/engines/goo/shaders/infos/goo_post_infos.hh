/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Post-process, GTAO, TAA, and Film Resolve/Overscan shader create-infos for Goo Engine.
 */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"

#  include "draw_view_infos.hh"
#  include "eevee_utility_tx.bsl.hh"
#  include "goo_shader_shared.hh"
#endif

#ifdef GLSL_CPP_STUBS
#  define STEP_ACCUM
#  define STEP_BLEND
#  define STEP_REPROJECT
#endif

#include "goo_defines.hh"
#include "gpu_shader_create_info.hh"

GPU_SHADER_INTERFACE_INFO(goo_fullscreen_iface)
SMOOTH(float2, screen_uv)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_CREATE_INFO(goo_fullscreen)
VERTEX_OUT(goo_fullscreen_iface)
VERTEX_SOURCE("goo_fullscreen_vert.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_effect_gtao)
TYPEDEF_SOURCE("goo_shader_shared.hh")
UNIFORM_BUF(GOO_LIGHT_UBO_SLOT, GooLightBlock, goo_light_buf)
SAMPLER(GOO_DEPTH_TEX_SLOT, sampler2DDepth, goo_depth_tx)
ADDITIONAL_INFO(UtilityTexture)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(goo_fullscreen)
FRAGMENT_OUT(0, float4, out_horizons)
FRAGMENT_SOURCE("goo_effect_gtao_frag.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_taa_accum)
SAMPLER(0, sampler2D, sample_tx)
PUSH_CONSTANT(float, sample_weight)
FRAGMENT_OUT(0, float4, out_color)
DEFINE("STEP_ACCUM")
ADDITIONAL_INFO(goo_fullscreen)
FRAGMENT_SOURCE("goo_effect_taa_frag.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_taa_blend)
SAMPLER(0, sampler2D, sample_tx)
SAMPLER(1, sampler2D, history_tx)
PUSH_CONSTANT(float, blend_alpha)
FRAGMENT_OUT(0, float4, out_color)
DEFINE("STEP_BLEND")
ADDITIONAL_INFO(goo_fullscreen)
FRAGMENT_SOURCE("goo_effect_taa_frag.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_taa_reproject)
SAMPLER(0, sampler2D, sample_tx)
SAMPLER(1, sampler2D, history_tx)
SAMPLER(2, sampler2DDepth, depth_tx)
PUSH_CONSTANT(float4x4, prev_persmat)
ADDITIONAL_INFO(draw_view)
FRAGMENT_OUT(0, float4, out_color)
DEFINE("STEP_REPROJECT")
ADDITIONAL_INFO(goo_fullscreen)
FRAGMENT_SOURCE("goo_effect_taa_frag.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_film_resolve)
SAMPLER(0, sampler2D, source_tx)
PUSH_CONSTANT(int2, overscan_offset)
FRAGMENT_OUT(0, float4, out_color)
ADDITIONAL_INFO(goo_fullscreen)
FRAGMENT_SOURCE("goo_film_resolve_frag.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_film_crop_depth)
SAMPLER(0, sampler2DDepth, depth_tx)
PUSH_CONSTANT(int2, overscan_offset)
DEPTH_WRITE(DepthWrite::ANY)
ADDITIONAL_INFO(goo_fullscreen)
FRAGMENT_SOURCE("goo_film_crop_depth_frag.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
