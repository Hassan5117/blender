/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_math_constants_lib.glsl"

float water_ripples_bias(float x, float b)
{
  float safe_b = clamp(b, 0.01f, 0.99f);
  return x / ((1.0f / safe_b - 2.0f) * (1.0f - x) + 1.0f);
}

float water_ripples_hash12(float2 p, float hash_scale)
{
  float3 p3 = fract(float3(p.x, p.y, p.x) * hash_scale);
  float3 add_val = p3.yzx + 19.19f;
  p3 += dot(p3, add_val);
  return fract((p3.x + p3.y) * p3.z);
}

float2 water_ripples_hash22(float2 p, float3 hash_scale)
{
  float3 p3 = fract(float3(p.x, p.y, p.x) * hash_scale);
  float3 add_val = p3.yzx + 19.19f;
  p3 += dot(p3, add_val);
  return fract((p3.xx + p3.yz) * p3.zy);
}

[[node]]
void node_water_ripples(float3 vector,
                        float time,
                        float mode,
                        float scale,
                        float intensity,
                        float speed,
                        float detail,
                        float bias_amount,
                        float3 &distorted_vector,
                        float &mask)
{
  float safe_scale = max(scale, 1.0e-3f);
  float2 uv = vector.xy * safe_scale;
  float h = 0.0f;
  int imode = int(mode + 0.5f);

  if (imode == 0) {
    /* DROPS MODE */
    int divisions = clamp(int(detail * 8.0f + 2.0f), 2, 8);
    float div_minus_one = max(float(divisions - 1), 1.0f);
    float hash_scale = 1031.0f / 10000.0f;

    for (int iy = 0; iy < 8; iy++) {
      if (iy >= divisions) {
        break;
      }
      for (int ix = 0; ix < 16; ix++) {
        if (ix >= divisions * 2) {
          break;
        }
        float2 noise_coord = float2(float(ix), float(iy)) / float(divisions);
        float4 t = float4(water_ripples_hash12(noise_coord, hash_scale),
                          water_ripples_hash12(noise_coord + float2(0.1f, 0.0f), hash_scale),
                          water_ripples_hash12(noise_coord + float2(0.0f, 0.1f), hash_scale),
                          water_ripples_hash12(noise_coord + float2(0.1f, 0.1f), hash_scale));

        float2 p = float2(float(ix), float(iy)) * (1.0f / div_minus_one);
        p += (0.75f / div_minus_one) * (t.xy * 2.0f - 1.0f);

        float2 v = uv - p;
        float d = pow(max(dot(v, v), 1.0e-3f), 0.7f);

        float n = time * speed * 5.0f * (t.w + 0.2f) - t.z * 6.0f;
        n *= 0.1f + t.w;
        n = mod(n, 20.0f + t.z * 3.0f);
        n = max(n, 1.0e-3f);

        float x = d * 99.0f;
        float two_pi_n = 2.0f * M_PI * n;
        float T = (x < two_pi_n) ? 1.0f : 0.0f;
        float e = max(1.0f - (n / 10.0f), 0.0f);
        float F = e * x / max(two_pi_n, 1.0e-3f);
        float s = sin(x - two_pi_n - M_PI * 0.5f) * 0.5f + 0.5f;
        s = water_ripples_bias(s, bias_amount);
        s = (F * s) / (x + 1.1f) * T;
        h += s * 100.0f * (0.5f + t.w) * intensity;
      }
    }

    float2 gradient = float2(0.0f);
    float eps = 1.0e-3f;
    float h_x = 0.0f;
    float2 uv_x = (vector.xy + float2(eps, 0.0f)) * safe_scale;

    for (int iy = 0; iy < 8; iy++) {
      if (iy >= divisions) {
        break;
      }
      for (int ix = 0; ix < 16; ix++) {
        if (ix >= divisions * 2) {
          break;
        }
        float2 noise_coord = float2(float(ix), float(iy)) / float(divisions);
        float4 t = float4(water_ripples_hash12(noise_coord, hash_scale),
                          water_ripples_hash12(noise_coord + float2(0.1f, 0.0f), hash_scale),
                          water_ripples_hash12(noise_coord + float2(0.0f, 0.1f), hash_scale),
                          water_ripples_hash12(noise_coord + float2(0.1f, 0.1f), hash_scale));

        float2 p = float2(float(ix), float(iy)) * (1.0f / div_minus_one);
        p += (0.75f / div_minus_one) * (t.xy * 2.0f - 1.0f);

        float2 v_x = uv_x - p;
        float d_x = pow(max(dot(v_x, v_x), 1.0e-3f), 0.7f);

        float n = time * speed * 5.0f * (t.w + 0.2f) - t.z * 6.0f;
        n *= 0.1f + t.w;
        n = mod(n, 20.0f + t.z * 3.0f);
        n = max(n, 1.0e-3f);

        float x = d_x * 99.0f;
        float two_pi_n = 2.0f * M_PI * n;
        float T = (x < two_pi_n) ? 1.0f : 0.0f;
        float e = max(1.0f - (n / 10.0f), 0.0f);
        float F = e * x / max(two_pi_n, 1.0e-3f);
        float s = sin(x - two_pi_n - M_PI * 0.5f) * 0.5f + 0.5f;
        s = water_ripples_bias(s, bias_amount);
        s = (F * s) / (x + 1.1f) * T;
        h_x += s * 100.0f * (0.5f + t.w) * intensity;
      }
    }

    gradient.x = (h_x - h) / eps;
    distorted_vector = vector + float3(gradient * intensity * 0.01f, 0.0f);
    mask = clamp(h * 0.01f, 0.0f, 1.0f);
  }
  else if (imode == 1) {
    /* RIPPLES MODE */
    int max_radius = clamp(int(detail * 3.0f + 1.0f), 1, 3);
    float2 p0 = floor(uv);
    float2 circles = float2(0.0f);
    float hash_scale1 = 1031.0f / 10000.0f;
    float hash_scale2 = 1030.0f / 10000.0f;
    float hash_scale3 = 973.0f / 10000.0f;

    for (int j = -3; j <= 3; j++) {
      if (abs(j) > max_radius) {
        continue;
      }
      for (int i = -3; i <= 3; i++) {
        if (abs(i) > max_radius) {
          continue;
        }
        float2 pi = p0 + float2(float(i), float(j));
        float2 p = pi + water_ripples_hash22(pi, float3(hash_scale1, hash_scale2, hash_scale3));
        float t = fract(speed * time + water_ripples_hash12(pi, hash_scale1));
        float2 v = p - uv;
        float d = length(v) - (float(max_radius) + 1.0f) * t;
        float h_val = 1.0e-3f;
        float d1 = d - h_val;
        float d2 = d + h_val;
        float smooth1 = -0.6f;
        float smooth2 = -0.3f;
        float p1 = sin(31.0f * d1) * smoothstep(smooth1, smooth2, d1) *
                   (1.0f - smoothstep(smooth2, 0.0f, d1));
        float p2 = sin(31.0f * d2) * smoothstep(smooth1, smooth2, d2) *
                   (1.0f - smoothstep(smooth2, 0.0f, d2));
        float ripple_fade = (1.0f - t) * (1.0f - t);
        float2 norm_v = float2(0.0f, 1.0f);
        float v_length = length(v);
        if (v_length > 1.0e-3f) {
          norm_v = v / v_length;
        }
        circles += norm_v * ((p2 - p1) / (2.0f * h_val) * ripple_fade);
      }
    }
    circles *= intensity;
    h = length(circles);
    distorted_vector = vector + float3(circles * intensity * 0.1f, 0.0f);
    mask = h;
  }
  else if (imode == 2) {
    /* FLOW MODE */
    float2 flow_uv = uv;
    float time_scaled = time * speed;

    float wave1 = sin(flow_uv.x * detail * 10.0f + time_scaled * 2.0f) * 0.1f;
    float wave2 = cos(flow_uv.y * detail * 8.0f + time_scaled * 1.5f) * 0.15f;

    float2 flow_offset = float2(
        wave1 + sin(flow_uv.y * detail * 5.0f + time_scaled) * 0.05f,
        wave2 + cos(flow_uv.x * detail * 6.0f + time_scaled * 0.8f) * 0.08f);
    flow_offset *= intensity * 0.1f;

    distorted_vector = vector + float3(flow_offset, 0.0f);
    mask = clamp(length(flow_offset) * 10.0f, 0.0f, 1.0f);
  }
  else if (imode == 3) {
    /* CAUSTIC MODE */
    float2 p = uv * scale;
    float a = 1.0f;
    float scale_safe = max(bias_amount, 1e-3f);
    float3 k = float3(p.x / scale_safe * detail, p.y / scale_safe * detail, sin(time * 0.2f));
    float3x3 m = float3x3(
                     float3(-2.0f, -1.0f, 2.0f), float3(3.0f, -2.0f, 1.0f), float3(1.0f, 2.0f, 2.0f)) *
                 0.3f;
    k = m * k;
    a = min(a, length(0.5f - fract(k)));
    k = m * k;
    a = min(a, length(0.5f - fract(k)));
    k = m * k;
    a = min(a, length(0.5f - fract(k)));
    float caustic = pow(max(a, 0.0f), detail) * 25.0f * intensity;
    distorted_vector = vector;
    mask = clamp(caustic, 0.0f, 1.0f);
  }
  else {
    /* Fallback distortion mode */
    float2 base_uv = uv;
    float hash_scale = 1031.0f / 10000.0f;
    float2 noise_uv = uv * scale;
    float2 t = float2(water_ripples_hash12(noise_uv, hash_scale),
                      water_ripples_hash12(noise_uv + float2(0.1f, 0.1f), hash_scale));
    if (detail > 0.5f) {
      base_uv.xy += ((t.xy - 0.5f) * intensity * 0.04f);
      base_uv.y += time * speed * 0.08f + intensity * 0.01f * sin(time * speed * 0.4f);
    }
    else {
      float noise = (t.x / max(t.y, 0.001f)) + time * speed * (-0.025f) +
                    intensity * 0.01f * sin(time * speed);
      base_uv += float2(noise * intensity * 0.4f);
    }
    float2 distortion = base_uv - uv;
    distorted_vector = float3(base_uv, vector.z);
    mask = clamp(length(distortion) * intensity * 10.0f, 0.0f, 1.0f);
  }
}
