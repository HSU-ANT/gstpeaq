/* GstPEAQ
 * Copyright (C) 2006, 2011, 2013, 2014, 2015
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * fftearmodel.h: FFT-based peripheral ear model part.
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

#ifndef __FFTEARMODEL_H__
#define __FFTEARMODEL_H__ 1

#include "earmodel.h"

#include <gst/fft/gstfftf64.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <complex>
#include <memory>
#include <numeric>

namespace peaq {

template<std::size_t BANDCOUNT = 109>
class FFTEarModel : public EarModelBase<BANDCOUNT>
{
public:
  static constexpr size_t FRAME_SIZE = 2048;
  static constexpr size_t STEP_SIZE = FRAME_SIZE / 2;
  struct state_t
  {
    using earmodel_t = FFTEarModel;
    [[nodiscard]] auto const& get_excitation() const { return excitation; }
    [[nodiscard]] auto const& get_unsmeared_excitation() const
    {
      return unsmeared_excitation;
    }
    [[nodiscard]] auto const& get_power_spectrum() const { return power_spectrum; }
    [[nodiscard]] auto const& get_weighted_power_spectrum() const
    {
      return weighted_power_spectrum;
    }
    [[nodiscard]] auto is_energy_threshold_reached() const
    {
      return energy_threshold_reached;
    }

  private:
    friend class FFTEarModel;
    std::array<double, BANDCOUNT> filtered_excitation{};
    std::array<double, BANDCOUNT> unsmeared_excitation;
    std::array<double, BANDCOUNT> excitation;
    std::array<double, FRAME_SIZE / 2 + 1> power_spectrum;
    std::array<double, FRAME_SIZE / 2 + 1> weighted_power_spectrum;
    bool energy_threshold_reached;
  };
  [[nodiscard]] static auto constexpr get_band_count() { return BANDCOUNT; }
  FFTEarModel();
  [[nodiscard]] auto get_playback_level() const
  {
    return 10.0 * std::log10(level_factor * 8. / 3. * (GAMMA / 4 * (FRAME_SIZE - 1)) *
                             (GAMMA / 4 * (FRAME_SIZE - 1)));
  }
  void set_playback_level(double level)
  {
    /* level_factor is the square of fac/N in [BS1387], which equals G_Li/N_F in
     * [Kabal03] except for a factor of sqrt(8/3) which is part of the Hann
     * window in [BS1387] but not in [Kabal03]; see [Kabal03] for the derivation
     * of the denominator and the meaning of GAMMA */
    level_factor = std::pow(10, level / 10.0) / (8. / 3. * (GAMMA / 4 * (FRAME_SIZE - 1)) *
                                                 (GAMMA / 4 * (FRAME_SIZE - 1)));
  }
  [[nodiscard]] auto const& get_masking_difference() const { return masking_difference; }

  template<typename InputIterator>
  void process_block(state_t& state, InputIterator samples) const;
  [[nodiscard]] auto group_into_bands(
    std::array<double, FRAME_SIZE / 2 + 1> const& spectrum) const
  {
    auto band_power = std::array<double, BANDCOUNT>{};
    for (std::size_t i = 0; i < BANDCOUNT; i++) {
      auto edge_power = band_lower_weight[i] * spectrum[band_lower_end[i]] +
                        band_upper_weight[i] * spectrum[band_upper_end[i]];
      auto power = std::accumulate(cbegin(spectrum) + band_lower_end[i] + 1,
                                   cbegin(spectrum) + band_upper_end[i],
                                   edge_power);
      band_power[i] = std::max(power, 1e-12);
    }
    return band_power;
  }
  [[nodiscard]] auto calc_time_constant(std::size_t band,
                                        double tau_min,
                                        double tau_100) const
  {
    return EarModelBase<BANDCOUNT>::calc_time_constant(band, tau_min, tau_100, STEP_SIZE);
  }

