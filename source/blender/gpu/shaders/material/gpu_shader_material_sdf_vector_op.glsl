/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_common_hash.glsl"
#include "gpu_shader_material_sdf_util.glsl"

#define SDF_AXIS_XYZ 0
#define SDF_AXIS_XZY 1
#define SDF_AXIS_YXZ 2
#define SDF_AXIS_YZX 3
#define SDF_AXIS_ZXY 4
#define SDF_AXIS_ZYX 5

float3 sdf_axis_swizzle(float3 p, float xyz)
{
  int axis = int(xyz);
  if (axis == SDF_AXIS_XZY) {
    return p.xzy;
  }
  else if (axis == SDF_AXIS_YXZ) {
    return p.yxz;
  }
  else if (axis == SDF_AXIS_YZX) {
    return p.yzx;
  }
  else if (axis == SDF_AXIS_ZXY) {
    return p.zxy;
  }
  else if (axis == SDF_AXIS_ZYX) {
    return p.zyx;
  }
  return p;
}

float3 sdf_axis_unswizzle(float3 p, float xyz)
{
  int axis = int(xyz);
  if (axis == SDF_AXIS_XZY) {
    return p.xzy;
  }
  else if (axis == SDF_AXIS_YXZ) {
    return p.yxz;
  }
  else if (axis == SDF_AXIS_YZX) {
    return p.zxy;
  }
  else if (axis == SDF_AXIS_ZXY) {
    return p.yzx;
  }
  else if (axis == SDF_AXIS_ZYX) {
    return p.zyx;
  }
  return p;
}

float2 sdf_uv_tileset(float3 puv, float pos, float padding, float scale, float cx, float cy)
{
  float2 uv;
  float safe_cx = max(cx, 1.0f);
  float safe_cy = max(cy, 1.0f);
  float stepx = 1.00001f / safe_cx;
  float stepy = 1.00001f / safe_cy;

  float posx = fract(pos);
  float posy = fract(pos * safe_cx);

  float fx = floor(safe_divide(posx, stepx)) * stepx;
  float fy = floor(safe_divide(posy, stepy)) * stepy;

  uv.x = fx + puv.x * stepx;
  uv.y = fy + puv.y * stepy;

  float cfx = stepx * 0.5f + fx;
  float cfy = stepy * 0.5f + fy;

  uv.x = safe_divide(uv.x - cfx, scale) + cfx;
  uv.y = safe_divide(uv.y - cfy, scale) + cfy;

  uv.x = clamp(uv.x, fx, fx + stepx);
  uv.y = clamp(uv.y, fy, fy + stepy);

  if (padding > 0.0f) {
    uv = sdf_map_value(uv, 0.0f, 1.0f, padding, 1.0f - padding, 0.0f);
  }
  return uv;
}

float2 sdf_uv_rotate_90(float2 p, int direction)
{
  float2 v = float2(0.5f, 0.5f);
  float2 uv = p - v;
  uv = (direction > 0) ? float2(uv.y, -uv.x) : float2(-uv.y, uv.x);
  return uv + v;
}

float2 sdf_uv_coords_rotate(float2 puv, float rnd)
{
  if (rnd > 0.75f) {
    return puv;
  }
  else if (rnd > 0.5f) {
    return sdf_uv_rotate_90(puv, 1);
  }
  else if (rnd > 0.25f) {
    return float2(1.0f) - puv;
  }
  return sdf_uv_rotate_90(puv, 0);
}

float2 sdf_uv_coords_flip_rotate(float2 puv, float rnd)
{
  if (rnd > 0.83333f) {
    return puv;
  }
  else if (rnd > 0.66667f) {
    return sdf_uv_rotate_90(puv, 1);
  }
  else if (rnd > 0.50000f) {
    return sdf_uv_rotate_90(puv, 0);
  }
  else if (rnd > 0.33333f) {
    return float2(1.0f - puv.x, puv.y);
  }
  else if (rnd > 0.16667f) {
    return float2(1.0f) - puv;
  }
  return float2(puv.x, 1.0f - puv.y);
}

