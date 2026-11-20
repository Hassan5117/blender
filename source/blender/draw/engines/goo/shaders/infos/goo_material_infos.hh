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
#  include "goo_nodetree_type_lib.glsl"
#endif

#ifdef GLSL_CPP_STUBS
#  define GOO_MAX_LIGHTS 16
#endif

#include "gpu_shader_create_info.hh"

GPU_SHADER_NAMED_INTERFACE_INFO(goo_geom_iface, interp)
SMOOTH(float3, P)
SMOOTH(float3, N)
GPU_SHADER_NAMED_INTERFACE_END(interp)

/* Scene lighting (temporary stand-in for Goo's light UBO, see goo_light.hh). */
GPU_SHADER_CREATE_INFO(goo_lighting)
DEFINE_VALUE("GOO_MAX_LIGHTS", "16")
PUSH_CONSTANT_ARRAY(float4, goo_lights, 32) /* 2 x float4 per light. */
PUSH_CONSTANT(int, goo_light_count)
PUSH_CONSTANT(float4, goo_world_color)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_material_surface_mesh)
VERTEX_IN(0, float3, pos)
VERTEX_IN(1, float3, nor)
VERTEX_OUT(goo_geom_iface)
FRAGMENT_OUT(0, float4, out_color)
VERTEX_SOURCE("goo_material_surface_vert.glsl")
FRAGMENT_SOURCE("goo_material_surface_frag.glsl")
ADDITIONAL_INFO(goo_lighting)
ADDITIONAL_INFO(UtilityTexture)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_object_infos)
GPU_SHADER_CREATE_END()