private:
  static constexpr auto TAU_MIN = 0.008;
  static constexpr auto TAU_100 = 0.030;
  static constexpr double LOUDNESS_SCALE = 1.07664;
  static constexpr double DELTA_Z = 27. / (BANDCOUNT - 1);
  static constexpr double GAMMA = 0.84971762641205;
  static const double lower_spreading;
  static const double lower_spreading_exponentiated;
  static const std::array<double, FRAME_SIZE> hann_window;
  static const std::array<double, BANDCOUNT> masking_difference;

  std::unique_ptr<GstFFTF64, decltype(&gst_fft_f64_free)> gstfft{
    gst_fft_f64_new(FRAME_SIZE, FALSE),
    &gst_fft_f64_free
  };
  std::array<double, FRAME_SIZE / 2 + 1> outer_middle_ear_weight;
  double level_factor;
  std::array<std::size_t, BANDCOUNT> band_lower_end;
  std::array<std::size_t, BANDCOUNT> band_upper_end;
  std::array<double, BANDCOUNT> band_lower_weight;
  std::array<double, BANDCOUNT> band_upper_weight;
  std::array<double, BANDCOUNT> spreading_normalization;
  std::array<double, BANDCOUNT> aUC;
  std::array<double, BANDCOUNT> gIL;
  [[nodiscard]] auto do_spreading(std::array<double, BANDCOUNT> const& Pp) const
    -> std::array<double, BANDCOUNT>;
};

template<std::size_t BANDCOUNT>
const double FFTEarModel<BANDCOUNT>::lower_spreading = std::pow(10.,
                                                                -2.7 *
                                                                  DELTA_Z); /* 1 / a_L */
template<std::size_t BANDCOUNT>
const double FFTEarModel<BANDCOUNT>::lower_spreading_exponentiated =
  std::pow(lower_spreading, 0.4);

template<std::size_t BANDCOUNT>
const std::array<double, FFTEarModel<BANDCOUNT>::FRAME_SIZE>
  FFTEarModel<BANDCOUNT>::hann_window = [] {
    auto win = std::array<double, FRAME_SIZE>{};
    for (std::size_t k = 0; k < win.size(); k++) {
      win[k] = std::sqrt(8. / 3.) * 0.5 * (1. - std::cos(2 * M_PI * k / (win.size() - 1)));
    }
    return win;
  }();

template<std::size_t BANDCOUNT>
const std::array<double, BANDCOUNT> FFTEarModel<BANDCOUNT>::masking_difference = [] {
  auto masking_difference = std::array<double, BANDCOUNT>{};
  for (std::size_t band = 0; band < masking_difference.size(); band++) {
    /* masking weighting function; (25) in [BS1387], (112) in [Kabal03] */
    masking_difference[band] =
      pow(10., (band * DELTA_Z <= 12. ? 3. : 0.25 * band * DELTA_Z) / 10.);
  }
  return masking_difference;
}();

template<std::size_t BANDCOUNT>
FFTEarModel<BANDCOUNT>::FFTEarModel()
{
  /* pre-compute weighting coefficients for outer and middle ear weighting
   * function; (7) in [BS1387], (6) in [Kabal03], but taking the squared value
   * for applying in the power domain */
  auto sampling_rate = this->get_sampling_rate();
  for (std::size_t k = 0; k <= FRAME_SIZE / 2; k++) {
    outer_middle_ear_weight[k] =
      std::pow(this->calc_ear_weight(double(k) * sampling_rate / FRAME_SIZE), 2);
  }
  set_playback_level(92);

  auto zL = 7. * std::asinh(80. / 650.);
  auto zU = 7. * std::asinh(18000. / 650.);
  assert(BANDCOUNT == std::ceil((zU - zL) / DELTA_Z));

  auto fc = std::array<double, BANDCOUNT>{};
  for (std::size_t band = 0; band < BANDCOUNT; band++) {
    auto zl = zL + band * DELTA_Z;
    auto zu = std::min(zU, zL + double(band + 1) * DELTA_Z);
    auto zc = (zu + zl) / 2.;
    auto curr_fc = 650. * std::sinh(zc / 7.);
    fc[band] = curr_fc;

    /* pre-compute helper data for group_into_bands()
     * The precomputed data is as proposed in [Kabal03], but the
     * algorithm to compute is somewhat simplified */
    auto fl = 650. * std::sinh(zl / 7.);
    auto fu = 650. * std::sinh(zu / 7.);
    band_lower_end[band] = std::lround(fl / sampling_rate * FRAME_SIZE);
    band_upper_end[band] = std::lround(fu / sampling_rate * FRAME_SIZE);
    auto upper_freq = (2 * band_lower_end[band] + 1) / 2. * sampling_rate / FRAME_SIZE;
    if (upper_freq > fu) {
      upper_freq = fu;
    }
    auto U = upper_freq - fl;
    band_lower_weight[band] = U * FRAME_SIZE / sampling_rate;
    if (band_lower_end[band] == band_upper_end[band]) {
      band_upper_weight[band] = 0;
    } else {
      auto lower_freq = (2 * band_upper_end[band] - 1) / 2. * sampling_rate / FRAME_SIZE;
      U = fu - lower_freq;
      band_upper_weight[band] = U * FRAME_SIZE / sampling_rate;
    }

    /* pre-compute internal noise, time constants for time smearing,
     * thresholds and helper data for spreading */
    const auto aL = lower_spreading;
    aUC[band] = std::pow(10., (-2.4 - 23. / curr_fc) * DELTA_Z);
    gIL[band] = (1. - std::pow(aL, band + 1)) / (1. - aL);
    spreading_normalization[band] = 1.;
  }

  this->precompute_constants(fc, LOUDNESS_SCALE, TAU_MIN, TAU_100, STEP_SIZE);

  spreading_normalization = do_spreading(spreading_normalization);
}

