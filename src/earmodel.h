/* GstPEAQ
 * Copyright (C) 2006, 2011, 2012, 2013, 2014, 2015, 2021
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * earmodel.h: Peripheral ear model part.
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

#ifndef __EARMODEL_H__
#define __EARMODEL_H__ 1

/**
 * SECTION:earmodel
 * @short_description: Common base class for FFT and filter bank base ear model.
 *
 * #PeaqEarModel is the common base class of both #PeaqFFTEarModel and
 * #PeaqFilterbankEarModel. It contains code for those computations which are
 * same or similar if both ear models. The derived models specialize by
 * overloading the fields of #PeaqEarModelClass and appropriately setting the
 * #PeaqEarModel:band-centers property.
 */

#include <array>
#include <cmath>

namespace peaq {

template<std::size_t BANDCOUNT>
class EarModelBase
{
public:
  [[nodiscard]] auto get_sampling_rate() const { return SAMPLINGRATE; };
  [[nodiscard]] auto get_internal_noise(std::size_t band) const
  {
    return internal_noise[band];
  }
  template<typename T>
  [[nodiscard]] auto calc_loudness(T const* state) const
  {
    static_assert(std::is_base_of_v<EarModelBase, typename T::earmodel_t>);
    auto overall_loudness = 0.;
    auto const& excitation = state->get_excitation();
    for (std::size_t i = 0; i < BANDCOUNT; i++) {
      auto loudness = loudness_factor[i] *
                      (std::pow(1.0 - threshold[i] +
                                  threshold[i] * excitation[i] / excitation_threshold[i],
                                0.23) -
                       1.0);
      overall_loudness += std::max(loudness, 0.);
    }
    return overall_loudness * (24.0 / BANDCOUNT);
  }
  [[nodiscard]] auto calc_time_constant(std::size_t band,
                                        double tau_min,
                                        double tau_100,
                                        std::size_t step_size) const
  {
    /* (21), (38), (41), and (56) in [BS1387], (32) in [Kabal03] */
    auto tau = tau_min + 100. / fc[band] * (tau_100 - tau_min);
    /* (24), (40), and (44) in [BS1387], (33) in [Kabal03] */
    return std::exp(step_size / (-get_sampling_rate() * tau));
  }
  [[nodiscard]] static auto calc_ear_weight(double frequency)
  {
    auto f_kHz = frequency / 1000.;
    auto W_dB = -0.6 * 3.64 * std::pow(f_kHz, -0.8) +
                6.5 * std::exp(-0.6 * std::pow(f_kHz - 3.3, 2)) -
                1e-3 * std::pow(f_kHz, 3.6);
    return std::pow(10.0, W_dB / 20);
  }

protected:
  void precompute_constants(std::array<double, BANDCOUNT> const& fc,
                            double loudness_scale,
                            double tau_min,
                            double tau_100,
                            std::size_t step_size)
  {
    this->fc = fc;
    for (std::size_t band = 0; band < BANDCOUNT; band++) {
      auto curr_fc = fc[band];
      /* internal noise; (13) in [BS1387] (18) in [Kabal03] */
      internal_noise[band] = pow(10., 0.4 * 0.364 * pow(curr_fc / 1000., -0.8));
      /* excitation threshold; (60) in [BS1387], (70) in [Kabal03] */
      excitation_threshold[band] = pow(10., 0.364 * pow(curr_fc / 1000., -0.8));
      /* threshold index; (61) in [BS1387], (69) in [Kabal03] */
      threshold[band] = pow(10.,
                            0.1 * (-2. - 2.05 * atan(curr_fc / 4000.) -
                                   0.75 * atan(curr_fc / 1600. * curr_fc / 1600.)));
      /* loudness scaling factor; part of (58) in [BS1387], (69) in [Kabal03] */
      loudness_factor[band] =
        loudness_scale * pow(excitation_threshold[band] / (1e4 * threshold[band]), 0.23);

      ear_time_constants[band] = calc_time_constant(band, tau_min, tau_100, step_size);
    }
  }
  [[nodiscard]] auto get_band_center_frequency(std::size_t band) const { return fc[band]; }
  [[nodiscard]] auto get_ear_time_constant(std::size_t band) const
  {
    return ear_time_constants[band];
  }

private:
  static constexpr auto SAMPLINGRATE = 48000;
  std::array<double, BANDCOUNT> ear_time_constants;
  std::array<double, BANDCOUNT> fc;
  std::array<double, BANDCOUNT> internal_noise;
  std::array<double, BANDCOUNT> excitation_threshold;
  std::array<double, BANDCOUNT> threshold;
  std::array<double, BANDCOUNT> loudness_factor;
};

} // namespace peaq

