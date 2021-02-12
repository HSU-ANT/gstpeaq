/* GstPEAQ
 * Copyright (C) 2006, 2007, 2012, 2013, 2015, 2021
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * level.c: Level and pattern adaptation.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>

#include <algorithm>
#include <cmath>

#include "leveladapter.h"

namespace peaq {
void LevelAdapter::set_ear_model(PeaqEarModel* ear_model)
{
  this->ear_model = ear_model;

  auto band_count = ear_model->get_band_count();

  ref_filtered_excitation.resize(band_count);
  test_filtered_excitation.resize(band_count);
  filtered_num.resize(band_count);
  filtered_den.resize(band_count);
  pattcorr_ref.resize(band_count);
  pattcorr_test.resize(band_count);
  spectrally_adapted_ref_patterns.resize(band_count);
  spectrally_adapted_test_patterns.resize(band_count);

  ear_time_constants.resize(band_count);

  /* see section 3.1 in [BS1387], section 4.1 in [Kabal03] */
  for (std::size_t k = 0; k < band_count; k++) {
    ear_time_constants[k] = ear_model->calc_time_constant(k, 0.008, 0.05);
  }
#if 0
  /* [Kabal03] suggests initialization to 1, although the standard does not
   * mention it; there seems to be almost no difference on conformance, though
   */
  for (k = 0; k < band_count; k++) {
    pattcorr_ref[k] = 1.;
    pattcorr_test[k] = 1.;
  }
#endif
}

void LevelAdapter::process(double const* ref_excitation, double const* test_excitation)
{
  std::size_t band_count = ear_model->get_band_count();
  auto* pattadapt_ref = g_newa(double, band_count);
  auto* pattadapt_test = g_newa(double, band_count);
  auto* levcorr_excitation = g_newa(double, band_count);

  auto num = 0.0;
  auto den = 0.0;
  for (std::size_t k = 0; k < band_count; k++) {
    /* (42) in [BS1387], (56) in [Kabal03] */
    ref_filtered_excitation[k] = ear_time_constants[k] * ref_filtered_excitation[k] +
                                 (1 - ear_time_constants[k]) * ref_excitation[k];
    /* (43) in [BS1387], (56) in [Kabal03] */
    test_filtered_excitation[k] = ear_time_constants[k] * test_filtered_excitation[k] +
                                  (1 - ear_time_constants[k]) * test_excitation[k];
    /* (45) in [BS1387], (57) in [Kabal03] */
    num += std::sqrt(ref_filtered_excitation[k] * test_filtered_excitation[k]);
    den += test_filtered_excitation[k];
  }
  auto lev_corr = num * num / (den * den);
  double const* levcorr_ref_excitation;
  double const* levcorr_test_excitation;
  if (lev_corr > 1) {
    levcorr_test_excitation = test_excitation;
    /* (46) in [BS1387], (58) in [Kabal03] */
    std::transform(ref_excitation,
                   ref_excitation + band_count,
                   levcorr_excitation,
                   [lev_corr](auto e) { return e / lev_corr; });
    levcorr_ref_excitation = levcorr_excitation;
  } else {
    levcorr_ref_excitation = ref_excitation;
    /* (47) in [BS1387], (58) in [Kabal03] */
    std::transform(test_excitation,
                   test_excitation + band_count,
                   levcorr_excitation,
                   [lev_corr](auto e) { return e * lev_corr; });
    levcorr_test_excitation = levcorr_excitation;
  }
  for (std::size_t k = 0; k < band_count; k++) {
    /* (48) in [BS1387], (59) in [Kabal03] */
    filtered_num[k] = ear_time_constants[k] * filtered_num[k] +
                      levcorr_test_excitation[k] * levcorr_ref_excitation[k];
    filtered_den[k] = ear_time_constants[k] * filtered_den[k] +
                      levcorr_ref_excitation[k] * levcorr_ref_excitation[k];
    /* (49) in [BS1387], (60) in [Kabal03] */
    /* these values cannot be zero [Kabal03], so the special case desribed in
     * [BS1387] is unnecessary */
    if (filtered_num[k] >= filtered_den[k]) {
      pattadapt_ref[k] = 1.;
      pattadapt_test[k] = filtered_den[k] / filtered_num[k];
    } else {
      pattadapt_ref[k] = filtered_num[k] / filtered_den[k];
      pattadapt_test[k] = 1.;
    }
  }
  for (std::size_t k = 0; k < band_count; k++) {
    /* (51) in [BS1387], (63) in [Kabal03] */
    /* dependence on band_count is an ugly hack to avoid a nasty switch/case */
    auto m1 = std::min(k, band_count / 36); /* 109 -> 3, 55 -> 1, 40 -> 1  */
    auto m2 =
      std::min(band_count - k - 1, band_count / 25); /* 109 -> 4, 55 -> 2, 40 -> 1  */
    /* (50) in [BS1387], (62) in [Kabal03] */
    auto ra_ref = 0.0;
    auto ra_test = 0.0;
    for (auto l = k - m1; l <= k + m2; l++) {
      ra_ref += pattadapt_ref[l];
      ra_test += pattadapt_test[l];
    }
    ra_ref /= (m1 + m2 + 1);
    ra_test /= (m1 + m2 + 1);
    /* (50) in [BS1387], (61) in [Kabal03] */
    pattcorr_ref[k] =
      ear_time_constants[k] * pattcorr_ref[k] + (1 - ear_time_constants[k]) * ra_ref;
    pattcorr_test[k] =
      ear_time_constants[k] * pattcorr_test[k] + (1 - ear_time_constants[k]) * ra_test;
    /* (52) in [BS1387], (64) in [Kabal03] */
    spectrally_adapted_ref_patterns[k] = levcorr_ref_excitation[k] * pattcorr_ref[k];
    /* (53) in [BS1387], (64) in [Kabal03] */
    spectrally_adapted_test_patterns[k] = levcorr_test_excitation[k] * pattcorr_test[k];
  }
}
} // namespace peaq

PeaqLevelAdapter* peaq_leveladapter_new(PeaqEarModel* ear_model)
{
  auto* level = new PeaqLevelAdapter;
  peaq_leveladapter_set_ear_model(level, ear_model);
  return level;
}

void peaq_leveladapter_delete(PeaqLevelAdapter* level)
{
  delete level;
}

void peaq_leveladapter_set_ear_model(PeaqLevelAdapter* level, PeaqEarModel* ear_model)
{
  level->set_ear_model(ear_model);
}

void peaq_leveladapter_process(PeaqLevelAdapter* level,
                               double const* ref_excitation,
                               double const* test_excitation)
{
  level->process(ref_excitation, test_excitation);
}

double const* peaq_leveladapter_get_adapted_ref(PeaqLevelAdapter const* level)
{
  return level->get_adapted_ref().data();
}

double const* peaq_leveladapter_get_adapted_test(PeaqLevelAdapter const* level)
{
  return level->get_adapted_test().data();
}