template<std::size_t BANDCOUNT>
template<typename InputIterator>
void FFTEarModel<BANDCOUNT>::process_block(state_t& state, InputIterator samples) const
{
  /* apply a Hann window to the input data frame; (3) in [BS1387], part of (4)
     * in [Kabal03] */
  auto windowed_data = std::array<double, FRAME_SIZE>{};
  std::transform(cbegin(hann_window),
                 cend(hann_window),
                 samples,
                 begin(windowed_data),
                 std::multiplies<>());

  /* apply FFT to windowed data; (4) in [BS1387] and part of (4) in [Kabal03],
     * but without division by FRAME_SIZE, which is subsumed in the
     * level_factor applied next */
  auto fftoutput = std::array<std::complex<double>, FRAME_SIZE / 2 + 1>{};
  gst_fft_f64_fft(gstfft.get(),
                  windowed_data.data(),
                  reinterpret_cast<GstFFTF64Complex*>(fftoutput.data()));

  for (std::size_t k = 0; k < FRAME_SIZE / 2 + 1; k++) {
    /* compute power spectrum and apply scaling depending on playback level; in
       * [BS1387], the scaling is applied on the magnitudes, so the factor is
       * squared when comparing to [BS1387] (and also includes the squared
       * division by FRAME_SIZE) */
    state.power_spectrum[k] = std::norm(fftoutput[k]) * level_factor;

    /* apply outer and middle ear weighting; (9) in [BS1387] (but in the power
       * domain), (8) in [Kabal03] */
    state.weighted_power_spectrum[k] = state.power_spectrum[k] * outer_middle_ear_weight[k];
  }

  /* group the outer ear weighted FFT outputs into critical bands according to
     * section 2.1.5 of [BS1387] / section 2.6 of [Kabal03] */
  auto band_power = group_into_bands(state.weighted_power_spectrum);

  /* add the internal noise to obtain the pitch patters; (14) in [BS1387], (17)
     * in [Kabal03] */
  auto noisy_band_power = std::array<double, BANDCOUNT>{};
  for (std::size_t i = 0; i < BANDCOUNT; i++) {
    noisy_band_power[i] = band_power[i] + this->get_internal_noise(i);
  }

  /* do (frequency) spreading according to section 2.1.7 in [BS1387] / section
     * 2.8 in [Kabal03] */
  state.unsmeared_excitation = do_spreading(noisy_band_power);

  /* do time domain spreading according to section 2.1.8 of [BS1387] / section
     * 2.9 of [Kabal03]
     * NOTE: according to [BS1387], the filtered_excitation after processing the
     * first frame should be all zero; we follow the interpretation of [Kabal03]
     * and only initialize to zero before the first frame. */
  for (std::size_t i = 0; i < BANDCOUNT; i++) {
    auto a = this->get_ear_time_constant(i);
    state.filtered_excitation[i] =
      a * state.filtered_excitation[i] + (1. - a) * state.unsmeared_excitation[i];
    state.excitation[i] =
      std::max(state.filtered_excitation[i], state.unsmeared_excitation[i]);
  }

  /* check whether energy threshold has been reached, see section 5.2.4.3 in
     * [BS1387] */
  auto energy = std::accumulate(samples + FRAME_SIZE / 2,
                                samples + FRAME_SIZE,
                                0.0,
                                [](auto e, auto x) { return e + x * x; });
  state.energy_threshold_reached = energy >= 8000. / (32768. * 32768.);
}