/**
 * PeaqEarModel:
 * @parent: The parent #GObject.
 * @band_count: Number of frequency bands.
 * @fc: Center frequencies of the frequency bands
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>f</mi><mi>c</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" /> and <xref linkend="Kabal03" />).
 * @internal_noise: The ear internal noise per band
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>P</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>E</mi><mtext>IN</mtext></msub><mfenced open="[" close="]"><mi>i</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @ear_time_constants: The time constants for time domain spreading / forward
 * masking
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>a</mi>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>&alpha;</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @excitation_threshold: The excitation threshold per band
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>E</mi><mi>t</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @threshold: The threshold index per band
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" /> and <xref linkend="Kabal03" />).
 * @loudness_factor: The loudness scaling factor per band
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>const</mi><mo>&sdot;</mo><msup><mfenced><mrow><mfrac><mn>1</mn><mrow><mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfrac><mo>&sdot;</mo><mfrac><mrow><msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow><msup><mn>10</mn><mn>4</mn></msup></mfrac></mrow></mfenced><mn>0.23</mn></msup></mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>c</mi><mo>&sdot;</mo><msup><mfenced><mfrac><mrow><msub><mi>E</mi><mi>t</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow><mrow><mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced><msub><mi>E</mi><mn>0</mn></msub></mrow></mfrac></mfenced><mn>0.23</mn></msup></mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 *
 * The fields in #PeaqEarModel get updated when the #PeaqEarModel:band-centers
 * property is set. Read access to them is usually safe as long as the number
 * of bands is not changed after one of the pointers has been obtained. The
 * data should not be written to directly, though.
 */

/**
 * PeaqEarModelClass:
 * @parent: The parent #GObjectClass.
 * @frame_size: The size in samples of one frame to process with
 * peaq_earmodel_process_block()/@process_block.
 * @step_size: The step size in samples to progress between successive
 * invocations of peaq_earmodel_process_block()/@process_block.
 * @loudness_scale: The frequency independent loudness scaling
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>const</mi>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>c</mi>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @tau_min: Parameter <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>&tau;</mi><mi>min</mi></msub>
 * </math></inlineequation>
 * of the time domain spreading filter.
 * @tau_100: Parameter <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>&tau;</mi><mn>100</mn></msub>
 * </math></inlineequation>
 * of the time domain spreading filter.
 * @get_playback_level: Function to call when the
 * #PeaqEarModel:playback-level property is read, has to return the current
 * playback level in dB.
 * @set_playback_level: Function to call when the
 * #PeaqEarModel:playback-level property is set to do any necessary parameter
 * adjustments.
 * @state_alloc: Function to allocate instance state data, called by
 * peaq_earmodel_state_alloc().
 * @state_free: Function to deallocate instance state data, called by
 * peaq_earmodel_state_free().
 * @process_block: Function to process one block of data, called by
 * peaq_earmodel_process_block().
 * @get_excitation: Function to obtain the current excitation from the state,
 * called by peaq_earmodel_get_excitation().
 * @get_unsmeared_excitation: Function to obtain the current unsmeared
 * excitation from the state, called by
 * peaq_earmodel_get_unsmeared_excitation().
 *
 * Derived classes must provide values for all fields of #PeaqEarModelClass
 * (except for <structfield>parent</structfield>).
 */

/**
 * peaq_earmodel_get_excitation:
 * @model: The underlying #PeaqEarModel.
 * @state: The current state from which to extract the excitation.
 *
 * Returns the current excitation patterns after frequency and time-domain
 * spreading
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mover><mi>E</mi><mo>~</mo></mover><mi>s</mi></msub><mfenced open="[" close="]"><mi>i</mi></mfenced></math></inlineequation>
 * in <xref linkend="Kabal03" />)
 * as computed during the last call to peaq_earmodel_process_block().
 *
 * Returns: The current excitation.
 */

/**
 * peaq_earmodel_get_unsmeared_excitation:
 * @model: The underlying #PeaqEarModel.
 * @state: The current state from which to extract the unsmeared excitation.
 *
 * Returns the current unsmeared excitation patterns after frequency, but
 * before time-domain spreading
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>s</mi></msub><mfenced open="[" close="]"><mi>i</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />)
 * as computed during the last call to peaq_earmodel_process_block().
 *
 * Returns: The current excitation.
 */