float2 sdf_op_radial_shear(float2 uv, float2 center, float strength, float2 offset)
{
  float2 pos = uv - center;
  float pos_offset = dot(pos, pos) * strength;
  return float2(pos.y, -pos.x) * pos_offset + offset;
}

float2 sdf_op_swirl(float2 uv, float2 center, float strength, float2 offset)
{
  float2 pos = uv - center;
  float angle = strength * length(pos);
  float c = cos(angle);
  float s = sin(angle);
  float x = c * pos.x - s * pos.y;
  float y = s * pos.x + c * pos.y;
  return float2(x + center.x + offset.x, y + center.y + offset.y);
}

float3 sdf_op_pinch_inflate(float3 uv, float3 center, float strength, float radius)
{
  uv -= center;
  float dist = length(uv);
  if (dist > 1e-6f && radius > 1e-6f && dist < radius) {
    float percent = dist / radius;
    if (strength > 0.0f) {
      uv *= mix(1.0f, smoothstep(0.0f, radius / dist, percent), strength * 0.75f);
    }
    else {
      uv *= mix(
          1.0f, pow(max(percent, 1e-6f), 1.0f + strength * 0.75f) * radius / dist, 1.0f - percent);
    }
  }
  return uv + center;
}

[[node]]
void node_sdf_vector_op_extrude(float3 p,
                                float3 p2,
                                float3 p3,
                                float scale,
                                float v,
                                float v2,
                                float angle,
                                float n,
                                float n2,
                                float axis,
                                float3 &vout,
                                float3 &pos,
                                float &d)
{
  pos = float3(0.0f);
  p = sdf_axis_swizzle(p, axis);
  d = sdf_op_extrude(p, p2);
  vout = sdf_axis_unswizzle(p, axis);
}

[[node]]
void node_sdf_vector_op_spin(float3 p,
                             float3 p2,
                             float3 p3,
                             float scale,
                             float v,
                             float v2,
                             float angle,
                             float n,
                             float n2,
                             float axis,
                             float3 &vout,
                             float3 &pos,
                             float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  p = sdf_axis_swizzle(p, axis);
  p = sdf_op_spin(p, v);
  vout = sdf_axis_unswizzle(p, axis);
}

[[node]]
void node_sdf_vector_op_twist(float3 p,
                              float3 p2,
                              float3 p3,
                              float scale,
                              float v,
                              float v2,
                              float angle,
                              float n,
                              float n2,
                              float axis,
                              float3 &vout,
                              float3 &pos,
                              float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  p = sdf_axis_swizzle(p, axis);
  p = sdf_op_twist(p, v, angle);
  vout = sdf_axis_unswizzle(p, axis);
}

[[node]]
void node_sdf_vector_op_bend(float3 p,
                             float3 p2,
                             float3 p3,
                             float scale,
                             float v,
                             float v2,
                             float angle,
                             float n,
                             float n2,
                             float axis,
                             float3 &vout,
                             float3 &pos,
                             float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  p = sdf_axis_swizzle(p, axis);
  p = sdf_op_bend(p, angle);
  vout = sdf_axis_unswizzle(p, axis);
}

[[node]]
void node_sdf_vector_op_repeat_inf_mirror(float3 p,
                                          float3 p2,
                                          float3 p3,
                                          float scale,
                                          float v,
                                          float v2,
                                          float angle,
                                          float n,
                                          float n2,
                                          float axis,
                                          float3 &vout,
                                          float3 &pos,
                                          float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  sdf_p_mod_mirror3(p, p2);
  vout = p;
}

[[node]]
void node_sdf_vector_op_repeat_inf(float3 p,
                                   float3 p2,
                                   float3 p3,
                                   float scale,
                                   float v,
                                   float v2,
                                   float angle,
                                   float n,
                                   float n2,
                                   float axis,
                                   float3 &vout,
                                   float3 &pos,
                                   float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  vout = sdf_safe_mod(p + 0.5f * p2, p2) - 0.5f * p2;
}

[[node]]
void node_sdf_vector_op_repeat(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               float3 &vout,
                               float3 &pos,
                               float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  vout = p - clamp(floor(safe_divide(p, p2) + 0.5f), -p3, p3) * p2;
}

