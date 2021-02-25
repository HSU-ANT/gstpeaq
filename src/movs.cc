/* GstPEAQ
 * Copyright (C) 2013, 2014, 2015, 2021 Martin Holters <martin.holters@hsu-hh.de>
 *
 * movs.h: Model Output Variables.
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

/**
 * SECTION:movs
 * @short_description: Model output variable calculation.
 * @title: MOVs
 *
 * The functions herein are used to calculate the model output variables
 * (MOVs). They have to be called once per frame and use one or more given
 * #MovAccum instances to accumulate the MOV. Note that the #MovAccum
 * instances have to be set up correctly to perform the correct type of
 * accumulation.
 */

#include "movs.h"

#include <gst/fft/gstfftf64.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>

namespace peaq {
auto constexpr ONE_POINT_FIVE_DB_POWER_FACTOR = 1.41253754462275;

template<typename EarModel>
static auto calc_noise_loudness(
  double alpha,
  double thres_fac,
  double S0,
  double NLmin,
  EarModel const& ear_model,
  typename ModulationProcessor<EarModel::get_band_count()>::state_t const&
    ref_mod_proc_state,
  typename ModulationProcessor<EarModel::get_band_count()>::state_t const&
    test_mod_proc_state,
  std::array<double, EarModel::get_band_count()> const& ref_excitation,
  std::array<double, EarModel::get_band_count()> const& test_excitation)
{
  auto noise_loudness = 0.0;
  auto band_count = ear_model.get_band_count();
  auto const& ref_modulation = ref_mod_proc_state.get_modulation();
  auto const& test_modulation = test_mod_proc_state.get_modulation();
  for (std::size_t i = 0; i < band_count; i++) {
    /* (67) in [BS1387] */
    auto sref = thres_fac * ref_modulation[i] + S0;
    auto stest = thres_fac * test_modulation[i] + S0;
    auto ethres = ear_model.get_internal_noise(i);
    auto ep_ref = ref_excitation[i];
    auto ep_test = test_excitation[i];
    /* (68) in [BS1387] */
    auto beta = exp(-alpha * (ep_test - ep_ref) / ep_ref);
    /* (66) in [BS1387] */
    noise_loudness += std::pow(ethres / stest, 0.23) *
                      (std::pow(1. + std::max(stest * ep_test - sref * ep_ref, 0.) /
                                       (ethres + sref * ep_ref * beta),
                                0.23) -
                       1.);
  }
  noise_loudness *= 24. / band_count;
  if (noise_loudness < NLmin) {
    noise_loudness = 0.;
  }
  return noise_loudness;
}

template<typename EarModel>
auto calc_modulation_difference(
  EarModel const& ear_model,
  typename ModulationProcessor<EarModel::get_band_count()>::state_t const&
    ref_mod_proc_state,
  typename ModulationProcessor<EarModel::get_band_count()>::state_t const&
    test_mod_proc_state,
  double levWt)
{
  auto band_count = ear_model.get_band_count();
  auto const& modulation_ref = ref_mod_proc_state.get_modulation();
  auto const& modulation_test = test_mod_proc_state.get_modulation();
  auto const& average_loudness_ref = ref_mod_proc_state.get_average_loudness();

  auto mod_diff_1b = 0.;
  auto mod_diff_2b = 0.;
  auto temp_wt = 0.;
  for (std::size_t i = 0; i < band_count; i++) {
    auto diff = ABS(modulation_ref[i] - modulation_test[i]);
    /* (63) in [BS1387] with negWt = 1, offset = 1 */
    mod_diff_1b += diff / (1. + modulation_ref[i]);
    /* (63) in [BS1387] with negWt = 0.1, offset = 0.01 */
    auto w = modulation_test[i] >= modulation_ref[i] ? 1. : .1;
    mod_diff_2b += w * diff / (0.01 + modulation_ref[i]);
    /* (65) in [BS1387] with levWt = 100 if more than one accumulator is
       given, 1 otherwise */
    temp_wt +=
      average_loudness_ref[i] /
      (average_loudness_ref[i] + levWt * std::pow(ear_model.get_internal_noise(i), 0.3));
  }
  return std::make_tuple(mod_diff_1b, mod_diff_2b, temp_wt);
}

void mov_modulation_difference(
  FFTEarModel<> const& ear_model,
  std::vector<ModulationProcessor<109>::state_t> const& ref_mod_proc_state,
  std::vector<ModulationProcessor<109>::state_t> const& test_mod_proc_state,
  movaccum_avg& mov_accum1,
  movaccum_avg& mov_accum2,
  movaccum_avg_window& mov_accum_win)
{
  auto band_count = ear_model.get_band_count();

  for (std::size_t c = 0; c < mov_accum1.get_channels(); c++) {
    auto [mod_diff_1b, mod_diff_2b, temp_wt] = calc_modulation_difference(
      ear_model, ref_mod_proc_state[c], test_mod_proc_state[c], 100.);
    mod_diff_1b *= 100. / band_count;
    mod_diff_2b *= 100. / band_count;
    mov_accum1.accumulate(c, mod_diff_1b, temp_wt);
    mov_accum2.accumulate(c, mod_diff_2b, temp_wt);
    mov_accum_win.accumulate(c, mod_diff_1b, 1.);
  }
}

void mov_modulation_difference(
  FilterbankEarModel const& ear_model,
  std::vector<ModulationProcessor<40>::state_t> const& ref_mod_proc_state,
  std::vector<ModulationProcessor<40>::state_t> const& test_mod_proc_state,
  movaccum_rms& mov_accum1)
{
  auto band_count = ear_model.get_band_count();

  for (std::size_t c = 0; c < mov_accum1.get_channels(); c++) {
    auto [mod_diff_1b, mod_diff_2b, temp_wt] = calc_modulation_difference(
      ear_model, ref_mod_proc_state[c], test_mod_proc_state[c], 1.);
    mod_diff_1b *= 100. / std::sqrt(band_count);
    mod_diff_2b *= 100. / band_count;
    mov_accum1.accumulate(c, mod_diff_1b, temp_wt);
  }
}

void mov_noise_loudness(
  FFTEarModel<> const& ear_model,
  std::vector<ModulationProcessor<109>::state_t> const& ref_mod_proc_state,
  std::vector<ModulationProcessor<109>::state_t> const& test_mod_proc_state,
  std::vector<LevelAdapter<109>::state_t> const& level_state,
  movaccum_rms& mov_accum)
{
  for (std::size_t c = 0; c < mov_accum.get_channels(); c++) {
    auto const& ref_excitation = level_state[c].get_adapted_ref();
    auto const& test_excitation = level_state[c].get_adapted_test();
    auto noise_loudness = calc_noise_loudness(1.5,
                                              0.15,
                                              0.5,
                                              0.,
                                              ear_model,
                                              ref_mod_proc_state[c],
                                              test_mod_proc_state[c],
                                              ref_excitation,
                                              test_excitation);
    mov_accum.accumulate(c, noise_loudness, 1.);
  }
}

void mov_noise_loud_asym(
  FilterbankEarModel const& ear_model,
  std::vector<ModulationProcessor<40>::state_t> const& ref_mod_proc_state,
  std::vector<ModulationProcessor<40>::state_t> const& test_mod_proc_state,
  std::vector<LevelAdapter<40>::state_t> const& level_state,
  movaccum_rms_asym& mov_accum)
{
  for (std::size_t c = 0; c < mov_accum.get_channels(); c++) {
    auto const& ref_excitation = level_state[c].get_adapted_ref();
    auto const& test_excitation = level_state[c].get_adapted_test();
    auto noise_loudness = calc_noise_loudness(2.5,
                                              0.3,
                                              1.,
                                              0.1,
                                              ear_model,
                                              ref_mod_proc_state[c],
                                              test_mod_proc_state[c],
                                              ref_excitation,
                                              test_excitation);
#if defined(SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS) &&                                     \
  SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS
    auto missing_components = calc_noise_loudness(1.5,
                                                  0.15,
                                                  1.,
                                                  0.,
                                                  ear_model,
                                                  test_mod_proc_state[c],
                                                  ref_mod_proc_state[c],
                                                  test_excitation,
                                                  ref_excitation);
#else
    auto missing_components = calc_noise_loudness(1.5,
                                                  0.15,
                                                  1.,
                                                  0.,
                                                  ear_model,
                                                  ref_mod_proc_state[c],
                                                  test_mod_proc_state[c],
                                                  test_excitation,
                                                  ref_excitation);
#endif
    mov_accum.accumulate(c, noise_loudness, missing_components);
  }
}

void mov_lin_dist(FilterbankEarModel const& ear_model,
                  std::vector<ModulationProcessor<40>::state_t> const& ref_mod_proc_state,
                  std::vector<LevelAdapter<40>::state_t> const& level_state,
                  std::vector<FilterbankEarModel::state_t> const& state,
                  movaccum_avg& mov_accum)
{
  for (std::size_t c = 0; c < mov_accum.get_channels(); c++) {
    auto const& ref_adapted_excitation = level_state[c].get_adapted_ref();
    auto const& ref_excitation = state[c].get_excitation();
#if defined(SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS) &&                                     \
  SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS
    auto noise_loudness = calc_noise_loudness(1.5,
                                              0.15,
                                              1.,
                                              0.,
                                              ear_model,
                                              ref_mod_proc_state[c],
                                              ref_mod_proc_state[c],
                                              ref_adapted_excitation,
                                              ref_excitation);
#else
    auto noise_loudness = calc_noise_loudness(1.5,
                                              0.15,
                                              1.,
                                              0.,
                                              ref_mod_proc_state[c],
                                              test_mod_proc_state[c],
                                              ref_adapted_excitation,
                                              ref_excitation);
#endif
    mov_accum.accumulate(c, noise_loudness, 1.);
  }
}

void mov_bandwidth(std::vector<FFTEarModel<>::state_t> const& ref_state,
                   std::vector<FFTEarModel<>::state_t> const& test_state,
                   movaccum_avg& mov_accum_ref,
                   movaccum_avg& mov_accum_test)
{
  auto constexpr FIVE_DB_POWER_FACTOR = 3.16227766016838;
  for (std::size_t c = 0; c < mov_accum_ref.get_channels(); c++) {
    auto const& ref_power_spectrum = ref_state[c].get_power_spectrum();
    auto const& test_power_spectrum = test_state[c].get_power_spectrum();
    auto zero_threshold = std::accumulate(cbegin(test_power_spectrum) + 922,
                                          cbegin(test_power_spectrum) + 1024,
                                          test_power_spectrum[921],
                                          [](auto x, auto y) { return std::max(x, y); });
    auto bw_ref = 0;
    for (auto i = 921; i > 0; i--) {
      if (ref_power_spectrum[i - 1] > 10. * zero_threshold) {
        bw_ref = i;
        break;
      }
    }
    if (bw_ref > 346) {
      auto bw_test = 0;
      for (auto i = bw_ref; i > 0; i--) {
        if (test_power_spectrum[i - 1] >= FIVE_DB_POWER_FACTOR * zero_threshold) {
          bw_test = i;
          break;
        }
      }
      mov_accum_ref.accumulate(c, bw_ref, 1.);
      mov_accum_test.accumulate(c, bw_test, 1.);
    }
  }
}

template<typename EarModel>
static auto calc_nmr(EarModel const& ear_model,
                     typename EarModel::state_t const& ref_state,
                     typename EarModel::state_t const& test_state)
{
  auto constexpr band_count = EarModel::get_band_count();
  auto const& ref_weighted_power_spectrum = ref_state.get_weighted_power_spectrum();
  auto const& test_weighted_power_spectrum = test_state.get_weighted_power_spectrum();
  std::array<double, 1025> noise_spectrum;

  std::transform(
    cbegin(ref_weighted_power_spectrum),
    cend(ref_weighted_power_spectrum),
    cbegin(test_weighted_power_spectrum),
    begin(noise_spectrum),
    [](auto ref, auto test) { return ref - 2 * std::sqrt(ref * test) + test; });

  auto noise_in_bands = std::array<double, band_count>();
  ear_model.group_into_bands(noise_spectrum, noise_in_bands);

  auto const& ref_excitation = ref_state.get_excitation();
  auto const& masking_difference = ear_model.get_masking_difference();
  auto nmr = 0.;
  auto nmr_max = 0.;
  for (std::size_t i = 0; i < band_count; i++) {
    /* (26) in [BS1387] */
    auto mask = ref_excitation[i] / masking_difference[i];
    /* (70) in [BS1387], except for conversion to dB in the end */
    auto curr_nmr = noise_in_bands[i] / mask;
    nmr += curr_nmr;
    /* for Relative Disturbed Frames */
    if (curr_nmr > nmr_max) {
      nmr_max = curr_nmr;
    }
  }
  nmr /= band_count;
  return std::make_tuple(nmr, nmr_max);
}

void mov_nmr(FFTEarModel<> const& ear_model,
             std::vector<FFTEarModel<>::state_t> const& ref_state,
             std::vector<FFTEarModel<>::state_t> const& test_state,
             movaccum_avg_log& mov_accum_nmr,
             movaccum_avg& mov_accum_rel_dist_frames)
{
  for (std::size_t c = 0; c < mov_accum_nmr.get_channels(); c++) {
    auto [nmr, nmr_max] = calc_nmr(ear_model, ref_state[c], test_state[c]);
    mov_accum_nmr.accumulate(c, nmr, 1.);
    mov_accum_rel_dist_frames.accumulate(
      c, nmr_max > ONE_POINT_FIVE_DB_POWER_FACTOR ? 1. : 0., 1.);
  }
}

void mov_nmr(FFTEarModel<55> const& ear_model,
             std::vector<FFTEarModel<55>::state_t> const& ref_state,
             std::vector<FFTEarModel<55>::state_t> const& test_state,
             movaccum_avg& mov_accum_nmr)
{
  for (std::size_t c = 0; c < mov_accum_nmr.get_channels(); c++) {
    auto [nmr, nmr_max] = calc_nmr(ear_model, ref_state[c], test_state[c]);
    mov_accum_nmr.accumulate(c, 10. * log10(nmr), 1.);
  }
}

void mov_prob_detect(FFTEarModel<> const& ear_model,
                     std::vector<FFTEarModel<>::state_t> const& ref_state,
                     std::vector<FFTEarModel<>::state_t> const& test_state,
                     movaccum_adb& mov_accum_adb,
                     movaccum_filtered_max& mov_accum_mfpd)
{
  auto band_count = ear_model.get_band_count();
  auto binaural_detection_probability = 1.;
  auto binaural_detection_steps = 0.;
  for (std::size_t i = 0; i < band_count; i++) {
    auto detection_probability = 0.;
    auto detection_steps = 0.;
    for (std::size_t c = 0; c < ref_state.size(); c++) {
      auto const& ref_excitation = ref_state[c].get_excitation();
      auto const& test_excitation = test_state[c].get_excitation();
      auto eref_db = 10. * std::log10(ref_excitation[i]);
      auto etest_db = 10. * std::log10(test_excitation[i]);
      /* (73) in [BS1387] */
      auto l = 0.3 * std::max(eref_db, etest_db) + 0.7 * etest_db;
      /* (74) in [BS1387] */
      auto s = l > 0. ? 5.95072 * std::pow(6.39468 / l, 1.71332) +
                          9.01033e-11 * std::pow(l, 4.) + 5.05622e-6 * std::pow(l, 3.) -
                          0.00102438 * l * l + 0.0550197 * l - 0.198719
                      : 1e30;
      /* (75) in [BS1387] */
      auto e = eref_db - etest_db;
      auto b = eref_db > etest_db ? 4. : 6.;
      /* (76) and (77) in [BS1387] simplify to this */
      auto pc = 1. - std::pow(0.5, std::pow(e / s, b));
      /* (78) in [BS1387] */
#if defined(USE_FLOOR_FOR_STEPS_ABOVE_THRESHOLD) && USE_FLOOR_FOR_STEPS_ABOVE_THRESHOLD
      auto qc = fabs(floor(e)) / s;
#else
      auto qc = std::abs(std::trunc(e)) / s;
#endif
      if (pc > detection_probability) {
        detection_probability = pc;
      }
      if (c == 0 || qc > detection_steps) {
        detection_steps = qc;
      }
    }
    binaural_detection_probability *= 1. - detection_probability;
    binaural_detection_steps += detection_steps;
  }
  binaural_detection_probability = 1. - binaural_detection_probability;
  if (binaural_detection_probability > 0.5) {
    mov_accum_adb.accumulate(0, binaural_detection_steps, 1.);
  }
  mov_accum_mfpd.accumulate(0, binaural_detection_probability, 1.);
}
} // namespace peaq
