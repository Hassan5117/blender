/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

float goo_srgb_to_lin(float color_channel)
{
  float c = max(0.0f, color_channel);
  if (c <= 0.04045f) {
    return c / 12.92f;
  }
  return pow((c + 0.055f) / 1.055f, 2.4f);
}

float goo_y_to_lstar(float Y)
{
  if (Y <= (216.0f / 24389.0f)) {
    return Y * (24389.0f / 27.0f);
  }
  return pow(max(0.0f, Y), 1.0f / 3.0f) * 116.0f - 16.0f;
}

[[node]]
void node_light_info_simple(float4 light_color,
                            float light_power,
                            float4 &out_light_color,
                            float &out_light_power,
                            float &out_perceptual_power)
{
  out_light_color = light_color;
  out_light_power = light_power;

  float vR = goo_srgb_to_lin(light_color.r);
  float vG = goo_srgb_to_lin(light_color.g);
  float vB = goo_srgb_to_lin(light_color.b);

  float Y = 0.2126f * vR + 0.7152f * vG + 0.0722f * vB;
  float Lstar = goo_y_to_lstar(Y);

  out_perceptual_power = ((Lstar * light_power) / 255.0f) * 2.489645f;
}