template<std::size_t BANDCOUNT>
auto FFTEarModel<BANDCOUNT>::do_spreading(std::array<double, BANDCOUNT> const& Pp) const
  -> std::array<double, BANDCOUNT>
{
  /* this computation follows the algorithm in [Kabal03] where the
   * correspondances between variables in the code and in [Kabal[03] are as
   * follows:
   *
   *    code     | [Kabal03]
   *    ---------+----------
   *    aLe      | a_L^-0.4
   *    aUCE     | a_Ua_C[l]a_E(E)
   *    gIU      | (1-(a_Ua_C[l]a_E(E))^(N_c-l)) / (1-a_Ua_C[l]a_E(E))
   *    En       | E[l] / A(l,E)
   *    aUCEe    | (a_Ua_C[l]a_E(E))^0.4
   *    Ene      | (E[l] / A(l,E))^0.4
   *    E2       | Es[l]
   */
  auto aUCEe = std::array<double, BANDCOUNT>{};
  auto Ene = std::array<double, BANDCOUNT>{};
  const auto aLe = lower_spreading_exponentiated;

  for (std::size_t i = 0; i < BANDCOUNT; i++) {
    /* from (23) in [Kabal03] */
    auto aUCE = aUC[i] * std::pow(Pp[i], 0.2 * DELTA_Z);
    /* part of (24) in [Kabal03] */
    auto gIU = (1. - std::pow(aUCE, BANDCOUNT - i)) / (1. - aUCE);
    /* Note: (24) in [Kabal03] is wrong; indeed it gives A(l,E) instead of
     * A(l,E)^-1 */
    auto En = Pp[i] / (gIL[i] + gIU - 1.);
    aUCEe[i] = std::pow(aUCE, 0.4);
    Ene[i] = std::pow(En, 0.4);
  }

  auto E2 = std::array<double, BANDCOUNT>{};
  /* first fill E2 with E_sL according to (28) in [Kabal03] */
  E2[BANDCOUNT - 1] = Ene[BANDCOUNT - 1];
  for (auto i = BANDCOUNT - 1; i > 0; i--) {
    E2[i - 1] = aLe * E2[i] + Ene[i - 1];
  }
  /* now add E_sU to E2 according to (27) in [Kabal03] (with rearranged ordering) */
  for (std::size_t i = 0; i < BANDCOUNT - 1; i++) {
    std::transform(cbegin(E2) + i + 1,
                   cend(E2),
                   begin(E2) + i + 1,
                   [&r = Ene[i], aUCEe_i = aUCEe[i]](auto E2j) {
                     r *= aUCEe_i;
                     return E2j + r;
                   });
  }
  /* compute end result by normalizing according to (25) in [Kabal03] */
  std::transform(
    cbegin(E2),
    cend(E2),
    cbegin(spreading_normalization),
    begin(E2),
    [](auto E2i, auto spread_norm) { return std::pow(E2i, 1.0 / 0.4) / spread_norm; });
  return E2;
}

} // namespace peaq

/**
 * SECTION:fftearmodel
 * @short_description: Transforms a time domain signal into pitch domain
 * excitation patterns.
 * @title: PeaqFFTEarModel
 *
 * The main processing is performed by calling peaq_earmodel_process_block().
 * The first step is to
 * apply a Hann window and transform the frame to the frequency domain. Then, a
 * filter modelling the effects of the outer and middle ear is applied by
 * weighting the spectral coefficients. These are grouped into frequency bands
 * of one fourth or half the width of the critical bands of auditoriy
 * perception to reach the pitch domain. To model the internal noise of the
 * ear, a pitch-dependent term is added to the power in each band. Finally,
 * spreading in the pitch domain and smearing in the time-domain are performed.
 * The necessary state information for the time smearing is stored in the state
 * data passed between successive calls to peaq_earmodel_process_block().
 *
 * The computation is thoroughly described in section 2 of
 * <xref linkend="Kabal03" />.
 */

