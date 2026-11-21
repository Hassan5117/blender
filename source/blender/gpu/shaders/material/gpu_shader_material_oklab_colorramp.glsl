/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_common_color_ramp.glsl"

float3 oklab_linear_srgb_to_oklab(float3 c)
{
  float l = 0.4122214708f * c.x + 0.5363325363f * c.y + 0.0514459929f * c.z;
  float m = 0.2119034982f * c.x + 0.6806995451f * c.y + 0.1073969566f * c.z;
  float s = 0.0883024619f * c.x + 0.2817188376f * c.y + 0.6299787005f * c.z;

  float l_ = sign(l) * pow(abs(l) + 1e-10f, 1.0f / 3.0f);
  float m_ = sign(m) * pow(abs(m) + 1e-10f, 1.0f / 3.0f);
  float s_ = sign(s) * pow(abs(s) + 1e-10f, 1.0f / 3.0f);

  return float3(0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
                1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
                0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_);
}

float3 oklab_oklab_to_linear_srgb(float3 c)
{
  float l_ = c.x + 0.3963377774f * c.y + 0.2158037573f * c.z;
  float m_ = c.x - 0.1055613458f * c.y - 0.0638541728f * c.z;
  float s_ = c.x - 0.0894841775f * c.y - 1.2914855480f * c.z;

  float l = l_ * l_ * l_;
  float m = m_ * m_ * m_;
  float s = s_ * s_ * s_;

  float3 linear_rgb = float3(+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
                             -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
                             -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);

  float max_component = max(max(linear_rgb.r, linear_rgb.g), linear_rgb.b);
  if (max_component > 1.0f) {
    linear_rgb = linear_rgb / max_component;
  }

  return max(float3(0.0f), linear_rgb);
}

float4 oklab_mix(float4 color1, float4 color2, float factor)
{
  factor = clamp(factor, 0.0f, 1.0f);

  float3 oklab1 = oklab_linear_srgb_to_oklab(color1.rgb);
  float3 oklab2 = oklab_linear_srgb_to_oklab(color2.rgb);

  float3 oklab_mixed = mix(oklab1, oklab2, factor);
  float3 linear_mixed = oklab_oklab_to_linear_srgb(oklab_mixed);
  float alpha_mixed = mix(color1.a, color2.a, factor);

  return float4(clamp(linear_mixed, 0.0f, 1.0f), alpha_mixed);
}

[[node]]
void oklab_valtorgb_opti_linear(
    float fac, float2 mulbias, float4 color1, float4 color2, float4 &outcol, float &outalpha)
{
  fac = clamp(fac * mulbias.x + mulbias.y, 0.0f, 1.0f);
  outcol = oklab_mix(color1, color2, fac);
  outalpha = outcol.a;
}

[[node]]
void oklab_valtorgb_opti_constant(
    float fac, float edge, float4 color1, float4 color2, float4 &outcol, float &outalpha)
{
  outcol = (fac > edge) ? color2 : color1;
  outalpha = outcol.a;
}

[[node]]
void oklab_valtorgb_opti_ease(
    float fac, float2 mulbias, float4 color1, float4 color2, float4 &outcol, float &outalpha)
{
  fac = clamp(fac * mulbias.x + mulbias.y, 0.0f, 1.0f);
  fac = fac * fac * (3.0f - 2.0f * fac);
  outcol = oklab_mix(color1, color2, fac);
  outalpha = outcol.a;
}

[[node]]
void oklab_valtorgb(float fac, sampler1DArray colormap, float layer, float4 &outcol, float &outalpha)
{
  fac = clamp(fac, 0.0f, 1.0f);
  outcol = texture(colormap, float2(compute_color_map_coordinate(fac), layer));
  outalpha = outcol.a;
}

[[node]]
void oklab_valtorgb_ease(
    float fac, sampler1DArray colormap, float layer, float4 &outcol, float &outalpha)
{
  fac = clamp(fac, 0.0f, 1.0f);
  outcol = texture(colormap, float2(compute_color_map_coordinate(fac), layer));
  outalpha = outcol.a;
}

[[node]]
void oklab_valtorgb_nearest(
    float fac, sampler1DArray colormap, float layer, float4 &outcol, float &outalpha)
{
  fac = clamp(fac, 0.0f, 1.0f);
  outcol = texelFetch(
      colormap, int2(int(fac * float(textureSize(colormap, 0).x - 1)), int(layer)), 0);
  outalpha = outcol.a;
}
