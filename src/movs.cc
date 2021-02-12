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
 * #PeaqMovAccum instances to accumulate the MOV. Note that the #PeaqMovAccum
 * instances have to be set up correctly to perform the correct type of
 * accumulation.
 */

#include "movs.h"
#include "settings.h"

#include <gst/fft/gstfftf64.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>

namespace peaq {
auto constexpr FIVE_DB_POWER_FACTOR = 3.16227766016838;
auto constexpr ONE_POINT_FIVE_DB_POWER_FACTOR = 1.41253754462275;
auto constexpr MAXLAG = 256;

template<typename T>
static auto calc_noise_loudness(double alpha,
                                double thres_fac,
                                double S0,
                                double NLmin,
                                ModulationProcessor const* ref_mod_proc,
                                ModulationProcessor const* test_mod_proc,
                                std::vector<double> const& ref_excitation,
                                T const& test_excitation)
{
  auto noise_loudness = 0.0;
  const auto* ear_model = ref_mod_proc->get_ear_model();
  auto band_count = ear_model->get_band_count();
  auto const& ref_modulation = ref_mod_proc->get_modulation();
  auto const& test_modulation = test_mod_proc->get_modulation();
  for (std::size_t i = 0; i < band_count; i++) {
    /* (67) in [BS1387] */
    auto sref = thres_fac * ref_modulation[i] + S0;
    auto stest = thres_fac * test_modulation[i] + S0;
    auto ethres = ear_model->get_internal_noise(i);
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

void mov_modulation_difference(ModulationProcessor* const* ref_mod_proc,
                               ModulationProcessor* const* test_mod_proc,
                               PeaqMovAccum* mov_accum1,
                               PeaqMovAccum* mov_accum2,
                               PeaqMovAccum* mov_accum_win)
{
  auto* ear_model = ref_mod_proc[0]->get_ear_model();
  auto band_count = ear_model->get_band_count();

  auto levWt = mov_accum2 != nullptr ? 100. : 1.;
  for (std::size_t c = 0; c < peaq_movaccum_get_channels(mov_accum1); c++) {
    auto const& modulation_ref = ref_mod_proc[c]->get_modulation();
    auto const& modulation_test = test_mod_proc[c]->get_modulation();
    auto const& average_loudness_ref = ref_mod_proc[c]->get_average_loudness();

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
        (average_loudness_ref[i] + levWt * std::pow(ear_model->get_internal_noise(i), 0.3));
    }
    if (peaq_movaccum_get_mode(mov_accum1) == MODE_RMS) {
      mod_diff_1b *= 100. / std::sqrt(band_count);
    } else {
      mod_diff_1b *= 100. / band_count;
    }
    mod_diff_2b *= 100. / band_count;
    peaq_movaccum_accumulate(mov_accum1, c, mod_diff_1b, temp_wt);
    if (mov_accum2 != nullptr) {
      peaq_movaccum_accumulate(mov_accum2, c, mod_diff_2b, temp_wt);
    }
    if (mov_accum_win != nullptr) {
      peaq_movaccum_accumulate(mov_accum_win, c, mod_diff_1b, 1.);
    }
  }
}

void mov_noise_loudness(PeaqModulationProcessor* const* ref_mod_proc,
                        PeaqModulationProcessor* const* test_mod_proc,
                        PeaqLevelAdapter* const* level,
                        PeaqMovAccum* mov_accum)
{
  for (std::size_t c = 0; c < peaq_movaccum_get_channels(mov_accum); c++) {
    auto const& ref_excitation = level[c]->get_adapted_ref();
    auto const& test_excitation = level[c]->get_adapted_test();
    auto noise_loudness = calc_noise_loudness(1.5,
                                              0.15,
                                              0.5,
                                              0.,
                                              ref_mod_proc[c],
                                              test_mod_proc[c],
                                              ref_excitation,
                                              test_excitation);
    peaq_movaccum_accumulate(mov_accum, c, noise_loudness, 1.);
  }
}

void mov_noise_loud_asym(PeaqModulationProcessor* const* ref_mod_proc,
                         PeaqModulationProcessor* const* test_mod_proc,
                         PeaqLevelAdapter* const* level,
                         PeaqMovAccum* mov_accum)
{
  for (std::size_t c = 0; c < peaq_movaccum_get_channels(mov_accum); c++) {
    auto const& ref_excitation = level[c]->get_adapted_ref();
    auto const& test_excitation = level[c]->get_adapted_test();
    auto noise_loudness = calc_noise_loudness(2.5,
                                              0.3,
                                              1.,
                                              0.1,
                                              ref_mod_proc[c],
                                              test_mod_proc[c],
                                              ref_excitation,
                                              test_excitation);
#if defined(SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS) &&                                     \
  SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS
    auto missing_components = calc_noise_loudness(1.5,
                                                  0.15,
                                                  1.,
                                                  0.,
                                                  test_mod_proc[c],
                                                  ref_mod_proc[c],
                                                  test_excitation,
                                                  ref_excitation);
#else
    auto missing_components = calc_noise_loudness(1.5,
                                                  0.15,
                                                  1.,
                                                  0.,
                                                  ref_mod_proc[c],
                                                  test_mod_proc[c],
                                                  test_excitation,
                                                  ref_excitation);
#endif
    peaq_movaccum_accumulate(mov_accum, c, noise_loudness, missing_components);
  }
}

void mov_lin_dist(ModulationProcessor* const* ref_mod_proc,
                  PeaqEarModel* ear_model,
                  LevelAdapter* const* level,
                  const gpointer* state,
                  PeaqMovAccum* mov_accum)
{
  for (std::size_t c = 0; c < peaq_movaccum_get_channels(mov_accum); c++) {
    auto const& ref_adapted_excitation = level[c]->get_adapted_ref();
    auto const* ref_excitation =
      ear_model->get_excitation(reinterpret_cast<PeaqEarModelState*>(state[c]));
#if defined(SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS) &&                                     \
  SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS
    auto noise_loudness = calc_noise_loudness(1.5,
                                              0.15,
                                              1.,
                                              0.,
                                              ref_mod_proc[c],
                                              ref_mod_proc[c],
                                              ref_adapted_excitation,
                                              ref_excitation);
#else
    auto noise_loudness = calc_noise_loudness(1.5,
                                              0.15,
                                              1.,
                                              0.,
                                              ref_mod_proc[c],
                                              test_mod_proc[c],
                                              ref_adapted_excitation,
                                              ref_excitation);
#endif
    peaq_movaccum_accumulate(mov_accum, c, noise_loudness, 1.);
  }
}

void mov_bandwidth(FFTEarModel::state_t const* const* ref_state,
                   FFTEarModel::state_t const* const* test_state,
                   PeaqMovAccum* mov_accum_ref,
                   PeaqMovAccum* mov_accum_test)
{
  for (std::size_t c = 0; c < peaq_movaccum_get_channels(mov_accum_ref); c++) {
    auto const& ref_power_spectrum = FFTEarModel::get_power_spectrum(*ref_state[c]);
    auto const& test_power_spectrum = FFTEarModel::get_power_spectrum(*test_state[c]);
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
      peaq_movaccum_accumulate(mov_accum_ref, c, bw_ref, 1.);
      peaq_movaccum_accumulate(mov_accum_test, c, bw_test, 1.);
    }
  }
}

void mov_nmr(FFTEarModel const* ear_model,
             FFTEarModel::state_t const* const* ref_state,
             FFTEarModel::state_t const* const* test_state,
             PeaqMovAccum* mov_accum_nmr,
             PeaqMovAccum* mov_accum_rel_dist_frames)
{
  auto band_count = ear_model->get_band_count();
  for (std::size_t c = 0; c < peaq_movaccum_get_channels(mov_accum_nmr); c++) {
    auto const& ref_weighted_power_spectrum =
      FFTEarModel::get_weighted_power_spectrum(*ref_state[c]);
    auto const& test_weighted_power_spectrum =
      FFTEarModel::get_weighted_power_spectrum(*test_state[c]);
    std::array<double, 1025> noise_spectrum;

    std::transform(
      cbegin(ref_weighted_power_spectrum),
      cend(ref_weighted_power_spectrum),
      cbegin(test_weighted_power_spectrum),
      begin(noise_spectrum),
      [](auto ref, auto test) { return ref - 2 * std::sqrt(ref * test) + test; });

    auto* noise_in_bands = g_newa(double, band_count);
    ear_model->group_into_bands(noise_spectrum, noise_in_bands);

    auto const* ref_excitation = ear_model->get_excitation(ref_state[c]);
    auto const& masking_difference = ear_model->get_masking_difference();
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

    if (peaq_movaccum_get_mode(mov_accum_nmr) == MODE_AVG_LOG) {
      peaq_movaccum_accumulate(mov_accum_nmr, c, nmr, 1.);
    } else {
      peaq_movaccum_accumulate(mov_accum_nmr, c, 10. * log10(nmr), 1.);
    }
    if (mov_accum_rel_dist_frames != nullptr) {
      peaq_movaccum_accumulate(mov_accum_rel_dist_frames,
                               c,
                               nmr_max > ONE_POINT_FIVE_DB_POWER_FACTOR ? 1. : 0.,
                               1.);
    }
  }
}

void mov_prob_detect(PeaqEarModel const* ear_model,
                     const gpointer* ref_state,
                     const gpointer* test_state,
                     guint channels,
                     PeaqMovAccum* mov_accum_adb,
                     PeaqMovAccum* mov_accum_mfpd)
{
  auto band_count = ear_model->get_band_count();
  auto binaural_detection_probability = 1.;
  auto binaural_detection_steps = 0.;
  for (std::size_t i = 0; i < band_count; i++) {
    auto detection_probability = 0.;
    auto detection_steps = 0.;
    for (std::size_t c = 0; c < channels; c++) {
      auto const* ref_excitation =
        ear_model->get_excitation(reinterpret_cast<PeaqEarModelState*>(ref_state[c]));
      auto const* test_excitation =
        ear_model->get_excitation(reinterpret_cast<PeaqEarModelState*>(test_state[c]));
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
    peaq_movaccum_accumulate(mov_accum_adb, 0, binaural_detection_steps, 1.);
  }
  peaq_movaccum_accumulate(mov_accum_mfpd, 0, binaural_detection_probability, 1.);
}

static auto do_xcorr(std::array<double, 2 * MAXLAG> const& d)
{
  static GstFFTF64* correlator_fft = nullptr;
  static GstFFTF64* correlator_inverse_fft = nullptr;
  if (correlator_fft == nullptr) {
    correlator_fft = gst_fft_f64_new(2 * MAXLAG, FALSE);
  }
  if (correlator_inverse_fft == nullptr) {
    correlator_inverse_fft = gst_fft_f64_new(2 * MAXLAG, TRUE);
  }
  /*
   * the follwing uses an equivalent computation in the frequency domain to
   * determine the correlation like function:
   * for (i = 0; i < MAXLAG; i++) {
   *   c[i] = 0;
   *   for (k = 0; k < MAXLAG; k++)
   *     c[i] += d[k] * d[k + i];
   * }
   */
  std::array<double, 2 * MAXLAG> timedata;
  std::array<std::complex<double>, MAXLAG + 1> freqdata1;
  std::array<std::complex<double>, MAXLAG + 1> freqdata2;
  std::copy(cbegin(d), cend(d), begin(timedata));
  gst_fft_f64_fft(
    correlator_fft, timedata.data(), reinterpret_cast<GstFFTF64Complex*>(freqdata1.data()));
  std::fill(begin(timedata) + MAXLAG, end(timedata), 0.0);
  gst_fft_f64_fft(
    correlator_fft, timedata.data(), reinterpret_cast<GstFFTF64Complex*>(freqdata2.data()));
  /* multiply freqdata1 with the conjugate of freqdata2 and scale */
  std::transform(cbegin(freqdata1),
                 cend(freqdata1),
                 cbegin(freqdata2),
                 begin(freqdata1),
                 [](auto X1, auto X2) { return X1 * std::conj(X2) / (2.0 * MAXLAG); });
  gst_fft_f64_inverse_fft(correlator_inverse_fft,
                          reinterpret_cast<GstFFTF64Complex*>(freqdata1.data()),
                          timedata.data());
  std::array<double, MAXLAG> c;
  std::copy_n(cbegin(timedata), MAXLAG, begin(c));
  return c;
}

void mov_ehs(FFTEarModel::state_t const* const* ref_state,
             FFTEarModel::state_t const* const* test_state,
             PeaqMovAccum* mov_accum)
{
  static GstFFTF64* correlation_fft = nullptr;
  if (correlation_fft == nullptr) {
    correlation_fft = gst_fft_f64_new(MAXLAG, FALSE);
  }
  static const auto correlation_window = []() {
    std::array<double, MAXLAG> win;
    /* centering the window of the correlation in the EHS computation at lag
     * zero (as considered in [Kabal03] to be more reasonable) degrades
     * conformance */
    for (std::size_t i = 0; i < MAXLAG; i++) {
#if defined(CENTER_EHS_CORRELATION_WINDOW) && CENTER_EHS_CORRELATION_WINDOW
      win[i] = 0.81649658092773 * (1 + std::cos(2 * M_PI * i / (2 * MAXLAG - 1))) / MAXLAG;
#else
      win[i] = 0.81649658092773 * (1 - std::cos(2 * M_PI * i / (MAXLAG - 1))) / MAXLAG;
#endif
    }
    return win;
  }();

  auto channels = peaq_movaccum_get_channels(mov_accum);

  if (std::none_of(
        ref_state,
        ref_state + channels,
        [](auto state) { return FFTEarModel::is_energy_threshold_reached(*state); }) &&
      std::none_of(test_state, test_state + channels, [](auto state) {
        return FFTEarModel::is_energy_threshold_reached(*state);
      })) {
    return;
  }

  for (std::size_t chan = 0; chan < channels; chan++) {
    auto const& ref_power_spectrum =
      FFTEarModel::get_weighted_power_spectrum(*ref_state[chan]);
    auto const& test_power_spectrum =
      FFTEarModel::get_weighted_power_spectrum(*test_state[chan]);

    auto d = std::array<double, 2 * MAXLAG>{};
    std::transform(cbegin(ref_power_spectrum),
                   cbegin(ref_power_spectrum) + 2 * MAXLAG,
                   cbegin(test_power_spectrum),
                   begin(d),
                   [](auto fref, auto ftest) {
                     return fref == 0. && ftest == 0. ? 0.0 : std::log(ftest / fref);
                   });

    auto c = do_xcorr(d);

    std::array<std::complex<double>, MAXLAG / 2 + 1> c_fft;

    auto const d0 = c[0];
    auto dk = d0;
#if defined(EHS_SUBTRACT_DC_BEFORE_WINDOW) && EHS_SUBTRACT_DC_BEFORE_WINDOW
    /* in the following, the mean is subtracted before the window is applied as
     * suggested by [Kabal03], although this contradicts [BS1387]; however, the
     * results thus obtained are closer to the reference */
    auto cavg = 0.0;
    for (std::size_t i = 0; i < MAXLAG; i++) {
      c[i] /= std::sqrt(d0 * dk);
      cavg += c[i];
      dk += d[i + MAXLAG] * d[i + MAXLAG] - d[i] * d[i];
    }
    cavg /= MAXLAG;
    std::transform(
      cbegin(c), cend(c), cbegin(correlation_window), begin(c), [cavg](auto ci, auto win) {
        return (ci - cavg) * win;
      });
#else
    for (i = 0; i < MAXLAG; i++) {
      c[i] *= correlation_window[i] / sqrt(d0 * dk);
      dk += d[i + MAXLAG] * d[i + MAXLAG] - d[i] * d[i];
    }
#endif
    gst_fft_f64_fft(
      correlation_fft, c.data(), reinterpret_cast<GstFFTF64Complex*>(c_fft.data()));
#if !defined(EHS_SUBTRACT_DC_BEFORE_WINDOW) || !EHS_SUBTRACT_DC_BEFORE_WINDOW
    /* subtracting the average is equivalent to setting the DC component to
     * zero */
    c_fft[0] = 0.0;
#endif

    auto ehs = 0.0;
    auto s = std::norm(c_fft[0]);
    for (auto c_fft_i : c_fft) {
      auto new_s = std::norm(c_fft_i);
      if (new_s > s && new_s > ehs) {
        ehs = new_s;
      }
      s = new_s;
    }
    peaq_movaccum_accumulate(mov_accum, chan, 1000. * ehs, 1.);
  }
}
} // namespace peaq

void peaq_mov_modulation_difference(PeaqModulationProcessor* const* ref_mod_proc,
                                    PeaqModulationProcessor* const* test_mod_proc,
                                    PeaqMovAccum* mov_accum1,
                                    PeaqMovAccum* mov_accum2,
                                    PeaqMovAccum* mov_accum_win)
{
  peaq::mov_modulation_difference(
    ref_mod_proc, test_mod_proc, mov_accum1, mov_accum2, mov_accum_win);
}

void peaq_mov_noise_loudness(PeaqModulationProcessor* const* ref_mod_proc,
                             PeaqModulationProcessor* const* test_mod_proc,
                             PeaqLevelAdapter* const* level,
                             PeaqMovAccum* mov_accum)
{
  peaq::mov_noise_loudness(ref_mod_proc, test_mod_proc, level, mov_accum);
}

void peaq_mov_noise_loud_asym(PeaqModulationProcessor* const* ref_mod_proc,
                              PeaqModulationProcessor* const* test_mod_proc,
                              PeaqLevelAdapter* const* level,
                              PeaqMovAccum* mov_accum)
{
  peaq::mov_noise_loud_asym(ref_mod_proc, test_mod_proc, level, mov_accum);
}

void peaq_mov_lin_dist(PeaqModulationProcessor* const* ref_mod_proc,
                       PeaqModulationProcessor* const* /*test_mod_proc*/,
                       PeaqLevelAdapter* const* level,
                       const gpointer* state,
                       PeaqMovAccum* mov_accum)
{
  peaq::mov_lin_dist(
    ref_mod_proc, ref_mod_proc[0]->get_ear_model(), level, state, mov_accum);
}

void peaq_mov_bandwidth(const gpointer* ref_state,
                        const gpointer* test_state,
                        PeaqMovAccum* mov_accum_ref,
                        PeaqMovAccum* mov_accum_test)
{
  peaq::mov_bandwidth(
    reinterpret_cast<peaq::FFTEarModel::state_t const* const*>(ref_state),
    reinterpret_cast<peaq::FFTEarModel::state_t const* const*>(test_state),
    mov_accum_ref,
    mov_accum_test);
}

void peaq_mov_nmr(PeaqFFTEarModel const* ear_model,
                  const gpointer* ref_state,
                  const gpointer* test_state,
                  PeaqMovAccum* mov_accum_nmr,
                  PeaqMovAccum* mov_accum_rel_dist_frames)
{
  peaq::mov_nmr(ear_model,
                reinterpret_cast<peaq::FFTEarModel::state_t const* const*>(ref_state),
                reinterpret_cast<peaq::FFTEarModel::state_t const* const*>(test_state),
                mov_accum_nmr,
                mov_accum_rel_dist_frames);
}

void peaq_mov_prob_detect(PeaqEarModel const* ear_model,
                          const gpointer* ref_state,
                          const gpointer* test_state,
                          guint channels,
                          PeaqMovAccum* mov_accum_adb,
                          PeaqMovAccum* mov_accum_mfpd)
{
  peaq::mov_prob_detect(
    ear_model, ref_state, test_state, channels, mov_accum_adb, mov_accum_mfpd);
}

void peaq_mov_ehs(PeaqEarModel const* /*ear_model*/,
                  gpointer* ref_state,
                  gpointer* test_state,
                  PeaqMovAccum* mov_accum)
{
  peaq::mov_ehs(reinterpret_cast<peaq::FFTEarModel::state_t**>(ref_state),
                reinterpret_cast<peaq::FFTEarModel::state_t**>(test_state),
                mov_accum);
}
