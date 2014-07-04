/* GstPEAQ
 * Copyright (C) 2013 Martin Holters <martin.holters@hsuhh.de>
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
 */

#include "movs.h"
#include "settings.h"

#include <math.h>

#define FIVE_DB_POWER_FACTOR 3.16227766016838

static gdouble calc_noise_loudness (gdouble alpha, gdouble thres_fac, gdouble S0,
                                    gdouble NLmin,
                                    PeaqModulationProcessor const *ref_mod_proc,
                                    PeaqModulationProcessor const *test_mod_proc,
                                    gdouble const *ref_excitation,
                                    gdouble const *test_excitation);

/**
 * peaq_mov_modulation_difference:
 * @ref_mod_proc: Modulation processors of the reference signal (one per
 * channel).
 * @test_mod_proc: Modulation processors of the test signal (one per channel).
 * @mov_accum1: Accumulator for the AvgModDiff1B or RmsModDiffA MOVs.
 * @mov_accum2: Accumulator for the AvgModDiff2B MOV or NULL.
 * @mov_accum_win: Accumulator for the WinModDiff1B MOV or NULL.
 *
 * Calculates the modulation difference based MOVs as described in section 4.2
 * of <xref linkend="BS1387" />. Given the modulation patterns 
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>Mod</mi><mi>Ref</mi></msub>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation> 
 * and
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>Mod</mi><mi>Test</mi></msub>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation> 
 * of reference and test signal, as obtained from @ref_mod_proc and
 * @test_mod_proc with peaq_modulationprocessor_get_modulation(), the
 * modulation difference is calculated according to
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>ModDiff</mi><mo>=</mo>
 *   <mfrac><mn>100</mn><mi>Z</mi></mfrac>
 *   <mo>&InvisibleTimes;</mo>
 *   <munderover>
 *     <mo>&sum;</mo>
 *     <mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow>
 *     <mrow><mi>Z</mi><mo>-</mo><mn>1</mn></mrow>
 *   </munderover>
 *   <mi>w</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   <mo>&InvisibleTimes;</mo>
 *   <mfrac>
 *     <mfenced open="|" close="|">
 *       <mrow>
 *         <msub><mi>Mod</mi><mi>Test</mi></msub>
 *         <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         <mo>-</mo>
 *         <msub><mi>Mod</mi><mi>Ref</mi></msub>
 *         <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       </mrow>
 *     </mfenced>
 *     <mrow>
 *       <mi>offset</mi>
 *       <mo>+</mo>
 *       <msub><mi>Mod</mi><mi>Ref</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow>
 *   </mfrac>
 *   <mspace width="2em" />
 *   <mtext> where </mtext>
 *   <mspace width="2em" />
 *   <mi>w</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   <mo>=</mo>
 *   <mfenced open="{" close="">
 *     <mtable>
 *       <mtr>
 *         <mtd><mn>1</mn></mtd>
 *         <mtd>
 *           <mtext>if </mtext>
 *           <msub><mi>Mod</mi><mi>Test</mi></msub>
 *           <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *           <mo>&ge;</mo>
 *           <msub><mi>Mod</mi><mi>Ref</mi></msub>
 *           <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mtd>
 *       </mtr>
 *       <mtr><mtd><mi>negWt</mi></mtd><mtd><mtext>else</mtext></mtd></mtr>
 *     </mtable>
 *   </mfenced>
 * </math></informalequation> 
 * and
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>Z</mi>
 * </math></inlineequation> 
 * denotes the number of bands. The parameters
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>offset</mi>
 * </math></inlineequation> 
 * and
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>negWt</mi>
 * </math></inlineequation> 
 * are chosen as:
 * <table>
 *   <tbody>
 *     <tr>
 *       <td>
 *         <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *           <mi>offset</mi><mo>=</mo><mn>1</mn>
 *         </math></inlineequation> 
 *       </td>
 *       <td>
 *         <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *           <mi>negWt</mi><mo>=</mo><mn>1</mn>
 *         </math></inlineequation> 
 *       </td>
 *       <td>for @mov_accum1 and @mov_accum_win</td>
 *     </tr>
 *     <tr>
 *       <td>
 *         <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *           <mi>offset</mi><mo>=</mo><mn>0.01</mn>
 *         </math></inlineequation> 
 *       </td>
 *       <td>
 *         <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *           <mi>negWt</mi><mo>=</mo><mn>0.1</mn>
 *         </math></inlineequation> 
 *       </td>
 *       <td>for @mov_accum2.</td>
 *     </tr>
 *   </tbody>
 * </table>
 * Accumulation of @mov_accum1 and @mov_accum2 (if provided) is weighted with
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>TempWt</mi><mo>=</mo>
 *   <munderover>
 *     <mo>&sum;</mo>
 *     <mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow>
 *     <mrow><mi>Z</mi><mo>-</mo><mn>1</mn></mrow>
 *   </munderover>
 *   <mfrac>
 *     <mrow>
 *       <msub><mover accent="true"><mi>E</mi><mi>-</mi></mover><mi>Ref</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow>
 *     <mrow>
 *       <msub><mover accent="true"><mi>E</mi><mi>-</mi></mover><mi>Ref</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       <mo>+</mo>
 *       <mi>levWt</mi>
 *       <mo>&sdot;</mo>
 *       <msup>
 *         <mrow>
 *           <msub><mi>E</mi><mi>Thres</mi></msub>
 *           <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *         <mn>0.3</mn>
 *       </msup>
 *     </mrow>
 *   </mfrac>
 * </math></informalequation> 
 * where 
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mover accent="true"><mi>E</mi><mi>-</mi></mover><mi>Ref</mi></msub>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation> 
 * is the average loudness obtained form @ref_mod_proc with
 * peaq_modulationprocessor_get_average_loudness(),
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>E</mi><mi>Thres</mi></msub>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation> 
 * is the internal ear noise as returned by peaq_earmodel_get_internal_noise(),
 * and
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>levWt</mi><mo>=</mo><mn>1</mn>
 * </math></inlineequation> 
 * if @mov_accum2 is NULL and
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>levWt</mi><mo>=</mo><mn>100</mn>
 * </math></inlineequation> 
 * otherwise.
 */