[[node]]
void node_sdf_vector_op_rotate(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               float3 &vout,
                               float3 &pos,
                               float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  p = sdf_axis_swizzle(p, axis);
  p.xy = cos(angle) * p.xy + sin(angle) * float2(p.y, -p.x);
  vout = sdf_axis_unswizzle(p, axis);
}

[[node]]
void node_sdf_vector_op_reflect(float3 p,
                                float3 p2,
                                float3 p3,
                                float scale,
                                float v,
                                float v2,
                                float angle,
                                float n,
                                float n2,
                                float axis,
                                float3 &vout,
                                float3 &pos,
                                float &d)
{
  pos = float3(0.0f);
  float len = length(p2);
  float3 normal = (len > 1e-6f) ? (p2 / len) : float3(0.0f, 0.0f, 1.0f);
  d = sdf_op_reflect(p, normal, v);
  vout = p;
}

[[node]]
void node_sdf_vector_op_mirror(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               float3 &vout,
                               float3 &pos,
                               float &d)
{
  d = 0.0f;
  p = sdf_axis_swizzle(p, axis);
  float2 p_xy = p.xy;
  pos = float3(sdf_p_mod_grid2(p_xy, p2.xy), 0.0f);
  p.xy = p_xy;
  vout = sdf_axis_unswizzle(p, axis);
  pos = sdf_axis_unswizzle(pos, axis);
}

[[node]]
void node_sdf_vector_op_polar(float3 p,
                              float3 p2,
                              float3 p3,
                              float scale,
                              float v,
                              float v2,
                              float angle,
                              float n,
                              float n2,
                              float axis,
                              float3 &vout,
                              float3 &pos,
                              float &d)
{
  pos = float3(0.0f);
  p = sdf_axis_swizzle(p, axis);
  float2 p_xy = p.xy;
  d = sdf_op_polar(p_xy, v);
  p.xy = p_xy;
  vout = sdf_axis_unswizzle(p, axis);
}

[[node]]
void node_sdf_vector_op_swizzle(float3 p,
                                float3 p2,
                                float3 p3,
                                float scale,
                                float v,
                                float v2,
                                float angle,
                                float n,
                                float n2,
                                float axis,
                                float3 &vout,
                                float3 &pos,
                                float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  vout = sdf_axis_swizzle(p, axis);
}

[[node]]
void node_sdf_vector_op_grid(float3 p,
                             float3 p2,
                             float3 p3,
                             float scale,
                             float v,
                             float v2,
                             float angle,
                             float n,
                             float n2,
                             float axis,
                             float3 &vout,
                             float3 &pos,
                             float &d)
{
  d = 0.0f;
  p = sdf_axis_swizzle(p, axis);
  p = (p + 0.000001f) * 0.999999f;
  p = p * p2;
  float3 pf = floor(p);
  p = p - pf;
  vout = sdf_axis_unswizzle(p, axis);
  pos = sdf_axis_unswizzle(safe_divide(pf, p2), axis);
}

[[node]]
void node_sdf_vector_op_random_uv_rotate(float3 p,
                                         float3 p2,
                                         float3 p3,
                                         float scale,
                                         float v,
                                         float v2,
                                         float angle,
                                         float n,
                                         float n2,
                                         float axis,
                                         float3 &vout,
                                         float3 &pos,
                                         float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  float rnd = hash_vec3_to_float(p2);
  vout = float3(sdf_uv_coords_rotate(p.xy, rnd), 0.0f);
}

[[node]]
void node_sdf_vector_op_random_uv_flip(float3 p,
                                       float3 p2,
                                       float3 p3,
                                       float scale,
                                       float v,
                                       float v2,
                                       float angle,
                                       float n,
                                       float n2,
                                       float axis,
                                       float3 &vout,
                                       float3 &pos,
                                       float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  float rnd = hash_vec3_to_float(p2);
  vout = float3(sdf_uv_coords_flip_rotate(p.xy, rnd), 0.0f);
}

