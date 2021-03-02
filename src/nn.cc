/* GstPEAQ
 * Copyright (C) 2014, 2015, 2021 Martin Holters <martin.holters@hsu-hh.de>
 *
 * nn.cc: Evaluate neural network to compute DI and ODG
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "nn.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace peaq {

template<std::size_t Nmovs, std::size_t N2>
struct nn_coeffs
{
  const std::array<double, Nmovs> amin;
  const std::array<double, Nmovs> amax;
  const std::array<std::array<double, N2>, Nmovs> wx;
  const std::array<double, N2> wxb;
  const std::array<double, N2> wy;
  const double wyb;
};

static auto const nn_basic = nn_coeffs<11, 3>{
  {
    // amin
    393.916656,
    361.965332,
    -24.045116,
    1.110661,
    -0.206623,
    0.074318,
    1.113683,
    0.950345,
    0.029985,
    0.000101,
    0.0,
  },
  {
    // amax
    921.0,
    881.131226,
    16.212030,
    107.137772,
    2.886017,
    13.933351,
    63.257874,
    1145.018555,
    14.819740,
    1.0,
    1.0,
  },
  { {
    // wx
    { -0.502657, 0.436333, 1.219602 },
    { 4.307481, 3.246017, 1.123743 },
    { 4.984241, -2.211189, -0.192096 },
    { 0.051056, -1.762424, 4.331315 },
    { 2.321580, 1.789971, -0.754560 },
    { -5.303901, -3.452257, -10.814982 },
    { 2.730991, -6.111805, 1.519223 },
    { 0.624950, -1.331523, -5.955151 },
    { 3.102889, 0.871260, -5.922878 },
    { -1.051468, -0.939882, -0.142913 },
    { -1.804679, -0.503610, -0.620456 },
  } },
  { -2.518254, 0.654841, -2.207228 }, // wxb
  { -3.817048, 4.107138, 4.629582 },  // wy
  -0.307594,                          // wyb
};

static auto const nn_advanced = nn_coeffs<5, 5>{
  { 13.298751, 0.041073, -25.018791, 0.061560, 0.02452 }, // amin
  { 2166.5, 13.24326, 13.46708, 10.226771, 14.224874 },   // amax
  { {
    // wx
    { 21.211773, -39.013052, -1.382553, -14.545348, -0.320899 },
    { -8.981803, 19.956049, 0.935389, -1.686586, -3.238586 },
    { 1.633830, -2.877505, -7.442935, 5.606502, -1.783120 },
    { 6.103821, 19.587435, -0.240284, 1.088213, -0.511314 },
    { 11.556344, 3.892028, 9.720441, -3.287205, -11.031250 },
  } },
  { 1.330890, 2.686103, 2.096598, -1.327851, 3.087055 },  // wxb
  { -4.696996, -3.289959, 7.004782, 6.651897, 4.009144 }, // wy
  -1.360308,                                              // wyb
};

static const auto bmin = -3.98;
static const auto bmax = 0.22;

template<std::size_t Nmovs, std::size_t N2>
static auto calculate_di(nn_coeffs<Nmovs, N2> const& nn,
                         std::array<double, Nmovs> const& movs)
{
  using std::begin;
  using std::end;

  auto x = nn.wxb;

  for (std::size_t i = 0; i < Nmovs; i++) {
    auto m = (movs[i] - nn.amin[i]) / (nn.amax[i] - nn.amin[i]);
    /* according to [Kabal03], it is unclear whether the MOVs should be
     * clipped to within [amin, amax], although [BS1387] does not mention
     * clipping at all; doing so slightly improves the results of the
     * conformance test */
#if defined(CLAMP_MOVS) && CLAMP_MOVS
    if (m < 0.)
      m = 0.;
    if (m > 1.)
      m = 1.;
#endif
    std::transform(begin(x), end(x), begin(nn.wx[i]), begin(x), [m](auto xj, auto wxj) {
      return xj + wxj * m;
    });
  }
  auto sum = nn.wyb;
  for (auto wyi = cbegin(nn.wy), xi = cbegin(x); wyi < cend(nn.wy); wyi++, xi++) {
    sum += *wyi / (1 + std::exp(-*xi));
  }
  return sum;
}

auto calculate_di_basic(std::array<double, 11> const& movs) -> double
{
  return calculate_di(nn_basic, movs);
}

auto calculate_di_advanced(std::array<double, 5> const& movs) -> double
{
  return calculate_di(nn_advanced, movs);
}

auto calculate_odg(double distortion_index) -> double
{
  return bmin + (bmax - bmin) / (1 + std::exp(-distortion_index));
}

} // namespace peaq