void
peaq_mov_modulation_difference (PeaqModulationProcessor* const *ref_mod_proc,
                                PeaqModulationProcessor* const *test_mod_proc,
                                PeaqMovAccum *mov_accum1,
                                PeaqMovAccum *mov_accum2,
                                PeaqMovAccum *mov_accum_win)
{
  guint c;
  PeaqEarModel *ear_model =
    peaq_modulationprocessor_get_ear_model (ref_mod_proc[0]);
  guint band_count = peaq_earmodel_get_band_count (ear_model);

  gdouble levWt = mov_accum2 ? 100. : 1.;
  for (c = 0; c < peaq_movaccum_get_channels (mov_accum1); c++) {
    guint i;
    gdouble const *modulation_ref =
      peaq_modulationprocessor_get_modulation (ref_mod_proc[c]);
    gdouble const *modulation_test =
      peaq_modulationprocessor_get_modulation (test_mod_proc[c]);
    gdouble const *average_loudness_ref =
      peaq_modulationprocessor_get_average_loudness (ref_mod_proc[c]);

    gdouble mod_diff_1b = 0.;
    gdouble mod_diff_2b = 0.;
    gdouble temp_wt = 0.;
    for (i = 0; i < band_count; i++) {
      gdouble w;
      gdouble diff = ABS (modulation_ref[i] - modulation_test[i]);
      /* (63) in [BS1387] with negWt = 1, offset = 1 */
      mod_diff_1b += diff / (1. + modulation_ref[i]);
      /* (63) in [BS1387] with negWt = 0.1, offset = 0.01 */
      w = modulation_test[i] >= modulation_ref[i] ? 1. : .1;
      mod_diff_2b += w * diff / (0.01 + modulation_ref[i]);
      /* (65) in [BS1387] with levWt = 100 if more than one accumulator is
         given, 1 otherwise */
      temp_wt += average_loudness_ref[i] /
        (average_loudness_ref[i] +
         levWt * pow (peaq_earmodel_get_internal_noise (ear_model, i), 0.3));
    }
    mod_diff_1b *= 100. / band_count;
    mod_diff_2b *= 100. / band_count;
    peaq_movaccum_accumulate (mov_accum1, c, mod_diff_1b, temp_wt);
    if (mov_accum2)
      peaq_movaccum_accumulate (mov_accum2, c, mod_diff_2b, temp_wt);
    if (mov_accum_win)
      peaq_movaccum_accumulate (mov_accum_win, c, mod_diff_1b, 1.);
  }
}