/**
 * peaq_earmodel_get_internal_noise:
 * @model: The #PeaqEarModel to obtain the internal noise from.
 * @band: The number of the band to obtain the internal noise for.
 *
 * Returns the ear internal noise
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub>
 *       <mi>P</mi>
 *       <mi>Thres</mi>
 *     </msub>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     <mo>=</mo>
 *     <msup>
 *       <mn>10</mn>
 *       <mrow>
 *         <mn>0.4</mn>
 *         <mo>&sdot;</mo>
 *         <mn>0.364</mn>
 *         <mo>&sdot;</mo>
 *         <msup>
 *           <mfenced>
 *             <mfrac>
 *               <mrow><msub><mi>f</mi><mi>c</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 *               <mrow><mn>1</mn><mtext>kHz</mtext></mrow>
 *             </mfrac>
 *           </mfenced>
 *           <mn>-0.8</mn>
 *         </msup>
 *       </mrow>
 *     </msup>
 *   </mrow>
 * </math></inlineequation>
 * (see sections 2.1.6 and 2.2.10 in <xref linkend="BS1387" /> and sections 2.7
 * and 3.6 in <xref linkend="Kabal03" />), where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>k</mi></math>
 * </inlineequation> is the band number given by @band and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>f</mi><mi>c</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation> the center frequency of that band.
 *
 * Note that the actual computation is performed upon setting the
 * #PeaqEarModel:band-centers property, so this function is fast as it only
 * returns a stored value.
 *
 * Returns: The internal noise at @band.
 */

/**
 * peaq_earmodel_get_ear_time_constant:
 * @model: The #PeaqEarModel to obtain the time constant from.
 * @band: The number of the band to obtain the time constant for.
 *
 * Returns the time constant
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>a</mi>
 *   <mo>=</mo>
 *   <msup>
 *     <mi>e</mi>
 *     <mrow>
 *       <mo>-</mo>
 *       <mfrac><mi>step_size</mi><mn>48000</mn></mfrac>
 *       <mo>&sdot;</mo>
 *       <mfrac><mn>1</mn><mi>&tau;</mi></mfrac>
 *     </mrow>
 *   </msup>
 * </math></inlineequation>
 * with
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>&tau;</mi>
 *   <mo>=</mo>
 *   <msub>
 *     <mi>&tau;</mi>
 *     <mi>min</mi>
 *   </msub>
 *   <mo>+</mo>
 *   <mfrac>
 *     <mrow><mn>100</mn><mtext>Hz</mtext></mrow>
 *     <mrow>
 *       <msub><mi>f</mi><mi>c</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow>
 *   </mfrac>
 *   <mo>&sdot;</mo>
 *   <mfenced>
 *     <mrow>
 *       <msub>
 *         <mi>&tau;</mi>
 *         <mn>100</mn>
 *       </msub>
 *       <mo>-</mo>
 *       <msub>
 *         <mi>&tau;</mi>
 *         <mi>min</mi>
 *       </msub>
 *     </mrow>
 *   </mfenced>
 * </math></inlineequation>
 * (see sections 2.1.8 and 2.2.11 in <xref linkend="BS1387" /> and sections 2.9.1
 * and 3.7 in <xref linkend="Kabal03" />), where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>k</mi></math>
 * </inlineequation> is the band number given by @band and
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>f</mi><mi>c</mi></msub>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation> the center frequency of that band and
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub> <mi>&tau;</mi> <mi>min</mi> </msub>
 * </math></inlineequation> and
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub> <mi>&tau;</mi> <mn>100</mn> </msub>
 * </math></inlineequation> are the respective entries of #PeaqEarModelClass.
 *
 * Note that the actual computation is performed upon setting the
 * #PeaqEarModel:band-centers property, so this function is fast as it only
 * returns a stored value.
 *
 * Returns: The time constant for @band.
 */