[[node]]
void node_sdf_vector_op_tileset(float3 p,
                                float3 p2,
                                float3 p3,
                                float scale,
                                float v,
                                float v2,
                                float angle,
                                float n,
                                float n2,
                                float axis,
                                float3 &vout,
                                float3 &pos,
                                float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  vout = float3(sdf_uv_tileset(p, v, v2, scale, n, n2), 0.0f);
}

[[node]]
void node_sdf_vector_op_octant(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               float3 &vout,
                               float3 &pos,
                               float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  p = sdf_axis_swizzle(p, axis);
  sdf_p_mirror(p.x, v);
  sdf_p_mirror(p.y, v);
  if (p.y > p.x) {
    p.xy = p.yx;
  }
  vout = sdf_axis_unswizzle(p, axis);
}

[[node]]
void node_sdf_vector_op_map_uv(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               float3 &vout,
                               float3 &pos,
                               float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  vout = sdf_map_value(p, -1.0f, 1.0f, 0.0f, 1.0f, 0.0f);
}

[[node]]
void node_sdf_vector_op_map_11(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               float3 &vout,
                               float3 &pos,
                               float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  vout = sdf_map_value(p, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f);
}

[[node]]
void node_sdf_vector_op_map_05(float3 p,
                               float3 p2,
                               float3 p3,
                               float scale,
                               float v,
                               float v2,
                               float angle,
                               float n,
                               float n2,
                               float axis,
                               float3 &vout,
                               float3 &pos,
                               float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  vout = sdf_map_value(p, 0.0f, 1.0f, -0.5f, 0.5f, 0.0f);
}

[[node]]
void node_sdf_vector_op_uv_rotate(float3 p,
                                  float3 p2,
                                  float3 p3,
                                  float scale,
                                  float v,
                                  float v2,
                                  float angle,
                                  float n,
                                  float n2,
                                  float axis,
                                  float3 &vout,
                                  float3 &pos,
                                  float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  float2 center = p2.xy;
  p.xy -= center;
  p.xy = cos(angle) * p.xy + sin(angle) * float2(p.y, -p.x);
  p.xy += center;
  vout = float3(p.xy, 0.0f);
}

[[node]]
void node_sdf_vector_op_uv_scale(float3 p,
                                 float3 p2,
                                 float3 p3,
                                 float scale,
                                 float v,
                                 float v2,
                                 float angle,
                                 float n,
                                 float n2,
                                 float axis,
                                 float3 &vout,
                                 float3 &pos,
                                 float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  float2 center = float2(0.5f, 0.5f);
  p.xy = (p.xy - center) * scale + center;
  vout = p;
}

[[node]]
void node_sdf_vector_op_swirl(float3 p,
                              float3 p2,
                              float3 p3,
                              float scale,
                              float v,
                              float v2,
                              float angle,
                              float n,
                              float n2,
                              float axis,
                              float3 &vout,
                              float3 &pos,
                              float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  p = sdf_axis_swizzle(p, axis);
  p.xy = sdf_op_swirl(p.xy, p2.xy, v, p3.xy);
  vout = sdf_axis_unswizzle(p, axis);
}

[[node]]
void node_sdf_vector_op_radial_shear(float3 p,
                                     float3 p2,
                                     float3 p3,
                                     float scale,
                                     float v,
                                     float v2,
                                     float angle,
                                     float n,
                                     float n2,
                                     float axis,
                                     float3 &vout,
                                     float3 &pos,
                                     float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  p = sdf_axis_swizzle(p, axis);
  p.xy = sdf_op_radial_shear(p.xy, p2.xy, v, p3.xy);
  vout = sdf_axis_unswizzle(p, axis);
}

[[node]]
void node_sdf_vector_op_pinch_inflate(float3 p,
                                      float3 p2,
                                      float3 p3,
                                      float scale,
                                      float v,
                                      float v2,
                                      float angle,
                                      float n,
                                      float n2,
                                      float axis,
                                      float3 &vout,
                                      float3 &pos,
                                      float &d)
{
  pos = float3(0.0f);
  d = 0.0f;
  p = sdf_axis_swizzle(p, axis);
  p = sdf_op_pinch_inflate(p, p2, v, v2);
  vout = sdf_axis_unswizzle(p, axis);
}