/*
 * process_block:
 * @model: the #FFTEarModel instance structure.
 * @state: the state data
 * @sample_data: pointer to a frame of #FRAME_SIZE samples to be processed.
 *
 * Performs the computation described in section 2.1 of <xref linkend="BS1387"
 * /> and section 2 of <xref linkend="Kabal03" /> for one single frame of
 * single-channel data. The input is assumed to be sampled at 48 kHz. To follow
 * the specification, the frames of successive invocations of
 * peaq_ear_process() have to overlap by 50%.
 *
 * The first step is to apply a Hann window to the input frame and transform
 * it to the frequency domain using FFT. The squared magnitude of the frequency
 * coefficients
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <mi>F</mi>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />,
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>G</mi><mi>L</mi></msub>
 *   <mo>&InvisibleTimes;</mo>
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <mi>X</mi>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />) up to half the frame length are stored in
 * <structfield>power_spectrum</structfield> of @output. Next, the outer and
 * middle ear weights are applied in the frequency domain and the result
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="(" close=")">
 *       <mrow>
 *         <msub> <mi>F</mi> <mi>e</mi> </msub>
 *         <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       </mrow>
 *     </mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />,
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <msub><mi>X</mi><mi>w</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />) is stored in
 * <structfield>weighted_power_spectrum</structfield> of @output. These
 * weighted spectral coefficients are then grouped into critical bands, the
 * internal noise is added and the result is spread over frequecies to obtain
 * the unsmeared excitation patterns
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub> <mi>E</mi> <mn>2</mn> </msub>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />,
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub> <mi>E</mi> <mn>s</mn> </msub>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />), which are is stored in
 * <structfield>unsmeared_excitation</structfield> of @output.
 * Finally, further spreading over time yields the excitation patterns
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <mi>E</mi>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />,
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub> <mover accent="true"><mi>E</mi><mo>~</mo></mover> <mn>s</mn> </msub>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />), which are is stored in
 * <structfield>excitation</structfield> of @output.
 */

/**
 * peaq_fftearmodel_get_power_spectrum:
 * @state: The #PeaqFFTEarModel's state from which to obtain the current power
 * spectrum.
 *
 * Returns the power spectrum as computed during the last call to
 * peaq_earmodel_process_block() with the given @state.
 *
 * Returns: The power spectrum, up to half the sampling rate
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="|" close="|"><mrow><mi>F</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msubsup><mi>G</mi><mi>L</mi><mn>2</mn></msubsup><mo>&InvisibleTimes;</mo><msup><mfenced open="|" close="|"><mrow><mi>X</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 */

/**
 * peaq_fftearmodel_get_masking_difference:
 * @model: The #PeaqFFTEarModel instance structure.
 *
 * Returns the masking weighting function <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mn>10</mn><mfrac><mrow><mi>m</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow><mn>10</mn></mfrac></msup>
 * </math></inlineequation>
 * with
 * <informalequation><math display="block" xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>m</mi>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   <mo>=</mo>
 *   <mfenced open="{" close="">
 *     <mtable>
 *       <mtr>
 *         <mtd> <mn>3.0</mn> </mtd>
 *         <mtd>
 *           <mtext>for </mtext><mspace width="1ex" />
 *           <mi>k</mi> <mo>&sdot;</mo> <mi>res</mi> <mo>&le;</mo> <mn>12</mn>
 *         </mtd>
 *       </mtr>
 *       <mtr>
 *         <mtd>
 *           <mn>0.25</mn><mo>&sdot;</mo><mi>k</mi><mo>&sdot;</mo><mi>res</mi>
 *         </mtd>
 *         <mtd>
 *           <mtext>for </mtext><mspace width="1ex" />
 *           <mi>k</mi> <mo>&sdot;</mo> <mi>res</mi> <mo>&gt;</mo> <mn>12</mn>
 *         </mtd>
 *       </mtr>
 *     </mtable>
 *   </mfenced>
 * </math></informalequation>
 * for all bands <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>k</mi>
 * </math></inlineequation>.
 *
 * Returns: The masking weighting function <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mn>10</mn><mfrac><mrow><mi>m</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow><mn>10</mn></mfrac></msup>
 * </math></inlineequation>.
 * The pointer points to internal data of the PeaqFFTEarModel and must not be
 * freed.
 */

/**
 * peaq_fftearmodel_get_weighted_power_spectrum:
 * @state: The #PeaqFFTEarModel's state from which to obtain the current
 * weighted power spectrum.
 *
 * Returns the power spectrum weighted with the outer ear weighting function as
 * computed during the last call to peaq_earmodel_process_block() with the
 * given @state.
 *
 * Returns: The weighted power spectrum, up to half the sampling rate
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="(" close=")"><mrow><msub> <mi>F</mi> <mi>e</mi> </msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="|" close="|"><mrow><msub><mi>X</mi><mi>w</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 */

/**
 * peaq_fftearmodel_is_energy_threshold_reached:
 * @state: The #PeaqFFTEarModel's state to query whether the energy threshold
 * has been reached.
 *
 * Returns whether the last frame processed with to
 * peaq_earmodel_process_block() with the given @state reached the energy
 * threshold described in section 5.2.4.3 of in <xref linkend="BS1387" />.
 *
 * Returns: TRUE if the energy threshold was reached, FALSE otherwise.
 */

#endif
