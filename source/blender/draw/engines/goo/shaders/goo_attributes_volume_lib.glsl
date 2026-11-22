/* SPDX-FileCopyrightText: 2022-2023 Blender Authors
 * SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Volume attribute & matrix accessors for Goo Engine runtime volume node-trees. */

#pragma once

#include "draw_shader_shared.hh"
#include "goo_geom_types_lib.glsl"
#include "gpu_shader_codegen_lib.glsl"

SHADER_LIBRARY_CREATE_INFO(goo_volume_ubo)

ObjectMatrices object_matrices_get()
{
  ObjectMatrices m;
  m.model = goo_vol_buf.obj_to_world;
  m.model_inverse = goo_vol_buf.world_to_obj;
  return m;
}

ObjectInfos object_infos_get()
{
  ObjectInfos info;
  info.orco_add = packed_float3(goo_vol_buf.orco_add.xyz);
  info.object_attrs_offset = 0u;
  info.orco_mul = packed_float3(goo_vol_buf.orco_mul.xyz);
  info.object_attrs_len = 0u;
  info.ob_color = float4(1.0f);
  info.index = 0u;
  info.light_and_shadow_set_membership = 0u;
  info.random = 0.0f;
  info.flag = eObjectInfoFlag(0u);
  info.shadow_terminator_normal_offset = 0.0f;
  info.shadow_terminator_geometry_offset = 0.0f;
  info._pad1 = 0.0f;
  info._pad2 = 0.0f;
  return info;
}

uint goo_resource_id_get()
{
  return 0u;
}

float3 attr_load_orco(VolumePoint point, float4 /*orco*/, int /*index*/)
{
  return point.orco_default;
}
float4 attr_load_tangent(VolumePoint /*point*/, float4 tangent, int /*index*/)
{
  return tangent;
}
float4 attr_load_float4(VolumePoint /*point*/, float4 attr, int /*index*/)
{
  return attr;
}
float3 attr_load_float3(VolumePoint /*point*/, float3 attr, int /*index*/)
{
  return attr;
}
float2 attr_load_float2(VolumePoint /*point*/, float2 attr, int /*index*/)
{
  return attr;
}
float attr_load_float(VolumePoint /*point*/, float attr, int /*index*/)
{
  return attr;
}
float4 attr_load_color(VolumePoint /*point*/, float4 attr, int /*index*/)
{
  return attr;
}
float3 attr_load_uv(VolumePoint /*point*/, float3 attr, int /*index*/)
{
  return attr;
}
