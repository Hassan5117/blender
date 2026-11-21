/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Goo material pipeline create-infos.
 *
 * The material create-info produced by GPU codegen (node tree UBO, textures, vertex attributes)
 * is amended at runtime with `goo_material_surface_mesh` (see `goo_material.cc`).
 */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"

#  include "draw_object_infos_infos.hh"
#  include "draw_view_infos.hh"

#  include "eevee_utility_tx.bsl.hh"
#  include "goo_indirect_infos.hh"
#  include "goo_nodetree_type_lib.glsl"
#  include "goo_shader_shared.hh"
#endif

#include "goo_defines.hh"
#include "gpu_shader_create_info.hh"

GPU_SHADER_NAMED_INTERFACE_INFO(goo_geom_iface, interp)
SMOOTH(float3, P)
SMOOTH(float3, N)
GPU_SHADER_NAMED_INTERFACE_END(interp)

GPU_SHADER_NAMED_INTERFACE_INFO(goo_geom_curves_iface, curve_interp)
SMOOTH(float3, tangent)
SMOOTH(float3, binormal)
SMOOTH(float, time_width)
SMOOTH(float, radius)
SMOOTH(float, point_id)
GPU_SHADER_NAMED_INTERFACE_END(curve_interp)

GPU_SHADER_NAMED_INTERFACE_INFO(goo_geom_curves_flat_iface, curve_interp_flat)
FLAT(int, strand_id)
GPU_SHADER_NAMED_INTERFACE_END(curve_interp_flat)

/* Render passes, Shader AOVs, and Cryptomatte outputs written from surface/world shaders. */
GPU_SHADER_CREATE_INFO(goo_render_passes)
TYPEDEF_SOURCE("goo_shader_shared.hh")
DEFINE("GOO_PASS_WRITE")
UNIFORM_BUF(GOO_PASS_UBO_SLOT, GooPassBlock, goo_pass_buf)
STORAGE_BUF(GOO_CRYPTO_BUF_SLOT, read, float2, goo_crypto_object_buf[])
IMAGE(GOO_RP_COLOR_IMG_SLOT, SFLOAT_16_16_16_16, write, image2DArray, goo_rp_color_img)
IMAGE(GOO_RP_VALUE_IMG_SLOT, SFLOAT_32, write, image2DArray, goo_rp_value_img)
IMAGE(GOO_RP_CRYPTO_IMG_SLOT, SFLOAT_32_32_32_32, write, image2D, goo_rp_crypto_img)
PUSH_CONSTANT(float, goo_material_hash)
GPU_SHADER_CREATE_END()