/**
 * peaq_earmodel_calc_time_constant:
 * @model: The #PeaqEarModel to use.
 * @band: The frequency band <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>k</mi>
 * </math></inlineequation>
 * for which to calculate the time constant.
 * @tau_min: The value of <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>&tau;</mi><mi>min</mi></msub>
 * </math></inlineequation>.
 * @tau_100: The value of <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>&tau;</mi><mn>100</mn></msub>
 * </math></inlineequation>.
 *
 * Calculates the time constant
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>a</mi><mo>=</mo>
 *   <msup>
 *     <mi>e</mi>
 *     <mrow>
 *       <mo>-</mo>
 *       <mfrac>
 *         <mi>StepSize</mi>
 *         <mrow> <mn>48000</mn><mo>&sdot;</mo><mi>&tau;</mi> </mrow>
 *       </mfrac>
 *     </mrow>
 *   </msup>
 * </math></inlineequation>
 * with
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>&tau;</mi>
 *   <mo>=</mo>
 *   <msub><mi>&tau;</mi><mi>min</mi></msub>
 *   <mo>+</mo>
 *   <mfrac>
 *     <mrow><mn>100</mn><mi>Hz</mi></mrow>
 *     <mrow>
 *       <msub><mi>f</mi><mi>c</mi></msub>
 *       <mfenced open="[" close="]">
 *         <mi>k</mi>
 *       </mfenced>
 *     </mrow>
 *   </mfrac>
 *   <mo>&sdot;</mo>
 *   <mfenced><mrow>
 *     <msub><mi>&tau;</mi><mn>100</mn></msub>
 *     <mo>-</mo>
 *     <msub><mi>&tau;</mi><mi>min</mi></msub>
 *   </mrow></mfenced>
 * </math></inlineequation>
 * where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>StepSize</mi>
 * </math></inlineequation>
 * and the center frequency <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>f</mi><mi>c</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * of the <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>k</mi>
 * </math></inlineequation>-th band are taken from the given #PeaqEarModel
 * @model.
 *
 * Returns: The time constant
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>a</mi>
 * </math></inlineequation>.
 */

/**
 * peaq_earmodel_calc_ear_weight:
 * @frequency: The frequency <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>f</mi>
 * </math></inlineequation>
 * to calculate the outer and middle ear weight for.
 *
 * Calculates the outer and middle ear filter weight
 * <informalequation><math display="block" xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>W</mi>
 *   <mo>=</mo>
 *   <msup>
 *     <mn>10</mn>
 *     <mrow>
 *       <mfrac><mn>1</mn><mn>20</mn></mfrac>
 *       <mo>&sdot;</mo>
 *       <mfenced><mrow>
 *         <mn>-0.6</mn>
 *         <mo>&sdot;</mo>
 *         <mn>3.64</mn>
 *         <mo>&sdot;</mo>
 *         <msup>
 *           <mfenced><mfrac>
 *             <mi>f</mi><mrow><mn>1</mn><mtext>kHz</mtext></mrow>
 *           </mfrac></mfenced>
 *           <mn>-0.8</mn>
 *         </msup>
 *         <mo>+</mo>
 *         <mn>6.5</mn>
 *         <mo>&sdot;</mo>
 *         <msup>
 *           <mi>e</mi>
 *           <mrow>
 *             <mn>-0.6</mn>
 *             <mo>&sdot;</mo>
 *             <msup>
 *               <mfenced><mrow>
 *                 <mfrac>
 *                   <mi>f</mi><mrow><mn>1</mn><mtext>kHz</mtext></mrow>
 *                 </mfrac>
 *                 <mo>-</mo>
 *                 <mn>3.3</mn>
 *               </mrow></mfenced>
 *               <mn>2</mn>
 *             </msup>
 *           </mrow>
 *         </msup>
 *         <mo>-</mo>
 *         <mn>0.001</mn>
 *         <mo>&sdot;</mo>
 *         <msup>
 *           <mfenced><mfrac>
 *             <mi>f</mi><mrow><mn>1</mn><mtext>kHz</mtext></mrow>
 *           </mfrac></mfenced>
 *           <mn>3.6</mn>
 *         </msup>
 *       </mrow></mfenced>
 *     </mrow>
 *   </msup>
 * </math></informalequation>
 * for the given frequency (see sections 2.1.4 and 2.2.6 in <xref linkend="BS1387" />
 * and section 2.5 in <xref linkend="Kabal03" />).
 *
 * Returns: The middle and outer ear weight at the given frequency.
 */

