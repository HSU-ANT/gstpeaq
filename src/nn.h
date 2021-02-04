/* GstPEAQ
 * Copyright (C) 2014, 2015, 2021 Martin Holters <martin.holters@hsu-hh.de>
 *
 * nn.h: Evaluate neural network to compute DI and ODG
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

#ifndef __NN_H_
#define __NN_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

/**
 * SECTION:nn
 * @short_description: Neural network for DI and ODG calculation.
 * @title: Neural network
 *
 * These functions encapsulate the neural-network-based computations described
 * in chapter 6 of <xref linkend="BS1387" />. They calculate the distortion
 * index from the model output variables or the objective difference grade from
 * the distortion index. While the calculation of the distortion index differs
 * between basic and advanced version, calculation of the objective difference
 * grade is the same.
 */

/**
 * peaq_calculate_di_basic:
 * @movs: Array of model output variables to calculate the distortion index
 * from.
 *
 * Calculates the distorion index
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>DI</mi><mo>=</mo>
 *   <msub><mi>w</mi><mrow><mi>y</mi><mn>0</mn></mrow></msub>
 *   <mo>+</mo>
 *   <munderover>
 *     <mo>&sum;</mo>
 *     <mrow><mi>j</mi><mo>=</mo><mn>0</mn></mrow>
 *     <mrow><mn>2</mn></mrow>
 *   </munderover>
 *   <msub><mi>w</mi><mi>y</mi></msub>
 *   <mfenced open="[" close="]"><mi>i</mi><mi>j</mi></mfenced>
 *   <mo>&sdot;</mo>
 *   <mi>sig</mi>
 *   <mfenced>
 *     <mrow>
 *       <msub><mi>w</mi><mrow><mi>x</mi><mn>0</mn></mrow></msub>
 *       <mfenced open="[" close="]"><mi>j</mi></mfenced>
 *       <mo>+</mo>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>0</mn></mrow>
 *         <mrow><mn>10</mn></mrow>
 *       </munderover>
 *       <msub><mi>w</mi><mi>x</mi></msub>
 *       <mfenced open="[" close="]"><mi>i</mi><mi>j</mi></mfenced>
 *       <mo>&sdot;</mo>
 *       <mfrac>
 *         <mrow>
 *           <mi>x</mi><mfenced open="[" close="]"><mi>i</mi></mfenced>
 *           <mo>-</mo>
 *           <msub><mi>a</mi><mi>min</mi></msub>
 *           <mfenced open="[" close="]"><mi>i</mi></mfenced>
 *         </mrow>
 *         <mrow>
 *           <msub><mi>a</mi><mi>max</mi></msub>
 *           <mfenced open="[" close="]"><mi>i</mi></mfenced>
 *           <mo>-</mo>
 *           <msub><mi>a</mi><mi>min</mi></msub>
 *           <mfenced open="[" close="]"><mi>i</mi></mfenced>
 *         </mrow>
 *       </mfrac>
 *     </mrow>
 *   </mfenced>
 * </math></informalequation>
 * for the basic version from the model output variables <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>x</mi><mfenced open="[" close="]"><mi>i</mi></mfenced>
 * </math></inlineequation> passed as @movs as described in chapter 6 of <xref
 * linkend="BS1387" />, where
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>sig</mi>
 *   <mfenced><mi>x</mi></mfenced>
 *   <mo>=</mo>
 *   <mfrac>
 *     <mn>1</mn>
 *     <mrow>
 *       <mn>1</mn>
 *       <mo>+</mo>
 *       <msup>
 *         <mi>e</mi>
 *         <mrow><mo>-</mo><mi>x</mi></mrow>
 *       </msup>
 *     </mrow>
 *   </mfrac>
 * </math></informalequation>
 * and the various constants are given in section 6.2 of <xref linkend="BS1387"
 * />.  The model output variables must be stored in the order
 * * BandwidthRef
 * * BandwidthTest
 * * TotalNMR
 * * WinModDiff1
 * * ADB
 * * EHS
 * * AvgModDiff1
 * * AvgModDiff2
 * * RmsNoiseLoud
 * * MFPD
 * * RelDistFrames
 *
 * in the array @movs.
 *
 * If #CLAMP_MOVS is set to true, the values of the model variables are clamped
 * to the range <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mfenced open="[" close="]"><msub><mi>a</mi><mi>min</mi></msub><msub><mi>a</mi><mi>max</mi></msub></mfenced>
 * </math></inlineequation> prior to the computation.
 *
 * Returns: The calculated distortion index.
 */
double peaq_calculate_di_basic (double const *movs);

