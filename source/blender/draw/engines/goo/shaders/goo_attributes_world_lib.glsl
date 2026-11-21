/* SPDX-FileCopyrightText: 2022-2023 Blender Authors
 * SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* World attribute loading used by runtime-generated `attrib_load(WorldPoint)`. */

#pragma once

#include "draw_shader_shared.hh"
#include "gpu_shader_codegen_lib.glsl"

#include "goo_geom_types_lib.glsl"

ObjectMatrices object_matrices_get()
{
  ObjectMatrices m;
  m.model = float4x4(1.0f);
  m.model_inverse = float4x4(1.0f);
  return m;
}

ObjectInfos object_infos_get()
{
  ObjectInfos info;
  info.orco_add = packed_float3(0.0f);
  info.object_attrs_offset = 0u;
  info.orco_mul = packed_float3(1.0f);
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

float3 attr_load_orco(WorldPoint point, float4 /*orco*/, int /*index*/)
{
  return point.lP;
}
float4 attr_load_tangent(WorldPoint /*point*/, float4 /*tangent*/, int /*index*/)
{
  return float4(0.0f);
}
float4 attr_load_float4(WorldPoint /*point*/, float4 /*attr*/, int /*index*/)
{
  return float4(0.0f);
}
float3 attr_load_float3(WorldPoint /*point*/, float3 /*attr*/, int /*index*/)
{
  return float3(0.0f);
}
float2 attr_load_float2(WorldPoint /*point*/, float2 /*attr*/, int /*index*/)
{
  return float2(0.0f);
}
float attr_load_float(WorldPoint /*point*/, float /*attr*/, int /*index*/)
{
  return 0.0f;
}
float4 attr_load_color(WorldPoint /*point*/, float4 /*attr*/, int /*index*/)
{
  return float4(0.0f);
}
float3 attr_load_uv(WorldPoint /*point*/, float3 /*attr*/, int /*index*/)
{
  return float3(0.0f);
}