void
peaq_mov_noise_loudness (PeaqModulationProcessor * const *ref_mod_proc,
                         PeaqModulationProcessor * const *test_mod_proc,
                         PeaqLevelAdapter * const *level,
                         PeaqMovAccum *mov_accum)
{
  guint c;

  for (c = 0; c < peaq_movaccum_get_channels (mov_accum); c++) {
    gdouble const *ref_excitation =
      peaq_leveladapter_get_adapted_ref (level[c]);
    gdouble const *test_excitation =
      peaq_leveladapter_get_adapted_test (level[c]);
    gdouble noise_loudness =
      calc_noise_loudness (1.5, 0.15, 0.5, 0., ref_mod_proc[c],
                           test_mod_proc[c], ref_excitation, test_excitation);
    peaq_movaccum_accumulate (mov_accum, c, noise_loudness, 1.);
  }
}

void
peaq_mov_noise_loud_asym (PeaqModulationProcessor * const *ref_mod_proc,
                          PeaqModulationProcessor * const *test_mod_proc,
                          PeaqLevelAdapter * const *level,
                          PeaqMovAccum *mov_accum)
{
  guint c;

  for (c = 0; c < peaq_movaccum_get_channels (mov_accum); c++) {
    gdouble const *ref_excitation =
      peaq_leveladapter_get_adapted_ref (level[c]);
    gdouble const *test_excitation =
      peaq_leveladapter_get_adapted_test (level[c]);
    gdouble noise_loudness =
      calc_noise_loudness (2.5, 0.3, 1., 0.1, ref_mod_proc[c],
                           test_mod_proc[c], ref_excitation, test_excitation);
#if defined(SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS) && SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS
    gdouble missing_components =
      calc_noise_loudness (1.5, 0.15, 1., 0., test_mod_proc[c],
                           ref_mod_proc[c], test_excitation, ref_excitation);
#else
    gdouble missing_components =
      calc_noise_loudness (1.5, 0.15, 1., 0., ref_mod_proc[c],
                           test_mod_proc[c], test_excitation, ref_excitation);
#endif
    peaq_movaccum_accumulate (mov_accum, c, noise_loudness, missing_components);
  }
}

void
peaq_mov_lin_dist (PeaqModulationProcessor * const *ref_mod_proc,
                   PeaqModulationProcessor * const *test_mod_proc,
                   PeaqLevelAdapter * const *level, const gpointer *state,
                   PeaqMovAccum *mov_accum)
{
  guint c;
  PeaqEarModel *ear_model =
    peaq_modulationprocessor_get_ear_model (ref_mod_proc[0]);

  for (c = 0; c < peaq_movaccum_get_channels (mov_accum); c++) {
    gdouble const *ref_adapted_excitation =
      peaq_leveladapter_get_adapted_ref (level[c]);
    gdouble const *ref_excitation =
      peaq_earmodel_get_excitation (ear_model, state[c]);
#if defined(SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS) && SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS
    gdouble noise_loudness =
      calc_noise_loudness (1.5, 0.15, 1., 0., ref_mod_proc[c],
                           ref_mod_proc[c], ref_adapted_excitation,
                           ref_excitation);
#else
    gdouble noise_loudness =
      calc_noise_loudness (1.5, 0.15, 1., 0., ref_mod_proc[c],
                           test_mod_proc[c], ref_adapted_excitation,
                           ref_excitation);
#endif
    peaq_movaccum_accumulate (mov_accum, c, noise_loudness, 1.);
  }
}

static gdouble
calc_noise_loudness (gdouble alpha, gdouble thres_fac, gdouble S0,
                     gdouble NLmin,
                     PeaqModulationProcessor const *ref_mod_proc,
                     PeaqModulationProcessor const *test_mod_proc,
                     gdouble const *ref_excitation,
                     gdouble const *test_excitation)
{
  guint i;
  gdouble noise_loudness = 0.;
  PeaqEarModel *ear_model =
    peaq_modulationprocessor_get_ear_model (ref_mod_proc);
  guint band_count = peaq_earmodel_get_band_count (ear_model);
  gdouble const *ref_modulation =
    peaq_modulationprocessor_get_modulation (ref_mod_proc);
  gdouble const *test_modulation =
    peaq_modulationprocessor_get_modulation (test_mod_proc);
  for (i = 0; i < band_count; i++) {
    /* (67) in [BS1387] */
    gdouble sref = thres_fac * ref_modulation[i] + S0;
    gdouble stest = thres_fac * test_modulation[i] + S0;
    gdouble ethres = peaq_earmodel_get_internal_noise (ear_model, i);
    gdouble ep_ref = ref_excitation[i];
    gdouble ep_test = test_excitation[i];
    /* (68) in [BS1387] */
    gdouble beta = exp (-alpha * (ep_test - ep_ref) / ep_ref);
    /* (66) in [BS1387] */
    noise_loudness += pow (1. / stest * ethres, 0.23) *
      (pow (1. + MAX (stest * ep_test - sref * ep_ref, 0.) /
	    (ethres + sref * ep_ref * beta), 0.23) - 1.);
  }
  noise_loudness *= 24. / band_count;
  if (noise_loudness < NLmin)
    noise_loudness = 0.;
  return noise_loudness;
}

