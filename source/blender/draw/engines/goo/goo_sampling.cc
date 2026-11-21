/* SPDX-FileCopyrightText: 2016 Blender Authors
 * SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <cmath>

#include "BLI_math_geom.h"
#include "BLI_rand.h"

#include "goo_sampling.hh"

namespace blender::goo {

static float filter_blackman_harris(float x)
{
  const float t = 2.0f * float(M_PI) * (x + 0.5f);
  return 0.35875f - 0.48829f * std::cos(t) + 0.14128f * std::cos(2.0f * t) -
         0.01168f * std::cos(3.0f * t);
}

static float eval_cdf_table(const std::array<float, Sampling::FILTER_CDF_TABLE_SIZE> &table,
                            float x)
{
  const float xc = std::clamp(x, 0.0f, 1.0f) * float(Sampling::FILTER_CDF_TABLE_SIZE - 1);
  const int index = std::min(int(xc), Sampling::FILTER_CDF_TABLE_SIZE - 1);
  const int nindex = std::min(index + 1, Sampling::FILTER_CDF_TABLE_SIZE - 1);
  const float t = xc - float(index);
  return (1.0f - t) * table[index] + t * table[nindex];
}

const std::array<float, Sampling::FILTER_CDF_TABLE_SIZE> &Sampling::inverted_cdf_table()
{
  static const std::array<float, FILTER_CDF_TABLE_SIZE> table = []() {
    std::array<float, FILTER_CDF_TABLE_SIZE> cdf{};
    cdf[0] = 0.0f;
    for (int u = 0; u < FILTER_CDF_TABLE_SIZE - 1; u++) {
      const float x = float(u + 1) / float(FILTER_CDF_TABLE_SIZE - 1);
      cdf[u + 1] = cdf[u] + std::max(0.0f, filter_blackman_harris(x - 0.5f));
    }
    const float total = std::max(cdf[FILTER_CDF_TABLE_SIZE - 1], 1e-6f);
    for (int u = 0; u < FILTER_CDF_TABLE_SIZE - 1; u++) {
      cdf[u] /= total;
    }
    cdf[FILTER_CDF_TABLE_SIZE - 1] = 1.0f;

    std::array<float, FILTER_CDF_TABLE_SIZE> inv{};
    for (int u = 0; u < FILTER_CDF_TABLE_SIZE; u++) {
      const float x = float(u) / float(FILTER_CDF_TABLE_SIZE - 1);
      for (int i = 0; i < FILTER_CDF_TABLE_SIZE; i++) {
        if (cdf[i] >= x) {
          if (i >= FILTER_CDF_TABLE_SIZE - 1) {
            inv[u] = 0.5f;
          }
          else {
            const float denom = cdf[i + 1] - cdf[i];
            const float t = (denom > 1e-8f) ? ((x - cdf[i]) / denom) : 0.0f;
            const float norm = (float(i) + std::clamp(t, 0.0f, 1.0f)) /
                               float(FILTER_CDF_TABLE_SIZE - 1);
            inv[u] = norm - 0.5f;
          }
          break;
        }
      }
    }
    return inv;
  }();
  return table;
}

float2 Sampling::pixel_jitter_get(int sample_index, float filter_size) const
{
  if (sample_index <= 0) {
    return float2(0.0f);
  }
  const unsigned int primes[2] = {2, 3};
  double offset[2] = {0.0, 0.0};
  double ht[2] = {0.0, 0.0};
  BLI_halton_2d(primes, offset, sample_index, ht);

  const float clamped_filter = std::clamp(filter_size, 0.01f, 10.0f);
  const auto &table = inverted_cdf_table();
  return float2(eval_cdf_table(table, float(ht[0])) * clamped_filter,
                eval_cdf_table(table, float(ht[1])) * clamped_filter);
}

float2 Sampling::temporal_rand_get(int sample_index) const
{
  if (sample_index <= 0) {
    return float2(0.0f);
  }
  double u = 0.7548776662466927 * double(sample_index);
  double v = 0.5698402909980532 * double(sample_index);
  u -= std::floor(u);
  v -= std::floor(v);
  return float2(float(u), float(v));
}

float4x4 Sampling::jittered_winmat(const float4x4 &base_winmat,
                                   int2 render_extent,
                                   float2 pixel_jitter)
{
  float4x4 winmat = base_winmat;
  if ((pixel_jitter.x != 0.0f || pixel_jitter.y != 0.0f) && render_extent.x > 0 &&
      render_extent.y > 0)
  {
    const float2 jitter_ndc = (pixel_jitter / float2(render_extent)) * 2.0f;
    window_translate_m4(winmat.ptr(), winmat.ptr(), jitter_ndc.x, jitter_ndc.y);
  }
  return winmat;
}

}  // namespace blender::goo