/* Scene lighting + shadow maps + screenspace buffers. */
GPU_SHADER_CREATE_INFO(goo_lighting)
TYPEDEF_SOURCE("goo_shader_shared.hh")
UNIFORM_BUF(GOO_LIGHT_UBO_SLOT, GooLightBlock, goo_light_buf)
UNIFORM_BUF(GOO_SHADOW_UBO_SLOT, GooShadowBlock, goo_shadow_buf)
SAMPLER(GOO_SHADOW_TEX_SLOT, sampler2DArrayDepth, goo_shadow_tx)
SAMPLER(GOO_DEPTH_TEX_SLOT, sampler2DDepth, goo_depth_tx)
SAMPLER(GOO_SCENE_COLOR_TEX_SLOT, sampler2D, goo_scene_color_tx)
SAMPLER(GOO_SHADOW_ID_TEX_SLOT, usampler2DArray, goo_shadow_id_tx)
SAMPLER(GOO_HORIZON_TEX_SLOT, sampler2D, goo_horizon_tx)
PUSH_CONSTANT(int4, goo_light_groups)
PUSH_CONSTANT(int4, goo_light_group_shadows)
PUSH_CONSTANT(float, goo_alpha_threshold)
PUSH_CONSTANT(int, goo_material_flags)
PUSH_CONSTANT(float, goo_refraction_depth)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_material_surface_mesh)
VERTEX_IN(0, float3, pos)
VERTEX_IN(1, float3, nor)
VERTEX_OUT(goo_geom_iface)
FRAGMENT_OUT(0, float4, out_color)
VERTEX_SOURCE("goo_material_surface_vert.glsl")
FRAGMENT_SOURCE("goo_material_surface_frag.glsl")
BUILTINS(BuiltinBits::CLIP_DISTANCES)
DEFINE("USE_WORLD_CLIP_PLANES")
ADDITIONAL_INFO(goo_lighting)
ADDITIONAL_INFO(goo_indirect_read)
ADDITIONAL_INFO(goo_ssr_write)
ADDITIONAL_INFO(goo_render_passes)
ADDITIONAL_INFO(UtilityTexture)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_object_infos)
ADDITIONAL_INFO(draw_resource_id_varying)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_material_prepass_mesh)
VERTEX_IN(0, float3, pos)
VERTEX_IN(1, float3, nor)
VERTEX_OUT(goo_geom_iface)
VERTEX_SOURCE("goo_material_surface_vert.glsl")
FRAGMENT_SOURCE("goo_material_prepass_frag.glsl")
BUILTINS(BuiltinBits::CLIP_DISTANCES)
DEFINE("USE_WORLD_CLIP_PLANES")
ADDITIONAL_INFO(goo_lighting)
ADDITIONAL_INFO(goo_probe_ubo)
ADDITIONAL_INFO(UtilityTexture)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_object_infos)
ADDITIONAL_INFO(draw_resource_id_varying)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_material_shadow_mesh)
VERTEX_IN(0, float3, pos)
VERTEX_IN(1, float3, nor)
VERTEX_OUT(goo_geom_iface)
FRAGMENT_OUT(0, uint, out_shadow_id)
VERTEX_SOURCE("goo_material_surface_vert.glsl")
FRAGMENT_SOURCE("goo_material_shadow_frag.glsl")
ADDITIONAL_INFO(goo_lighting)
ADDITIONAL_INFO(UtilityTexture)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_object_infos)
ADDITIONAL_INFO(draw_resource_id_varying)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_material_surface_curves)
VERTEX_OUT(goo_geom_iface)
VERTEX_OUT(goo_geom_curves_iface)
VERTEX_OUT(goo_geom_curves_flat_iface)
FRAGMENT_OUT(0, float4, out_color)
DEFINE("MAT_GEOM_CURVES")
VERTEX_SOURCE("goo_material_curves_vert.glsl")
FRAGMENT_SOURCE("goo_material_curves_frag.glsl")
BUILTINS(BuiltinBits::CLIP_DISTANCES)
DEFINE("USE_WORLD_CLIP_PLANES")
ADDITIONAL_INFO(goo_lighting)
ADDITIONAL_INFO(goo_indirect_read)
ADDITIONAL_INFO(goo_ssr_write)
ADDITIONAL_INFO(goo_render_passes)
ADDITIONAL_INFO(UtilityTexture)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_object_infos)
ADDITIONAL_INFO(draw_resource_id_varying)
ADDITIONAL_INFO(draw_curves)
ADDITIONAL_INFO(draw_curves_infos)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_material_prepass_curves)
VERTEX_OUT(goo_geom_iface)
VERTEX_OUT(goo_geom_curves_iface)
VERTEX_OUT(goo_geom_curves_flat_iface)
DEFINE("MAT_GEOM_CURVES")
VERTEX_SOURCE("goo_material_curves_vert.glsl")
FRAGMENT_SOURCE("goo_material_prepass_curves_frag.glsl")
BUILTINS(BuiltinBits::CLIP_DISTANCES)
DEFINE("USE_WORLD_CLIP_PLANES")
ADDITIONAL_INFO(goo_lighting)
ADDITIONAL_INFO(goo_probe_ubo)
ADDITIONAL_INFO(UtilityTexture)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_object_infos)
ADDITIONAL_INFO(draw_resource_id_varying)
ADDITIONAL_INFO(draw_curves)
ADDITIONAL_INFO(draw_curves_infos)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_material_shadow_curves)
VERTEX_OUT(goo_geom_iface)
VERTEX_OUT(goo_geom_curves_iface)
VERTEX_OUT(goo_geom_curves_flat_iface)
FRAGMENT_OUT(0, uint, out_shadow_id)
DEFINE("MAT_GEOM_CURVES")
DEFINE("MAT_SHADOW")
VERTEX_SOURCE("goo_material_curves_vert.glsl")
FRAGMENT_SOURCE("goo_material_shadow_curves_frag.glsl")
ADDITIONAL_INFO(goo_lighting)
ADDITIONAL_INFO(UtilityTexture)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_object_infos)
ADDITIONAL_INFO(draw_resource_id_varying)
ADDITIONAL_INFO(draw_curves)
ADDITIONAL_INFO(draw_curves_infos)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_material_world)
VERTEX_OUT(goo_geom_iface)
FRAGMENT_OUT(0, float4, out_color)
VERTEX_SOURCE("goo_material_world_vert.glsl")
FRAGMENT_SOURCE("goo_material_world_frag.glsl")
ADDITIONAL_INFO(goo_lighting)
ADDITIONAL_INFO(goo_indirect_read)
ADDITIONAL_INFO(goo_render_passes)
ADDITIONAL_INFO(UtilityTexture)
ADDITIONAL_INFO(draw_view)
GPU_SHADER_CREATE_END()