/**
 * peaq_mov_bandwidth:
 * @ref_state: State of the reference signal #PeaqFFTEarModel.
 * @test_state: State of the test signal #PeaqFFTEarModel.
 * @mov_accum_ref: Accumulator for the BandwidthRefB MOV.
 * @mov_accum_test: Accumulator for the BandwidthTestB MOV.
 *
 * Calculates the bandwidth based MOVs as described in section 4.4
 * of <xref linkend="BS1387" />. The power spectra
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <msub><mi>F</mi><mi>Ref</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * and
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <msub><mi>F</mi><mi>Test</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * are obtained from @ref_state and @test_state, respectively, using
 * peaq_fftearmodel_get_power_spectrum().The first step is to determine the
 * zero threshold
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <munder>
 *     <mi>max</mi>
 *     <mrow><mn>921</mn><mo>&le;</mo><mi>k</mi><mo>&le;</mo><mn>1023</mn></mrow>
 *   </munder>
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <msub><mi>F</mi><mi>Ref</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>.
 * The reference signal bandwidth is then determined as the largest 
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>k</mi>
 * </math></inlineequation>
 * such that
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <msub><mi>F</mi><mi>Ref</mi></msub>
 *       <mfenced open="[" close="]"><mrow><mi>k</mi><mo>-</mo><mn>1</mn></mrow></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * is 10 dB above the zero threshold. Likewise, the test signal bandwidth is
 * then determined as the largest 
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>k</mi>
 * </math></inlineequation>
 * smaller than the reference signal bandwidth such that
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <msub><mi>F</mi><mi>Test</mi></msub>
 *       <mfenced open="[" close="]"><mrow><mi>k</mi><mo>-</mo><mn>1</mn></mrow></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * is 5 dB above the zero threshold. If no frequency bin is above the zero
 * threshold, the respective bandwidth is set to zero. The resulting bandwidths
 * are accumulated to @mov_accum_ref and @mov_accum_test only if the reference
 * bandwidth is greater than 346.
 */
void
peaq_mov_bandwidth (const gpointer *ref_state, const gpointer *test_state,
                    PeaqMovAccum *mov_accum_ref, PeaqMovAccum *mov_accum_test)
{
  guint c;

  for (c = 0; c < peaq_movaccum_get_channels (mov_accum_ref); c++) {
    guint i;
    gdouble const *ref_power_spectrum =
      peaq_fftearmodel_get_power_spectrum (ref_state[c]);
    gdouble const *test_power_spectrum =
      peaq_fftearmodel_get_power_spectrum (test_state[c]);
    gdouble zero_threshold = test_power_spectrum[921];
    for (i = 922; i < 1024; i++)
      if (test_power_spectrum[i] >= zero_threshold)
        zero_threshold = test_power_spectrum[i];
    guint bw_ref = 0;
    for (i = 921; i > 0; i--)
      if (ref_power_spectrum[i - 1] > 10. * zero_threshold) {
        bw_ref = i;
        break;
      }
    if (bw_ref > 346) {
      guint bw_test = 0;
      for (i = bw_ref; i > 0; i--)
        if (test_power_spectrum[i - 1] >=
            FIVE_DB_POWER_FACTOR * zero_threshold) {
          bw_test = i;
          break;
        }
      peaq_movaccum_accumulate (mov_accum_ref, c, bw_ref, 1.);
      peaq_movaccum_accumulate (mov_accum_test, c, bw_test, 1.);
    }
  }
}

