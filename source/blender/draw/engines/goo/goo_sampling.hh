/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Temporal anti-aliasing jitter & low-discrepancy sample sequences.
 */

#pragma once

#include <array>

#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_vector.hh"

namespace blender {
struct CurveMapping;
}

namespace blender::goo {

class Sampling {
 public:
  static constexpr int FILTER_CDF_TABLE_SIZE = 512;

  Sampling() = default;

  /** Pixel-space subpixel jitter offset using Blackman-Harris importance sampling + Halton (2,3). */
  float2 pixel_jitter_get(int sample_index, float filter_size) const;

  /** Temporal decorrelation 2D offset using R2 sequence (0,0 at sample 0). */
  float2 temporal_rand_get(int sample_index) const;

  /** Return `base_winmat` translated by `pixel_jitter` within `render_extent`. */
  static float4x4 jittered_winmat(const float4x4 &base_winmat,
                                  int2 render_extent,
                                  float2 pixel_jitter);

  static void cdf_from_curvemapping(const CurveMapping &curve, Vector<float> &cdf);
  static void cdf_invert(const Vector<float> &cdf, Vector<float> &inverted_cdf);

 private:
  static const std::array<float, FILTER_CDF_TABLE_SIZE> &inverted_cdf_table();
};

}  // namespace blender::goo