/**
 * peaq_earmodel_calc_loudness:
 * @model: The #PeaqEarModel to use for loudness calculation.
 * @state: The state information from which the excitation patterns are
 * obtained to calculate the loudness for.
 *
 * Calculates the overall loudness
 * <informalequation><math display="block" xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>N</mi><mi>total</mi></msub>
 *   <mo>=</mo>
 *   <mfrac><mn>24</mn><mi>band_count</mi></mfrac>
 *   <munderover>
 *     <mo>&sum;</mo>
 *     <mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow>
 *     <mrow><mi>band_count</mi><mo>-</mo><mn>1</mn></mrow>
 *   </munderover>
 *   <mi>max</mi>
 *   <mfenced>
 *     <mn>0</mn>
 *     <mrow>
 *       <mi>l</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       <mo>&sdot;</mo>
 *       <mfenced><mrow>
 *         <msup>
 *           <mfenced><mrow>
 *             <mn>1</mn>
 *             <mo>-</mo>
 *             <mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *             <mo>+</mo>
 *             <mfrac>
 *               <mrow>
 *                 <mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mi>E</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *               </mrow>
 *               <mrow>
 *                 <msub><mi>E</mi><mi>Thres</mi></msub>
 *                 <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *               </mrow>
 *             </mfrac>
 *           </mrow></mfenced>
 *           <mn>0.23</mn>
 *         </msup>
 *         <mo>-</mo>
 *         <mn>1</mn>
 *       </mrow></mfenced>
 *     </mrow>
 *   </mfenced>
 * </math></informalequation>
 * of the current frame
 * (see section 3.3 in <xref linkend="BS1387" /> and section 4.3 in <xref linkend="Kabal03" />),
 * where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>E</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * are the excitation patterns provided by @excitation and the following
 * precomputed constants are used:
 * <variablelist>
 * <varlistentry>
 *   <term>
 *     <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *       <msub><mi>E</mi><mi>Thres</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       <mo>=</mo>
 *       <msup>
 *         <mn>10</mn>
 *         <mrow>
 *           <mn>0.364</mn>
 *           <mo>&sdot;</mo>
 *           <msup>
 *             <mfenced><mfrac>
 *               <mrow>
 *                 <msub><mi>f</mi><mi>c</mi></msub>
 *                 <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *               </mrow>
 *               <mrow><mn>1</mn><mtext>kHz</mtext> </mrow>
 *             </mfrac></mfenced>
 *             <mn>-0.8</mn>
 *           </msup>
 *         </mrow>
 *       </msup>
 *     </math></inlineequation>
 *   </term>
 *   <listitem>
 *     The excitation at threshold as stored in
 *     <structfield>excitation_threshold</structfield> of @model.
 *   </listitem>
 * </varlistentry>
 * <varlistentry>
 *   <term>
 *     <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *       <mi>s</mi> <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       <mo>=</mo>
 *       <msup>
 *         <mn>10</mn>
 *         <mrow>
 *           <mfrac>
 *             <mn>1</mn>
 *             <mn>10</mn>
 *           </mfrac>
 *           <mo>&sdot;</mo>
 *           <mfenced><mrow>
 *             <mo>-</mo>
 *             <mn>2</mn>
 *             <mo>-</mo>
 *             <mn>2.05</mn>
 *             <mo>&sdot;</mo>
 *             <mi>arctan</mi>
 *             <mfenced><mfrac>
 *               <mrow>
 *                 <msub><mi>f</mi><mi>c</mi></msub>
 *                 <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *               </mrow>
 *               <mrow><mn>4</mn><mtext>kHz</mtext> </mrow>
 *             </mfrac></mfenced>
 *             <mo>-</mo>
 *             <mn>0.75</mn>
 *             <mo>&sdot;</mo>
 *             <mi>arctan</mi>
 *             <mfenced><msup>
 *               <mfenced><mfrac>
 *                 <mrow>
 *                   <msub><mi>f</mi><mi>c</mi></msub>
 *                   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 </mrow>
 *                 <mrow><mn>1600</mn><mtext>Hz</mtext> </mrow>
 *               </mfrac></mfenced>
 *               <mn>2</mn>
 *             </msup></mfenced>
 *           </mrow></mfenced>
 *         </mrow>
 *       </msup>
 *     </math></inlineequation>
 *   </term>
 *   <listitem>
 *     The threshold index as stored in
 *     <structfield>threshold</structfield> of @model.
 *   </listitem>
 * </varlistentry>
 * <varlistentry>
 *   <term>
 *     <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *       <mi>l</mi> <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       <mo>=</mo>
 *       <mi>const</mi>
 *       <mo>&sdot;</mo>
 *       <msup>
 *         <mfenced><mrow>
 *           <mfrac>
 *             <mn>1</mn>
 *             <mrow>
 *               <mi>s</mi> <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *             </mrow>
 *           </mfrac>
 *           <mo>&sdot;</mo>
 *           <mfrac>
 *             <mrow>
 *               <msub><mi>E</mi><mi>Thres</mi></msub>
 *               <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *             </mrow>
 *             <msup><mn>10</mn><mn>4</mn></msup>
 *           </mfrac>
 *         </mrow></mfenced>
 *         <mn>0.23</mn>
 *       </msup>
 *     </math></inlineequation>
 *   </term>
 *   <listitem>
 *     The loudness scaling factor as stored in
 *     <structfield>loudness_factor</structfield> of @model. The value of
 *     <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *       <mi>const</mi>
 *     </math></inlineequation>
 *     depends on the ear model used and is taken from the
 *     <structfield>loudness_scale</structfield> field of #PeaqEarModelClass.
 *   </listitem>
 * </varlistentry>
 * </variablelist>
 *
 * Returns: The overall loudness.
 */

#endif
