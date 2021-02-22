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

#ifndef __MOVS_H_
#define __MOVS_H_ 1

#include "fbearmodel.h"
#include "fftearmodel.h"
#include "leveladapter.h"
#include "modpatt.h"
#include "movaccum.h"
#include "settings.h"

#include <numeric>

namespace peaq {
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
 * of <xref linkend="BS1387" />. Given the modulation patterns <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>Mod</mi><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>Mod</mi><mi>Test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * of reference and test signal, as obtained from @ref_mod_proc and
 * @test_mod_proc with peaq_modulationprocessor_get_modulation(), the
 * modulation difference is calculated according to
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>ModDiff</mi><mo>=</mo>
 *   <mfrac><mn>100</mn><mover accent="true"><mi>Z</mi><mi>^</mi></mover></mfrac>
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
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>Z</mi>
 * </math></inlineequation>
 * denotes the number of bands. The parameters <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>offset</mi>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>negWt</mi>
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
 * If the accumulation mode of @mov_accum1 is #MODE_RMS, then <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mover accent="true"><mi>Z</mi><mi>^</mi></mover><mo>=</mo><msqrt><mi>Z</mi></msqrt>
 * </math></inlineequation>
 * to handle the special weighting with <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msqrt><mi>Z</mi></msqrt>
 * </math></inlineequation>
 * introduced in (92) of <xref linkend="BS1387" />, otherwise <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mover accent="true"><mi>Z</mi><mi>^</mi></mover><mo>=</mo><mi>Z</mi>
 * </math></inlineequation>.
 *
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
 * where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mover accent="true"><mi>E</mi><mi>-</mi></mover><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * is the average loudness obtained form @ref_mod_proc with
 * peaq_modulationprocessor_get_average_loudness(), <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * is the internal ear noise as returned by PeaqEarmodel::get_internal_noise(),
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>levWt</mi><mo>=</mo><mn>1</mn>
 * </math></inlineequation>
 * if @mov_accum2 is NULL and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>levWt</mi><mo>=</mo><mn>100</mn>
 * </math></inlineequation>
 * otherwise.
 */
void mov_modulation_difference(FFTEarModel<> const& ear_model,
                               std::vector<ModulationProcessor<109>> const& ref_mod_proc,
                               std::vector<ModulationProcessor<109>> const& test_mod_proc,
                               MovAccum& mov_accum1,
                               MovAccum& mov_accum2,
                               MovAccum& mov_accum_win);
void mov_modulation_difference(FilterbankEarModel const& ear_model,
                               std::vector<ModulationProcessor<40>> const& ref_mod_proc,
                               std::vector<ModulationProcessor<40>> const& test_mod_proc,
                               MovAccum& mov_accum1);
/**
 * peaq_mov_noise_loudness:
 * @ref_mod_proc: Modulation processors of the reference signal (one per
 * channel).
 * @test_mod_proc: Modulation processors of the test signal (one per channel).
 * @level: Level adapters (one per channel).
 * @mov_accum: Accumulator for the RmsNoiseLoudB MOV.
 *
 * Calculates the RmsNoiseLoudB model output variable as
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>NL</mi><mo>=</mo>
 *   <mfrac><mn>24</mn><mi>Z</mi></mfrac>
 *   <mo>&InvisibleTimes;</mo>
 *   <munderover>
 *     <mo>&sum;</mo>
 *     <mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow>
 *     <mrow><mi>Z</mi><mo>-</mo><mn>1</mn></mrow>
 *   </munderover>
 *   <msup>
 *     <mfenced>
 *       <mfrac>
 *         <mrow>
 *           <msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *         <mrow>
 *           <msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *       </mfrac>
 *     </mfenced>
 *     <mn>0.23</mn>
 *   </msup>
 *   <mfenced>
 *     <mrow>
 *       <msup>
 *         <mfenced>
 *           <mrow>
 *             <mn>1</mn>
 *             <mo>+</mo>
 *             <mfrac>
 *               <mrow>
 *                 <mi>max</mi>
 *                 <mfenced>
 *                   <mrow>
 *                     <msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>&sdot;</mo>
 *                     <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub>
 *                     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>-</mo>
 *                     <msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>&sdot;</mo>
 *                     <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *                     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                   </mrow>
 *                   <mn>0</mn>
 *                 </mfenced>
 *               </mrow>
 *               <mrow>
 *                 <msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>+</mo>
 *                 <msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>&sdot;</mo>
 *                 <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *                 <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>&sdot;</mo>
 *                 <mi>&beta;</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *               </mrow>
 *             </mfrac>
 *           </mrow>
 *         </mfenced>
 *         <mn>0.23</mn>
 *       </msup>
 *       <mo>-</mo>
 *       <mn>1</mn>
 *     </mrow>
 *   </mfenced>
 * </math></informalequation>
 * where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * is the internal ear noise as returned by PeaqEarmodel::get_internal_noise(), <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * are the spectrally adapted patterns of the reference and test signal as
 * returned by peaq_leveladapter_get_adapted_ref() and
 * peaq_leveladapter_get_adapted_test(), respectively, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mn>0.15</mn><mo>&sdot;</mo><msub><mi>Mod</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>+</mo><mn>0.5</mn>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mn>0.15</mn><mo>&sdot;</mo><msub><mi>Mod</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>+</mo><mn>0.5</mn>
 * </math></inlineequation>
 * are computed from the modulation <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>Mod</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>Mod</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * of the test and reference signal, respectively, as obtained with
 * peaq_modulationprocessor_get_modulation() and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>&beta;</mi><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mi>exp</mi><mfenced><mrow><mn>-1.5</mn><mo>&sdot;</mo><mfenced><mrow><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>-</mo><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mo>/</mo><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced>
 * </math></inlineequation>.
 * If the resulting noise loudness is negative, it is set to zero.
 */
void mov_noise_loudness(FFTEarModel<> const& ear_model,
                        std::vector<ModulationProcessor<109>> const& ref_mod_proc,
                        std::vector<ModulationProcessor<109>> const& test_mod_proc,
                        std::vector<LevelAdapter<109>> const& level,
                        MovAccum& mov_accum);

/**
 * peaq_mov_noise_loud_asym:
 * @ref_mod_proc: Modulation processors of the reference signal (one per
 * channel).
 * @test_mod_proc: Modulation processors of the test signal (one per channel).
 * @level: Level adapters (one per channel).
 * @mov_accum: Accumulator for the RmsNoiseLoudAsymA MOV.
 *
 * Calculates the RmsNoiseLoudAsymA model output variable as <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>NL</mi><mi>Asym</mi></msub><mo>=</mo><mi>NL</mi><mo>+</mo><mn>0.5</mn><mo>&sdot;</mo><mi>MC</mi>
 * </math></inlineequation>
 * where
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>NL</mi><mo>=</mo>
 *   <mfrac><mn>24</mn><mi>Z</mi></mfrac>
 *   <mo>&InvisibleTimes;</mo>
 *   <munderover>
 *     <mo>&sum;</mo>
 *     <mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow>
 *     <mrow><mi>Z</mi><mo>-</mo><mn>1</mn></mrow>
 *   </munderover>
 *   <msup>
 *     <mfenced>
 *       <mfrac>
 *         <mrow>
 *           <msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *         <mrow>
 *           <msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *       </mfrac>
 *     </mfenced>
 *     <mn>0.23</mn>
 *   </msup>
 *   <mfenced>
 *     <mrow>
 *       <msup>
 *         <mfenced>
 *           <mrow>
 *             <mn>1</mn>
 *             <mo>+</mo>
 *             <mfrac>
 *               <mrow>
 *                 <mi>max</mi>
 *                 <mfenced>
 *                   <mrow>
 *                     <msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>&sdot;</mo>
 *                     <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub>
 *                     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>-</mo>
 *                     <msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>&sdot;</mo>
 *                     <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *                     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                   </mrow>
 *                   <mn>0</mn>
 *                 </mfenced>
 *               </mrow>
 *               <mrow>
 *                 <msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>+</mo>
 *                 <msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>&sdot;</mo>
 *                 <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *                 <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>&sdot;</mo>
 *                 <mi>&beta;</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *               </mrow>
 *             </mfrac>
 *           </mrow>
 *         </mfenced>
 *         <mn>0.23</mn>
 *       </msup>
 *       <mo>-</mo>
 *       <mn>1</mn>
 *     </mrow>
 *   </mfenced>
 * </math></informalequation>
 * with <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mn>0.3</mn><mo>&sdot;</mo><msub><mi>Mod</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>+</mo><mn>1</mn>
 * </math></inlineequation>, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mn>0.3</mn><mo>&sdot;</mo><msub><mi>Mod</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>+</mo><mn>1</mn>
 * </math></inlineequation>,
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>&beta;</mi><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mi>exp</mi><mfenced><mrow><mn>-2.5</mn><mo>&sdot;</mo><mfenced><mrow><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>-</mo><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mo>/</mo><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced>
 * </math></inlineequation> and
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>MC</mi><mo>=</mo>
 *   <mfrac><mn>24</mn><mi>Z</mi></mfrac>
 *   <mo>&InvisibleTimes;</mo>
 *   <munderover>
 *     <mo>&sum;</mo>
 *     <mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow>
 *     <mrow><mi>Z</mi><mo>-</mo><mn>1</mn></mrow>
 *   </munderover>
 *   <msup>
 *     <mfenced>
 *       <mfrac>
 *         <mrow>
 *           <msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *         <mrow>
 *           <msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *       </mfrac>
 *     </mfenced>
 *     <mn>0.23</mn>
 *   </msup>
 *   <mfenced>
 *     <mrow>
 *       <msup>
 *         <mfenced>
 *           <mrow>
 *             <mn>1</mn>
 *             <mo>+</mo>
 *             <mfrac>
 *               <mrow>
 *                 <mi>max</mi>
 *                 <mfenced>
 *                   <mrow>
 *                     <msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>&sdot;</mo>
 *                     <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *                     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>-</mo>
 *                     <msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>&sdot;</mo>
 *                     <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub>
 *                     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                   </mrow>
 *                   <mn>0</mn>
 *                 </mfenced>
 *               </mrow>
 *               <mrow>
 *                 <msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>+</mo>
 *                 <msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>&sdot;</mo>
 *                 <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub>
 *                 <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>&sdot;</mo>
 *                 <mi>&beta;</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *               </mrow>
 *             </mfrac>
 *           </mrow>
 *         </mfenced>
 *         <mn>0.23</mn>
 *       </msup>
 *       <mo>-</mo>
 *       <mn>1</mn>
 *     </mrow>
 *   </mfenced>
 * </math></informalequation>
 * with <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mn>0.15</mn><mo>&sdot;</mo><msub><mi>Mod</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>+</mo><mn>1</mn>
 * </math></inlineequation>, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mn>0.15</mn><mo>&sdot;</mo><msub><mi>Mod</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>+</mo><mn>1</mn>
 * </math></inlineequation>,
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>&beta;</mi><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mi>exp</mi><mfenced><mrow><mn>-1.5</mn><mo>&sdot;</mo><mfenced><mrow><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>-</mo><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mo>/</mo><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced>
 * </math></inlineequation>
 * where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * is the internal ear noise as returned by PeaqEarmodel::get_internal_noise(), <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * are the spectrally adapted patterns of the reference and test signal as
 * returned by peaq_leveladapter_get_adapted_ref() and
 * peaq_leveladapter_get_adapted_test(), respectively, and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>Mod</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>Mod</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * are the modulation of the test and reference signal, respectively, as
* obtained with peaq_modulationprocessor_get_modulation()
 * If MC is negative, it is set to zero. Likewise, if NL is less than 0.1, it
 * is set to zero.
 *
 * Note: If #SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS is not set, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>Mod</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>Mod</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation> have to be exchanged in the calculation of MC.
 */
void mov_noise_loud_asym(FilterbankEarModel const& ear_model,
                         std::vector<ModulationProcessor<40>> const& ref_mod_proc,
                         std::vector<ModulationProcessor<40>> const& test_mod_proc,
                         std::vector<LevelAdapter<40>> const& level,
                         MovAccum& mov_accum);

/**
 * peaq_mov_lin_dist:
 * @ref_mod_proc: Modulation processors of the reference signal (one per
 * channel).
 * @test_mod_proc: Modulation processors of the test signal (one per channel).
 * @level: Level adapters (one per channel).
 * @state: States of the reference signal ear model (one per channels).
 * @mov_accum: Accumulator for the AvgLinDistA MOV.
 *
 * Calculates the AvgLinDistA model output variable as
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>LD</mi><mo>=</mo>
 *   <mfrac><mn>24</mn><mi>Z</mi></mfrac>
 *   <mo>&InvisibleTimes;</mo>
 *   <munderover>
 *     <mo>&sum;</mo>
 *     <mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow>
 *     <mrow><mi>Z</mi><mo>-</mo><mn>1</mn></mrow>
 *   </munderover>
 *   <msup>
 *     <mfenced>
 *       <mfrac>
 *         <mrow>
 *           <msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *         <mrow>
 *           <msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *       </mfrac>
 *     </mfenced>
 *     <mn>0.23</mn>
 *   </msup>
 *   <mfenced>
 *     <mrow>
 *       <msup>
 *         <mfenced>
 *           <mrow>
 *             <mn>1</mn>
 *             <mo>+</mo>
 *             <mfrac>
 *               <mrow>
 *                 <mi>max</mi>
 *                 <mfenced>
 *                   <mrow>
 *                     <msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>&sdot;</mo>
 *                     <msub><mi>E</mi><mi>Ref</mi></msub>
 *                     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>-</mo>
 *                     <msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                     <mo>&sdot;</mo>
 *                     <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *                     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                   </mrow>
 *                   <mn>0</mn>
 *                 </mfenced>
 *               </mrow>
 *               <mrow>
 *                 <msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>+</mo>
 *                 <msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>&sdot;</mo>
 *                 <msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *                 <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>&sdot;</mo>
 *                 <mi>&beta;</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *               </mrow>
 *             </mfrac>
 *           </mrow>
 *         </mfenced>
 *         <mn>0.23</mn>
 *       </msup>
 *       <mo>-</mo>
 *       <mn>1</mn>
 *     </mrow>
 *   </mfenced>
 * </math></informalequation>
 * where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * is the internal ear noise as returned by PeaqEarmodel::get_internal_noise(), <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * are the spectrally adapted patterns of the reference signal as returned by
 * peaq_leveladapter_get_adapted_ref(), <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * are the excitation patterns of the reference signal as returned by
 * peaq_earmodel_get_excitation(), <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><msub><mi>s</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mn>0.15</mn><mo>&sdot;</mo><msub><mi>Mod</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>+</mo><mn>1</mn>
 * </math></inlineequation>
 * are computed from the modulation <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>Mod</mi><mi>ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * of the reference signal as obtained with
 * peaq_modulationprocessor_get_modulation() and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>&beta;</mi><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mi>exp</mi><mfenced><mrow><mn>-1.5</mn><mo>&sdot;</mo><mfenced><mrow><msub><mi>E</mi><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>-</mo><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mo>/</mo><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced>
 * </math></inlineequation>.
 * If the resulting linear distortion measure is negative, it is set to zero.
 *
 * Note: If #SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS is not set, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>Mod</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * is used to calculate <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>s</mi><mi>test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>.
 */
void mov_lin_dist(FilterbankEarModel const& ear_model,
                  std::vector<ModulationProcessor<40>> const& ref_mod_proc,
                  std::vector<LevelAdapter<40>> const& level,
                  std::vector<FilterbankEarModel::state_t> const& state,
                  MovAccum& mov_accum);

/**
 * peaq_mov_bandwidth:
 * @ref_state: State of the reference signal #PeaqFFTEarModel.
 * @test_state: State of the test signal #PeaqFFTEarModel.
 * @mov_accum_ref: Accumulator for the BandwidthRefB MOV.
 * @mov_accum_test: Accumulator for the BandwidthTestB MOV.
 *
 * Calculates the bandwidth based MOVs as described in section 4.4
 * of <xref linkend="BS1387" />. The power spectra <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="|" close="|"><mrow><msub><mi>F</mi><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="|" close="|"><mrow><msub><mi>F</mi><mi>Test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * are obtained from @ref_state and @test_state, respectively, using
 * peaq_fftearmodel_get_power_spectrum().The first step is to determine the
 * zero threshold <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><munder><mi>max</mi><mrow><mn>921</mn><mo>&le;</mo><mi>k</mi><mo>&le;</mo><mn>1023</mn></mrow></munder><msup><mfenced open="|" close="|"><mrow><msub><mi>F</mi><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>.
 * The reference signal bandwidth is then determined as the largest <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>k</mi>
 * </math></inlineequation>
 * such that <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="|" close="|"><mrow><msub><mi>F</mi><mi>Ref</mi></msub><mfenced open="[" close="]"><mrow><mi>k</mi><mo>-</mo><mn>1</mn></mrow></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * is 10 dB above the zero threshold. Likewise, the test signal bandwidth is
 * then determined as the largest <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>k</mi>
 * </math></inlineequation>
 * smaller than the reference signal bandwidth such that <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="|" close="|"><mrow><msub><mi>F</mi><mi>Test</mi></msub><mfenced open="[" close="]"><mrow><mi>k</mi><mo>-</mo><mn>1</mn></mrow></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * is 5 dB above the zero threshold. If no frequency bin is above the zero
 * threshold, the respective bandwidth is set to zero. The resulting bandwidths
 * are accumulated to @mov_accum_ref and @mov_accum_test only if the reference
 * bandwidth is greater than 346.
 */
void mov_bandwidth(std::vector<FFTEarModel<>::state_t> const& ref_state,
                   std::vector<FFTEarModel<>::state_t> const& test_state,
                   MovAccum& mov_accum_ref,
                   MovAccum& mov_accum_test);

/**
 * peaq_mov_nmr:
 * @ear_model: The underlying FFT based ear model to which @ref_state and
 * @test_state belong.
 * @ref_state: Ear model states for the reference signal.
 * @test_state: Ear model states for the test signal.
 * @mov_accum_nmr: Accumulator for the Total NMRB or Segmental NMRB MOVs.
 * @mov_accum_rel_dist_frames: Accumulator for the Relative Disturbed FramesB
 * MOV or NULL.
 *
 * Calculates the noise-to-mask ratio based model output variables as described
 * in sections 4.5 and 4.6 of <xref linkend="BS1387" />. From the weighted power
 * spectra <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="|" close="|"><mrow><msub><mi>F</mi><mrow><mi>e</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="|" close="|"><mrow><msub><mi>F</mi><mrow><mi>e</mi><mo>,</mo><mi>Test</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * obtained with
 * peaq_fftearmodel_get_weighted_power_spectrum() from @ref_state and
 * @test_state, respectively, the noise power spectrum
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|">
 *       <mrow>
 *         <msub><mi>F</mi><mi>noise</mi></msub>
 *         <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       </mrow>
 *     </mfenced>
 *     <mn>2</mn>
 *   </msup>
 *   <mo>=</mo>
 *   <msup>
 *     <mfenced open="|" close="|">
 *       <mrow>
 *         <mfenced open="|" close="|">
 *           <msub><mi>F</mi><mrow><mi>e</mi><mo>,</mo><mi>Test</mi></mrow></msub>
 *           <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mfenced>
 *         <mo>-</mo>
 *         <mfenced open="|" close="|">
 *           <msub><mi>F</mi><mrow><mi>e</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *           <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mfenced>
 *       </mrow>
 *     </mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * is calculated as
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|">
 *       <mrow>
 *         <msub><mi>F</mi><mi>noise</mi></msub>
 *         <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       </mrow>
 *     </mfenced>
 *     <mn>2</mn>
 *   </msup>
 *   <mo>=</mo>
 *   <msup>
 *     <mfenced open="|" close="|">
 *       <mrow>
 *         <msub><mi>F</mi><mrow><mi>e</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *         <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       </mrow>
 *     </mfenced>
 *     <mn>2</mn>
 *   </msup>
 *   <mo>-</mo>
 *   <mn>2</mn>
 *   <mo>&InvisibleTimes;</mo>
 *   <msqrt>
 *     <msup>
 *       <mfenced open="|" close="|">
 *         <mrow>
 *           <msub><mi>F</mi><mrow><mi>e</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *           <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *       </mfenced>
 *       <mn>2</mn>
 *     </msup>
 *     <mo>&InvisibleTimes;</mo>
 *     <msup>
 *       <mfenced open="|" close="|">
 *         <mrow>
 *           <msub><mi>F</mi><mrow><mi>e</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *           <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *       </mfenced>
 *       <mn>2</mn>
 *     </msup>
 *   </msqrt>
 *   <mo>+</mo>
 *   <msup>
 *     <mfenced open="|" close="|">
 *       <mrow>
 *         <msub><mi>F</mi><mrow><mi>e</mi><mo>,</mo><mi>Ref</mi></mrow></msub>
 *         <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       </mrow>
 *     </mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * and grouped into bands using peaq_fftearmodel_group_into_bands() to obtain
 * the noise patterns <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>P</mi><mi>noise</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>.
 * The mask pattern <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>M</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * is calculated from the excitation pattern of the reference signal as
 * obtained by peaq_earmodel_get_excitation() from @ref_state by dividing it by
 * the masking difference as returned by
 * peaq_fftearmodel_get_masking_difference(). From these, the noise-to-mask
 * ratio is calculated as
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>NMR</mi>
 *   <mo>=</mo>
 *   <mfrac><mn>1</mn><mi>Z</mi></mfrac>
 *   <mo>&InvisibleTimes;</mo>
 *   <munderover>
 *     <mo>&sum;</mo>
 *     <mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow>
 *     <mrow><mi>Z</mi><mo>-</mo><mn>1</mn></mrow>
 *   </munderover>
 *   <mfrac>
 *     <mrow>
 *       <msub><mi>P</mi><mi>noise</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow>
 *     <mrow>
 *       <mi>M</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow>
 *   </mfrac>
 * </math></inlineequation>
 * where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>Z</mi>
 * </math></inlineequation>
 * denotes the number of bands. If @mov_accum_nmr is set to #MODE_AVG_LOG, the
 * NMR is directly accumulated (used for Total NMRB), otherwise, it is
 * converted to dB-scale first (used for Segmental NMRB).
 *
 * If @mov_accum_rel_dist_frames is not NULL, in addition, the frames where
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <munder>
 *     <mi>max</mi>
 *     <mrow><mi>k</mi><mo>&lt;</mo><mi>Z</mi></mrow>
 *   </munder>
 *   <mfrac>
 *     <mrow>
 *       <msub><mi>P</mi><mi>noise</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow>
 *     <mrow>
 *       <mi>M</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow>
 *   </mfrac>
 * </math></inlineequation>
 * exceeds 1.41253754462275 (corresponding to 1.5 dB) are counted by
 * accumlating one for frames that do exceed the threshold, a zero for those
 * that do not.
 */
void mov_nmr(FFTEarModel<> const& ear_model,
             std::vector<FFTEarModel<>::state_t> const& ref_state,
             std::vector<FFTEarModel<>::state_t> const& test_state,
             MovAccum& mov_accum_nmr,
             MovAccum& mov_accum_rel_dist_frames);
void mov_nmr(FFTEarModel<55> const& ear_model,
             std::vector<FFTEarModel<55>::state_t> const& ref_state,
             std::vector<FFTEarModel<55>::state_t> const& test_state,
             MovAccum& mov_accum_nmr);

/**
 * peaq_mov_prob_detect:
 * @ear_model: The underlying ear model to which @ref_state and
 * @test_state belong.
 * @ref_state: Ear model states for the reference signal.
 * @test_state: Ear model states for the test signal.
 * @channels: Number of audio channels being processed.
 * @mov_accum_adb: Accumulator for the ADBB MOV.
 * @mov_accum_mfpd: Accumulator for the MFPDB MOV.
 *
 * Calculates the detection probability based model output variables as described in
 * in section 4.7 of <xref linkend="BS1387" />. The excitation patterns <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>E</mi><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>E</mi><mi>Test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * are converted to dB as <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mover><mi>E</mi><mo>~</mo></mover><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mn>10</mn><mo>&InvisibleTimes;</mo><msub><mi>log</mi><mn>10</mn></msub><mfenced><mrow><msub><mi>E</mi><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced></mrow>
 * </math></inlineequation>
 * and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mover><mi>E</mi><mo>~</mo></mover><mi>Test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mn>10</mn><mo>&InvisibleTimes;</mo><msub><mi>log</mi><mn>10</mn></msub><mfenced><mrow><msub><mi>E</mi><mi>Test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced></mrow>
 * </math></inlineequation>
 * from which the asymmetric average exciation <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>L</mi><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><mn>0.3</mn><mo>&InvisibleTimes;</mo><mi>max</mi><mfenced><mrow><msub><mover><mi>E</mi><mo>~</mo></mover><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow><mrow><msub><mover><mi>E</mi><mo>~</mo></mover><mi>Test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mo>+</mo><mn>0.7</mn><mo>&InvisibleTimes;</mo><msub><mover><mi>E</mi><mo>~</mo></mover><mi>Test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * is computed. This is then used to determine the effective detection step size
 * <informalequation>
 *   <math xmlns="http://www.w3.org/1998/Math/MathML">
 *     <mrow>
 *       <mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       <mo>=</mo>
 *       <mn>5.95072</mn><mo>&sdot;</mo>
 *       <msup>
 *         <mfenced>
 *           <mrow>
 *             <mn>6.39468</mn>
 *             <mo>/</mo>
 *             <mi>L</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *           </mrow>
 *         </mfenced>
 *         <mn>1.71332</mn>
 *       </msup>
 *       <mo>+</mo>
 *       <mn>9.01033</mn><mo>&times;</mo><msup><mn>10</mn><mn>-11</mn></msup>
 *       <mo>&sdot;</mo>
 *       <msup>
 *         <mrow><mi>L</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 *         <mn>4</mn>
 *       </msup>
 *       <mo>+</mo>
 *       <mn>5.05622</mn><mo>&times;</mo><msup><mn>10</mn><mn>-6</mn></msup>
 *       <mo>&sdot;</mo>
 *       <msup>
 *         <mrow><mi>L</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 *         <mn>3</mn>
 *       </msup>
 *       <mo>-</mo>
 *       <mn>0.00102438</mn>
 *       <mo>&sdot;</mo>
 *       <msup>
 *         <mrow><mi>L</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 *         <mn>2</mn>
 *       </msup>
 *       <mo>+</mo>
 *       <mn>0.0550197</mn>
 *       <mo>&sdot;</mo>
 *       <mi>L</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       <mo>-</mo>
 *       <mn>0.198719</mn>
 *     </mrow>
 *   </math>
 * </informalequation>
 * if <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>L</mi><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>&gt;</mo><mn>0</mn></mrow>
 * </math></inlineequation>, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><msup><mn>10</mn><mn>30</mn></msup></mrow>
 * </math></inlineequation>
 * otherwise. For every channel <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>c</mi>
 * </math></inlineequation>, the probability of detection is then given by
 * <informalequation>
 *   <math xmlns="http://www.w3.org/1998/Math/MathML">
 *     <mrow>
 *       <msub><mi>p</mi><mi>c</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       <mo>=</mo>
 *       <mn>1</mn>
 *       <mo>-</mo>
 *       <msup>
 *         <mn>0.5</mn>
 *         <msup>
 *           <mfenced>
 *             <mrow>
 *               <mfenced>
 *                 <mrow>
 *                   <msub><mover><mi>E</mi><mo>~</mo></mover><mi>Ref</mi></msub>
 *                   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                   <mo>-</mo>
 *                   <msub><mover><mi>E</mi><mo>~</mo></mover><mi>Test</mi></msub>
 *                   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 </mrow>
 *               </mfenced>
 *               <mo>/</mo>
 *               <mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *             </mrow>
 *           </mfenced>
 *           <mi>b</mi>
 *         </msup>
 *       </msup>
 *     </mrow>
 *    <mspace width="2em" />
 *    <mtext> where </mtext>
 *    <mspace width="2em" />
 *    <mi>b</mi>
 *    <mo>=</mo>
 *    <mfenced open="{" close="">
 *      <mtable>
 *        <mtr>
 *          <mtd><mn>4</mn></mtd>
 *          <mtd>
 *            <mtext>if </mtext>
 *            <msub><mover><mi>E</mi><mo>~</mo></mover><mi>Ref</mi></msub>
 *            <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *            <mo>&gt;</mo>
 *            <msub><mover><mi>E</mi><mo>~</mo></mover><mi>Test</mi></msub>
 *            <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *          </mtd>
 *        </mtr>
 *        <mtr><mtd><mn>6</mn></mtd><mtd><mtext>else</mtext></mtd></mtr>
 *      </mtable>
 *    </mfenced>
 *   </math>
 * </informalequation>
 * and the total number of steps above the threshold as
 * <informalequation>
 *   <math xmlns="http://www.w3.org/1998/Math/MathML">
 *     <mrow>
 *       <msub><mi>q</mi><mi>c</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       <mo>=</mo>
 *       <mfrac>
 *         <mfenced open="|" close="|">
 *           <mrow>
 *             <mi>INT</mi>
 *             <mfenced>
 *               <mrow>
 *                 <msub><mover><mi>E</mi><mo>~</mo></mover><mi>Ref</mi></msub>
 *                 <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *                 <mo>-</mo>
 *                 <msub><mover><mi>E</mi><mo>~</mo></mover><mi>Test</mi></msub>
 *                 <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *               </mrow>
 *             </mfenced>
 *           </mrow>
 *         </mfenced>
 *         <mrow><mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 *       </mfrac>
 *     </mrow>
 *   </math>.
 * </informalequation>
 * The binaural values are then given as the respective maxima over all channels <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>p</mi><mi>bin</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><munder><mi>max</mi><mi>c</mi></munder><mfenced><mrow><msub><mi>p</mi><mi>c</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced>
 * </math></inlineequation> and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>q</mi><mi>bin</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced><mo>=</mo><munder><mi>max</mi><mi>c</mi></munder><mfenced><mrow><msub><mi>q</mi><mi>c</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced>
 * </math></inlineequation>, from which the total probability of detection is computed as
 * <informalequation>
 *   <math xmlns="http://www.w3.org/1998/Math/MathML">
 *     <mrow>
 *       <msub><mi>P</mi><mi>bin</mi></msub>
 *       <mo>=</mo>
 *       <mn>1</mn>
 *       <mo>-</mo>
 *       <munderover>
 *         <mo>&prod;</mo>
 *         <mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow>
 *         <mrow><mi>Z</mi><mo>-</mo><mn>1</mn></mrow>
 *       </munderover>
 *       <mfenced>
 *         <mrow>
 *           <mn>1</mn>
 *           <mo>-</mo>
 *           <msub><mi>p</mi><mi>bin</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *         </mrow>
 *       </mfenced>
 *     </mrow>
 *   </math>
 * </informalequation>
 * and the total number of steps above the threshold as
 * <informalequation>
 *   <math xmlns="http://www.w3.org/1998/Math/MathML">
 *     <mrow>
 *       <msub><mi>Q</mi><mi>bin</mi></msub>
 *       <mo>=</mo>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow>
 *         <mrow><mi>Z</mi><mo>-</mo><mn>1</mn></mrow>
 *       </munderover>
 *       <msub><mi>q</mi><mi>bin</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow>
 *   </math>
 * </informalequation>
 * where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>Z</mi>
 * </math></inlineequation> denotes the number of bands. The total probaility of
 * detection is accumulated in @mov_accum_mfpd, which should be set to
 * #MODE_FILTERED_MAX, and for frames with <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>P</mi><mi>bin</mi></msub><mo>&gt;</mo><mn>0.5</mn>
 * </math></inlineequation>, the total number of steps above the threshold is
 * accumulated in @mov_accum_adb, which should be set to #MODE_ADB.
 */
void mov_prob_detect(FFTEarModel<> const& ear_model,
                     std::vector<FFTEarModel<>::state_t> const& ref_state,
                     std::vector<FFTEarModel<>::state_t> const& test_state,
                     unsigned int channels,
                     MovAccum& mov_accum_adb,
                     MovAccum& mov_accum_mfpd);

/**
 * peaq_mov_ehs:
 * @ear_model: The underlying ear model to which @ref_state and
 * @test_state belong.
 * @ref_state: Ear model states for the reference signal.
 * @test_state: Ear model states for the test signal.
 * @mov_accum: Accumulator for the EHSB MOV.
 *
 * Calculates the error harmonic structure based model output variable as
 * described in section 4.8 of <xref linkend="BS1387" /> with the
 * interpretations of <xref linkend="Kabal03" />. The error harmonic structure
 * is computed based on the difference of the logarithms of the weighted power
 * spectra <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>F</mi><mi>e</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * for test and reference signal. The autocorrelation of this difference is
 * then windowed and Fourier-transformed. In the resulting cepstrum-like data,
 * the height of the maximum peak after the first valley is the EHSB model
 * output variable which is accumulated in @mov_accum.
 *
 * Two aspects in which <xref linkend="Kabal03" /> suggests to not strictly
 * follow <xref linkend="BS1387" /> can be controlled by compile-time switches:
 * * #CENTER_EHS_CORRELATION_WINDOW controls whether the window to apply is
 *   centered around lag 0 of the correlation as suggested in <xref
 *   linkend="Kabal03" /> (if unset or set to false) or centered around the
 *   middle of the correlation (if set to true).
 * * #EHS_SUBTRACT_DC_BEFORE_WINDOW controls whether the average is subtracted
 *   before windowing as suggested in <xref linkend="Kabal03" /> or afterwards.
 */
namespace detail {
template<std::size_t MAXLAG>
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
} // namespace detail

template<std::size_t BANDCOUNT>
void mov_ehs(std::vector<typename FFTEarModel<BANDCOUNT>::state_t> const& ref_state,
             std::vector<typename FFTEarModel<BANDCOUNT>::state_t> const& test_state,
             MovAccum& mov_accum)
{
  auto constexpr MAXLAG = 256;
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

  auto channels = mov_accum.get_channels();

  if (std::none_of(cbegin(ref_state),
                   cend(ref_state),
                   [](auto state) {
                     return FFTEarModel<BANDCOUNT>::is_energy_threshold_reached(state);
                   }) &&
      std::none_of(cbegin(test_state), cend(test_state), [](auto state) {
        return FFTEarModel<BANDCOUNT>::is_energy_threshold_reached(state);
      })) {
    return;
  }

  for (std::size_t chan = 0; chan < channels; chan++) {
    auto const& ref_power_spectrum =
      FFTEarModel<BANDCOUNT>::get_weighted_power_spectrum(ref_state[chan]);
    auto const& test_power_spectrum =
      FFTEarModel<BANDCOUNT>::get_weighted_power_spectrum(test_state[chan]);

    auto d = std::array<double, 2 * MAXLAG>{};
    std::transform(cbegin(ref_power_spectrum),
                   cbegin(ref_power_spectrum) + 2 * MAXLAG,
                   cbegin(test_power_spectrum),
                   begin(d),
                   [](auto fref, auto ftest) {
                     return fref == 0. && ftest == 0. ? 0.0 : std::log(ftest / fref);
                   });

    auto c = detail::do_xcorr<MAXLAG>(d);

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
    for (std::size_t i = 0; i < MAXLAG; i++) {
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
    mov_accum.accumulate(chan, 1000. * ehs, 1.);
  }
}

} // namespace peaq

#endif
