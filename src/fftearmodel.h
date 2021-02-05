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

#ifdef __cplusplus
#include <array>
#include <memory>
#include <vector>

namespace peaq {

struct FFTEarModel
{
  static constexpr size_t FFT_FRAMESIZE = 2048;
  static constexpr double GAMMA = 0.84971762641205;
  static constexpr double LOUDNESS_SCALE = 1.07664;
  struct state_t
  {
    state_t(std::size_t band_count)
      : filtered_excitation(band_count)
      , unsmeared_excitation(band_count)
      , excitation(band_count)
    {}
    std::vector<double> filtered_excitation;
    std::vector<double> unsmeared_excitation;
    std::vector<double> excitation;
    std::array<double, FFT_FRAMESIZE / 2 + 1> power_spectrum;
    std::array<double, FFT_FRAMESIZE / 2 + 1> weighted_power_spectrum;
    bool energy_threshold_reached;
  };
  PeaqEarModel parent;
  FFTEarModel();
  [[nodiscard]] auto playback_level() const
  {
    return 10.0 * std::log10(level_factor * 8. / 3. * (GAMMA / 4 * (FFT_FRAMESIZE - 1)) *
                             (GAMMA / 4 * (FFT_FRAMESIZE - 1)));
  }
  void set_playback_level(double level)
  {
    /* level_factor is the square of fac/N in [BS1387], which equals G_Li/N_F in
     * [Kabal03] except for a factor of sqrt(8/3) which is part of the Hann
     * window in [BS1387] but not in [Kabal03]; see [Kabal03] for the derivation
     * of the denominator and the meaning of GAMMA */
    level_factor =
      std::pow(10, level / 10.0) /
      (8. / 3. * (GAMMA / 4 * (FFT_FRAMESIZE - 1)) * (GAMMA / 4 * (FFT_FRAMESIZE - 1)));
  }
  void set_bandcount(std::size_t bandcount);
  [[nodiscard]] auto const& get_masking_difference() const { return masking_difference; }

  void process_block(state_t* state,
                     std::array<float, FFT_FRAMESIZE> const& sample_data) const;
  void group_into_bands(std::array<double, FFT_FRAMESIZE / 2 + 1> const& spectrum,
                        double* band_power) const;
  static auto const& get_power_spectrum(state_t const& state) {
    return state.power_spectrum;
  }
  static auto const& get_weighted_power_spectrum(state_t const& state) {
    return state.weighted_power_spectrum;
  }
  static auto is_energy_threshold_reached(state_t const& state) {
    return state.energy_threshold_reached;
  }

private:
  void do_spreading(double const* Pp, double* E2) const;
  static const std::array<double, FFT_FRAMESIZE> hann_window;
  std::unique_ptr<GstFFTF64, decltype(&gst_fft_f64_free)> gstfft{
    gst_fft_f64_new(FFT_FRAMESIZE, FALSE),
    &gst_fft_f64_free
  };
  std::array<double, FFT_FRAMESIZE / 2 + 1> outer_middle_ear_weight;
  double deltaZ;
  double level_factor;
  std::vector<std::size_t> band_lower_end;
  std::vector<std::size_t> band_upper_end;
  std::vector<double> band_lower_weight;
  std::vector<double> band_upper_weight;
  double lower_spreading;
  double lower_spreading_exponantiated;
  std::vector<double> spreading_normalization;
  std::vector<double> aUC;
  std::vector<double> gIL;
  std::vector<double> masking_difference;
};
}

using PeaqFFTEarModel = peaq::FFTEarModel;

extern "C" {
#else
/**
 * PeaqFFTEarModel:
 *
 * The opaque PeaqFFTEarModel structure.
 */
typedef struct _PeaqFFTEarModel PeaqFFTEarModel;
#endif

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

#define PEAQ_TYPE_FFTEARMODEL (peaq_fftearmodel_get_type ())
#define PEAQ_FFTEARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_FFTEARMODEL, PeaqFFTEarModel))
#define PEAQ_FFTEARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_FFTEARMODEL, PeaqFFTEarModelClass))
#define PEAQ_IS_FFTEARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE (obj, PEAQ_TYPE_FFTEARMODEL))
#define PEAQ_IS_FFTEARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE (klass, PEAQ_TYPE_FFTEARMODEL))
#define PEAQ_FFTEARMODEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_FFTEARMODEL, PeaqFFTEarModelClass))

/**
 * PeaqFFTEarModelClass:
 *
 * The opaque PeaqFFTEarModelClass structure.
 */
typedef struct _PeaqFFTEarModelClass PeaqFFTEarModelClass;

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
void peaq_fftearmodel_group_into_bands (PeaqFFTEarModel const *model,
                                        gdouble const *spectrum,
                                        gdouble *band_power);

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
gdouble const *peaq_fftearmodel_get_masking_difference (PeaqFFTEarModel const *model);
gdouble const *peaq_fftearmodel_get_power_spectrum (gpointer state);

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
gdouble const *peaq_fftearmodel_get_weighted_power_spectrum (gpointer state);

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
gboolean peaq_fftearmodel_is_energy_threshold_reached (gpointer state);
GType peaq_fftearmodel_get_type ();

#ifdef __cplusplus
}
#endif

#endif