void
peaq_mov_nmr (PeaqFFTEarModel const *ear_model, const gpointer *ref_state,
              const gpointer *test_state, PeaqMovAccum *mov_accum_nmr,
              PeaqMovAccum *mov_accum_rel_dist_frames)
{
  guint c;
  guint band_count = peaq_earmodel_get_band_count (PEAQ_EARMODEL (ear_model));
  guint frame_size = peaq_earmodel_get_frame_size (PEAQ_EARMODEL (ear_model));
  gdouble const *masking_difference = 
    peaq_fftearmodel_get_masking_difference (ear_model);
  for (c = 0; c < peaq_movaccum_get_channels (mov_accum_nmr); c++) {
    guint i;
    gdouble const *ref_excitation =
      peaq_earmodel_get_excitation (PEAQ_EARMODEL (ear_model), ref_state[c]);
    gdouble nmr = 0.;
    gdouble nmr_max = 0.;
    gdouble *noise_in_bands = g_newa (gdouble, band_count);
    gdouble const *ref_weighted_power_spectrum =
      peaq_fftearmodel_get_weighted_power_spectrum (ref_state[c]);
    gdouble const *test_weighted_power_spectrum =
      peaq_fftearmodel_get_weighted_power_spectrum (test_state[c]);
    gdouble noise_spectrum[1025];

    for (i = 0; i < frame_size / 2 + 1; i++)
      noise_spectrum[i] =
        ref_weighted_power_spectrum[i] -
        2 * sqrt (ref_weighted_power_spectrum[i] *
                  test_weighted_power_spectrum[i]) +
        test_weighted_power_spectrum[i];

    peaq_fftearmodel_group_into_bands (ear_model, noise_spectrum,
                                       noise_in_bands);

    for (i = 0; i < band_count; i++) {
      /* (26) in [BS1387] */
      gdouble mask = ref_excitation[i] / masking_difference[i];
      /* (70) in [BS1387], except for conversion to dB in the end */
      gdouble curr_nmr = noise_in_bands[i] / mask;
      nmr += curr_nmr;
      /* for Relative Disturbed Frames */
      if (curr_nmr > nmr_max)
        nmr_max = curr_nmr;
    }
    nmr /= band_count;

    if (peaq_movaccum_get_mode (mov_accum_nmr) == MODE_AVG_LOG)
      peaq_movaccum_accumulate (mov_accum_nmr, c, nmr, 1.);
    else
      peaq_movaccum_accumulate (mov_accum_nmr, c, 10. * log10 (nmr), 1.);
    if (mov_accum_rel_dist_frames)
      peaq_movaccum_accumulate (mov_accum_rel_dist_frames, c,
                                nmr_max > 1.41253754462275 ? 1. : 0., 1.);
  }
}

void
peaq_mov_prob_detect (PeaqEarModel const *ear_model, const gpointer *ref_state,
                      const gpointer *test_state, PeaqMovAccum *mov_accum_adb,
                      PeaqMovAccum *mov_accum_mfpd)
{
  guint c;
  guint i;
  guint band_count = peaq_earmodel_get_band_count (ear_model);
  gdouble binaural_detection_probability = 1.;
  gdouble binaural_detection_steps = 0.;
  for (i = 0; i < band_count; i++) {
    gdouble detection_probability = 0.;
    gdouble detection_steps;
    for (c = 0; c < peaq_movaccum_get_channels (mov_accum_adb); c++) {
      gdouble const *ref_excitation =
        peaq_earmodel_get_excitation (ear_model, ref_state[c]);
      gdouble const *test_excitation =
        peaq_earmodel_get_excitation (ear_model, test_state[c]);
      gdouble eref_db = 10. * log10 (ref_excitation[i]);
      gdouble etest_db = 10. * log10 (test_excitation[i]);
      /* (73) in [BS1387] */
      gdouble l = 0.3 * MAX (eref_db, etest_db) + 0.7 * etest_db;
      /* (74) in [BS1387] */
      gdouble s = l > 0. ? 5.95072 * pow (6.39468 / l, 1.71332) +
        9.01033e-11 * pow (l, 4.) + 5.05622e-6 * pow (l, 3.) -
        0.00102438 * l * l + 0.0550197 * l - 0.198719 : 1e30;
      /* (75) in [BS1387] */
      gdouble e = eref_db - etest_db;
      gdouble b = eref_db > etest_db ? 4. : 6.;
      /* (76) and (77) in [BS1387] simplify to this */
      gdouble pc = 1. - pow (0.5, pow (e / s, b));
      /* (78) in [BS1387] */
      gdouble qc = fabs (trunc(e)) / s;
      if (pc > detection_probability)
        detection_probability = pc;
      if (c == 0 || qc > detection_steps)
        detection_steps = qc;
    }
    binaural_detection_probability *= 1. - detection_probability;
    binaural_detection_steps += detection_steps;
  }
  binaural_detection_probability = 1. - binaural_detection_probability;
  if (binaural_detection_probability > 0.5) {
    peaq_movaccum_accumulate (mov_accum_adb, 0,
                              binaural_detection_steps, 1.);
  }
  peaq_movaccum_accumulate (mov_accum_mfpd, 0,
                            binaural_detection_probability, 1.);
}

