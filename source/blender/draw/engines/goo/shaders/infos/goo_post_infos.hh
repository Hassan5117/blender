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

GPU_SHADER_CREATE_INFO(goo_pass_accum)
LOCAL_GROUP_SIZE(8, 8, 1)
TYPEDEF_SOURCE("goo_shader_shared.hh")
UNIFORM_BUF(GOO_PASS_UBO_SLOT, GooPassBlock, goo_pass_buf)
ADDITIONAL_INFO(draw_view)
SAMPLER(0, sampler2DArray, sample_color_tx)
SAMPLER(1, sampler2DArray, sample_value_tx)
SAMPLER(2, sampler2D, sample_crypto_tx)
SAMPLER(3, sampler2DDepth, sample_depth_tx)
IMAGE(0, SFLOAT_32_32_32_32, read_write, image2DArray, accum_color_img)
IMAGE(1, SFLOAT_32, read_write, image2DArray, accum_value_img)
IMAGE(2, SFLOAT_32_32_32_32, read_write, image2DArray, accum_crypto_img)
PUSH_CONSTANT(int2, output_extent)
PUSH_CONSTANT(int2, overscan_offset)
PUSH_CONSTANT(int, sample_index)
PUSH_CONSTANT(float, sample_weight)
COMPUTE_SOURCE("goo_pass_accum_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_cryptomatte_sort)
LOCAL_GROUP_SIZE(8, 8, 1)
IMAGE(0, SFLOAT_32_32_32_32, read_write, image2DArray, accum_crypto_img)
PUSH_CONSTANT(int2, output_extent)
PUSH_CONSTANT(int, crypto_layer_count)
PUSH_CONSTANT(int, crypto_levels)
COMPUTE_SOURCE("goo_cryptomatte_sort_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_pass_display)
SAMPLER(0, sampler2DArray, accum_color_tx)
SAMPLER(1, sampler2DArray, accum_value_tx)
SAMPLER(2, sampler2DArray, accum_crypto_tx)
PUSH_CONSTANT(int, display_mode)
PUSH_CONSTANT(int, display_layer)
PUSH_CONSTANT(float, depth_scale)
FRAGMENT_OUT(0, float4, out_color)
ADDITIONAL_INFO(goo_fullscreen)
FRAGMENT_SOURCE("goo_pass_display_frag.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