/**
 * peaq_calculate_di_advanced:
 * @movs: Array of model output variables to calculate the distortion index
 * from.
 *
 * Calculates the distorion index
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>DI</mi><mo>=</mo>
 *   <msub><mi>w</mi><mrow><mi>y</mi><mn>0</mn></mrow></msub>
 *   <mo>+</mo>
 *   <munderover>
 *     <mo>&sum;</mo>
 *     <mrow><mi>j</mi><mo>=</mo><mn>0</mn></mrow>
 *     <mrow><mn>4</mn></mrow>
 *   </munderover>
 *   <msub><mi>w</mi><mi>y</mi></msub>
 *   <mfenced open="[" close="]"><mi>i</mi><mi>j</mi></mfenced>
 *   <mo>&sdot;</mo>
 *   <mi>sig</mi>
 *   <mfenced>
 *     <mrow>
 *       <msub><mi>w</mi><mrow><mi>x</mi><mn>0</mn></mrow></msub>
 *       <mfenced open="[" close="]"><mi>j</mi></mfenced>
 *       <mo>+</mo>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>0</mn></mrow>
 *         <mrow><mn>4</mn></mrow>
 *       </munderover>
 *       <msub><mi>w</mi><mi>x</mi></msub>
 *       <mfenced open="[" close="]"><mi>i</mi><mi>j</mi></mfenced>
 *       <mo>&sdot;</mo>
 *       <mfrac>
 *         <mrow>
 *           <mi>x</mi><mfenced open="[" close="]"><mi>i</mi></mfenced>
 *           <mo>-</mo>
 *           <msub><mi>a</mi><mi>min</mi></msub>
 *           <mfenced open="[" close="]"><mi>i</mi></mfenced>
 *         </mrow>
 *         <mrow>
 *           <msub><mi>a</mi><mi>max</mi></msub>
 *           <mfenced open="[" close="]"><mi>i</mi></mfenced>
 *           <mo>-</mo>
 *           <msub><mi>a</mi><mi>min</mi></msub>
 *           <mfenced open="[" close="]"><mi>i</mi></mfenced>
 *         </mrow>
 *       </mfrac>
 *     </mrow>
 *   </mfenced>
 * </math></informalequation>
 * for the advanced version from the model output variables <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>x</mi><mfenced open="[" close="]"><mi>i</mi></mfenced>
 * </math></inlineequation> passed as @movs as described in chapter 6 of <xref
 * linkend="BS1387" />, where
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>sig</mi>
 *   <mfenced><mi>x</mi></mfenced>
 *   <mo>=</mo>
 *   <mfrac>
 *     <mn>1</mn>
 *     <mrow>
 *       <mn>1</mn>
 *       <mo>+</mo>
 *       <msup>
 *         <mi>e</mi>
 *         <mrow><mo>-</mo><mi>x</mi></mrow>
 *       </msup>
 *     </mrow>
 *   </mfrac>
 * </math></informalequation>
 * and the various constants are given in section 6.3 of <xref linkend="BS1387"
 * />.  The model output variables must be stored in the order
 * * RmsModDiff1
 * * RmsNoiseLoudAsym
 * * SegmentalNMR
 * * EHS
 * * AvgLinDist
 *
 * in the array @movs.
 *
 * If #CLAMP_MOVS is set to true, the values of the model variables are clamped
 * to the range <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mfenced open="[" close="]"><msub><mi>a</mi><mi>min</mi></msub><msub><mi>a</mi><mi>max</mi></msub></mfenced>
 * </math></inlineequation> prior to the computation.
 *
 * Returns: The calculated distortion index.
 */
double peaq_calculate_di_advanced (double const *movs);

/**
 * peaq_calculate_odg:
 * @distortion_index: The distortion index to calculate the objective
 * difference grade from.
 *
 * Calculates the objective difference grade
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>ODG</mi><mo>=</mo>
 *   <mn>-3.98</mn>
 *   <mo>+</mo>
 *   <mn>4.2</mn>
 *   <mo>&sdot;</mo>
 *   <mi>sig</mi><mfenced><mi>DI</mi></mfenced>
 * </math></informalequation>
 * from the disstorion index chapter 6 of <xref linkend="BS1387" />, where
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>sig</mi>
 *   <mfenced><mi>x</mi></mfenced>
 *   <mo>=</mo>
 *   <mfrac>
 *     <mn>1</mn>
 *     <mrow>
 *       <mn>1</mn>
 *       <mo>+</mo>
 *       <msup>
 *         <mi>e</mi>
 *         <mrow><mo>-</mo><mi>x</mi></mrow>
 *       </msup>
 *     </mrow>
 *   </mfrac>
 * </math></informalequation>.
 *
 * Returns: The calculated objective difference grade.
 */
double peaq_calculate_odg (double distortion_index);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __NN_H__ */
