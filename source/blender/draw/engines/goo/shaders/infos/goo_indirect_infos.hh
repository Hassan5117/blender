/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Indirect lighting (World IBL, LookDev, Light Probes), color/Hi-Z pyramids, and SSR create-infos.
 */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"

#  include "draw_view_infos.hh"
#  include "eevee_utility_tx.bsl.hh"
#  include "goo_shader_shared.hh"
#endif

#include "goo_defines.hh"
#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(goo_probe_ubo)
TYPEDEF_SOURCE("goo_shader_shared.hh")
UNIFORM_BUF(GOO_PROBE_UBO_SLOT, GooProbeBlock, goo_probe_buf)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_indirect_read)
DEFINE("GOO_INDIRECT_READ")
ADDITIONAL_INFO(goo_probe_ubo)
STORAGE_BUF(GOO_SPHERE_PROBE_BUF_SLOT, read, GooSphereProbeData, goo_sphere_probe_buf[])
STORAGE_BUF(GOO_PLANAR_PROBE_BUF_SLOT, read, GooPlanarProbeData, goo_planar_probe_buf[])
STORAGE_BUF(GOO_VOLUME_PROBE_BUF_SLOT, read, GooVolumeProbeData, goo_volume_probe_buf[])
STORAGE_BUF(GOO_VOLUME_SH_BUF_SLOT, read, GooSHL2, goo_volume_sh_buf[])
SAMPLER(GOO_ENV_PROBE_TEX_SLOT, sampler2DArray, goo_env_probe_tx)
SAMPLER(GOO_PLANAR_TEX_SLOT, sampler2DArray, goo_planar_tx)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_ssr_write)
DEFINE("GOO_SSR_WRITE")
IMAGE(GOO_SSR_DATA_IMG_SLOT, SFLOAT_16_16_16_16, write, image2D, goo_ssr_data_img)
IMAGE(GOO_SSR_SPEC_IMG_SLOT, SFLOAT_16_16_16_16, write, image2D, goo_ssr_spec_img)
IMAGE(GOO_SSR_FALLBACK_IMG_SLOT, SFLOAT_16_16_16_16, write, image2D, goo_ssr_fallback_img)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_env_remap)
LOCAL_GROUP_SIZE(8, 8, 1)
SAMPLER(0, samplerCube, capture_cube_tx)
IMAGE(0, SFLOAT_16_16_16_16, write, image2DArray, env_mip0_img)
PUSH_CONSTANT(int, dst_layer)
PUSH_CONSTANT(int, dst_res)
PUSH_CONSTANT(float, indirect_clamp)
COMPUTE_SOURCE("goo_env_remap_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_env_convolve)
LOCAL_GROUP_SIZE(8, 8, 1)
SAMPLER(0, sampler2DArray, env_src_tx)
IMAGE(0, SFLOAT_16_16_16_16, write, image2DArray, env_dst_img)
PUSH_CONSTANT(int, layer)
PUSH_CONSTANT(int, src_res)
PUSH_CONSTANT(int, dst_res)
PUSH_CONSTANT(float, target_roughness)
COMPUTE_SOURCE("goo_env_convolve_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_cube_to_sh)
LOCAL_GROUP_SIZE(64, 1, 1)
TYPEDEF_SOURCE("goo_shader_shared.hh")
SAMPLER(0, samplerCube, capture_cube_tx)
STORAGE_BUF(0, write, GooSHL2, out_sh_buf[])
PUSH_CONSTANT(int, dst_index)
PUSH_CONSTANT(int, grid_dim)
PUSH_CONSTANT(float, indirect_clamp)
COMPUTE_SOURCE("goo_cube_to_sh_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_color_copy)
LOCAL_GROUP_SIZE(8, 8, 1)
SAMPLER(0, sampler2D, src_tx)
IMAGE(0, SFLOAT_16_16_16_16, write, image2D, dst_img)
PUSH_CONSTANT(int2, extent)
COMPUTE_SOURCE("goo_color_copy_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_color_downsample)
LOCAL_GROUP_SIZE(8, 8, 1)
SAMPLER(0, sampler2D, src_tx)
IMAGE(0, SFLOAT_16_16_16_16, write, image2D, dst_img)
PUSH_CONSTANT(int2, src_extent)
PUSH_CONSTANT(int2, dst_extent)
COMPUTE_SOURCE("goo_color_downsample_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_color_downsample_array)
LOCAL_GROUP_SIZE(8, 8, 1)
SAMPLER(0, sampler2DArray, src_tx)
IMAGE(0, SFLOAT_16_16_16_16, write, image2DArray, dst_img)
PUSH_CONSTANT(int2, src_extent)
PUSH_CONSTANT(int2, dst_extent)
PUSH_CONSTANT(int, layer)
COMPUTE_SOURCE("goo_color_downsample_array_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_hiz_copy)
LOCAL_GROUP_SIZE(8, 8, 1)
SAMPLER(0, sampler2DDepth, src_depth_tx)
IMAGE(0, SFLOAT_32, write, image2D, dst_hiz_img)
PUSH_CONSTANT(int2, extent)
COMPUTE_SOURCE("goo_hiz_copy_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_hiz_downsample)
LOCAL_GROUP_SIZE(8, 8, 1)
SAMPLER(0, sampler2D, src_hiz_tx)
IMAGE(0, SFLOAT_32, write, image2D, dst_hiz_img)
PUSH_CONSTANT(int2, src_extent)
PUSH_CONSTANT(int2, dst_extent)
COMPUTE_SOURCE("goo_hiz_downsample_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_ssr_trace)
LOCAL_GROUP_SIZE(8, 8, 1)
TYPEDEF_SOURCE("goo_shader_shared.hh")
ADDITIONAL_INFO(goo_probe_ubo)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(UtilityTexture)
UNIFORM_BUF(GOO_LIGHT_UBO_SLOT, GooLightBlock, goo_light_buf)
SAMPLER(3, sampler2D, hiz_depth_tx)
SAMPLER(4, sampler2D, ssr_data_tx)
SAMPLER(5, sampler2D, ssr_spec_tx)
IMAGE(0, SFLOAT_16_16_16_16, write, image2D, out_hit_img)
PUSH_CONSTANT(int2, render_extent)
PUSH_CONSTANT(int2, trace_extent)
PUSH_CONSTANT(int, half_res)
COMPUTE_SOURCE("goo_ssr_trace_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_ssr_resolve)
LOCAL_GROUP_SIZE(8, 8, 1)
TYPEDEF_SOURCE("goo_shader_shared.hh")
ADDITIONAL_INFO(goo_probe_ubo)
SAMPLER(0, sampler2D, scene_color_tx)
SAMPLER(1, sampler2D, ssr_data_tx)
SAMPLER(2, sampler2D, ssr_spec_tx)
SAMPLER(3, sampler2D, ssr_fallback_tx)
SAMPLER(4, sampler2D, ssr_hit_tx)
SAMPLER(5, sampler2D, hiz_depth_tx)
IMAGE(0, SFLOAT_16_16_16_16, write, image2D, out_color_img)
PUSH_CONSTANT(int2, render_extent)
PUSH_CONSTANT(int2, trace_extent)
PUSH_CONSTANT(int, half_res)
PUSH_CONSTANT(int, color_mips)
COMPUTE_SOURCE("goo_ssr_resolve_comp.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
