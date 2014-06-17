/* GstPEAQ
 * Copyright (C) 2013 Martin Holters <martin.holters@hsuhh.de>
 *
 * movaccum.h: Model out variable (MOV) accumulation.
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


#ifndef __MOVACCUM_H__
#define __MOVACCUM_H__ 1

#include <glib-object.h>

#define PEAQ_TYPE_MOVACCUM (peaq_movaccum_get_type ())
#define PEAQ_MOVACCUM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_MOVACCUM, PeaqMovAccum))
#define PEAQ_MOVACCUM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_MOVACCUM, PeaqMovAccumClass))
#define PEAQ_IS_MOVACCUM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE (obj, PEAQ_TYPE_MOVACCUM))
#define PEAQ_IS_MOVACCUM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE (klass, PEAQ_TYPE_MOVACCUM))
#define PEAQ_MOVACCUM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_MOVACCUM, PeaqMovAccumClass))

typedef struct _PeaqMovAccumClass PeaqMovAccumClass;
typedef struct _PeaqMovAccum PeaqMovAccum;

/**
 * PeaqMovAccumMode:
 * @MODE_AVG: <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>X</mi><mi>c</mi></msub>
 *   <mo>=</mo>
 *   <mfrac>
 *     <mrow>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *         <mi>N</mi>
 *       </munderover>
 *       <msub><mi>w</mi><mi>i</mi></msub>
 *       <mo>&InvisibleTimes;</mo>
 *       <msub><mi>x</mi><mi>i</mi></msub>
 *     </mrow>
 *     <mrow>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *         <mi>N</mi>
 *       </munderover>
 *       <msub><mi>w</mi><mi>i</mi></msub>
 *     </mrow>
 *   </mfrac>
 * </math></inlineequation>
 * @MODE_AVG_LOG: <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub><mi>X</mi><mi>c</mi></msub>
 *     <mo>=</mo>
 *     <mn>10</mn>
 *     <mo>&InvisibleTimes;</mo>
 *     <msub><mi>log</mi><mn>10</mn></msub>
 *     <mo>&ApplyFunction;</mo>
 *     <mfenced open="(" close=")">
 *       <mfrac>
 *         <mrow>
 *           <munderover>
 *             <mo>&sum;</mo>
 *             <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *             <mi>N</mi>
 *           </munderover>
 *           <msub><mi>w</mi><mi>i</mi></msub>
 *           <mo>&InvisibleTimes;</mo>
 *           <msub><mi>x</mi><mi>i</mi></msub>
 *         </mrow>
 *         <mrow>
 *           <munderover>
 *             <mo>&sum;</mo>
 *             <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *             <mi>N</mi>
 *           </munderover>
 *           <msub><mi>w</mi><mi>i</mi></msub>
 *         </mrow>
 *       </mfrac>
 *     </mfenced>
 *   </mrow>
 * </math></inlineequation>
 * @MODE_RMS: <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>X</mi><mi>c</mi></msub>
 *   <mo>=</mo>
 *   <msqrt><mfrac>
 *     <mrow>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *         <mi>N</mi>
 *       </munderover>
 *       <msup><msub><mi>w</mi><mi>i</mi></msub><mn>2</mn></msup>
 *       <mo>&InvisibleTimes;</mo>
 *       <msup><msub><mi>x</mi><mi>i</mi></msub><mn>2</mn></msup>
 *     </mrow>
 *     <mrow>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *         <mi>N</mi>
 *       </munderover>
 *       <msup><msub><mi>w</mi><mi>i</mi></msub><mn>2</mn></msup>
 *     </mrow>
 *   </mfrac></msqrt>
 * </math></inlineequation>
 * @MODE_RMS_ASYM: <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub><mi>X</mi><mi>c</mi></msub>
 *     <mo>=</mo>
 *     <msqrt>
 *       <mrow>
 *         <mfrac><mn>1</mn><mi>N</mi></mfrac>
 *         <mo>&InvisibleTimes;</mo>
 *         <munderover>
 *           <mo>&sum;</mo>
 *           <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *           <mi>N</mi>
 *         </munderover>
 *         <msup><msub><mi>x</mi><mi>i</mi></msub><mn>2</mn></msup>
 *       </mrow>
 *     </msqrt>
 *     <mo>+</mo>
 *     <mfrac><mn>1</mn><mn>2</mn></mfrac>
 *     <mo>&InvisibleTimes;</mo>
 *     <msqrt>
 *       <mrow>
 *         <mfrac><mn>1</mn><mi>N</mi></mfrac>
 *         <mo>&InvisibleTimes;</mo>
 *         <munderover>
 *           <mo>&sum;</mo>
 *           <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *           <mi>N</mi>
 *         </munderover>
 *         <msup><msub><mi>w</mi><mi>i</mi></msub><mn>2</mn></msup>
 *       </mrow>
 *     </msqrt>
 *   </mrow>
 * </math></inlineequation>
 * @MODE_AVG_WINDOW: <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>X</mi><mi>c</mi></msub>
 *   <mo>=</mo>
 *   <msqrt>
 *     <mrow>
 *       <mfrac><mn>1</mn><mi>N</mi></mfrac>
 *       <mo>&InvisibleTimes;</mo>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *         <mi>N</mi>
 *       </munderover>
 *       <msup>
 *         <mfenced open="(" close=")">
 *           <mrow>
 *             <mfrac><mn>1</mn><mn>4</mn></mfrac>
 *             <mo>&InvisibleTimes;</mo>
 *             <munderover>
 *               <mo>&sum;</mo>
 *               <mrow><mi>j</mi><mo>=</mo><mn>i</mn><mo>-</mo><mn>3</mn></mrow>
 *               <mi>i</mi>
 *             </munderover>
 *             <msqrt><msub><mi>x</mi><mi>j</mi></msub></msqrt>
 *           </mrow>
 *         </mfenced>
 *         <mn>4</mn>
 *       </msup>
 *     </mrow>
 *   </msqrt>
 * </math></inlineequation>
 * @MODE_FILTERED_MAX: <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub><mi>X</mi><mi>c</mi></msub>
 *     <mo>=</mo>
 *     <mi>max</mi>
 *     <mo>&ApplyFunction;</mo>
 *     <mfenced open="{" close="}">
 *       <msub><mi>y</mi><mi>i</mi></msub>
 *     </mfenced>
 *     <mo> where </mo>
 *     <msub><mi>y</mi><mi>i</mi></msub>
 *     <mo>=</mo>
 *     <mn>0.9</mn>
 *     <mo>&InvisibleTimes;</mo>
 *     <msub><mi>y</mi><mrow><mi>i</mi><mo>-</mo><mn>1</mn></mrow></msub>
 *     <mo>+</mo>
 *     <mn>0.1</mn>
 *     <mo>&InvisibleTimes;</mo>
 *     <msub><mi>x</mi><mi>i</mi></msub>
 *   </mrow>
 * </math></inlineequation>
 * @MODE_ADB: <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub><mi>X</mi><mi>c</mi></msub>
 *     <mo>=</mo>
 *     <mfenced open="{" close="">
 *       <mtable>
 *         <mtr>
 *           <mtd><mn>0</mn></mtd>
 *           <mtd columnalign="left">
 *             <mo>for </mo>
 *             <munderover>
 *               <mo>&sum;</mo>
 *               <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *               <mi>N</mi>
 *             </munderover>
 *             <msub><mi>w</mi><mi>i</mi></msub>
 *             <mo>=</mo><mn>0</mn>
 *           </mtd>
 *         </mtr>
 *         <mtr>
 *           <mtd><mn>-0.5</mn></mtd>
 *           <mtd columnalign="left">
 *             <mo>for </mo>
 *             <munderover>
 *               <mo>&sum;</mo>
 *               <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *               <mi>N</mi>
 *             </munderover>
 *             <msub><mi>w</mi><mi>i</mi></msub>
 *             <mo>&InvisibleTimes;</mo>
 *             <msub><mi>x</mi><mi>i</mi></msub>
 *             <mo>=</mo><mn>0</mn>
 *             <mtext>,</mtext><mspace width="1ex" />
 *             <munderover>
 *               <mo>&sum;</mo>
 *               <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *               <mi>N</mi>
 *             </munderover>
 *             <msub><mi>w</mi><mi>i</mi></msub>
 *             <mo>&ne;</mo><mn>0</mn>
 *           </mtd>
 *         </mtr>
 *         <mtr>
 *           <mtd>
 *             <msub><mi>log</mi><mn>10</mn></msub>
 *             <mo>&ApplyFunction;</mo>
 *             <mfenced open="(" close=")">
 *               <mfrac>
 *                 <mrow>
 *                   <munderover>
 *                     <mo>&sum;</mo>
 *                     <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *                     <mi>N</mi>
 *                   </munderover>
 *                   <msub><mi>w</mi><mi>i</mi></msub>
 *                   <mo>&InvisibleTimes;</mo>
 *                   <msub><mi>x</mi><mi>i</mi></msub>
 *                 </mrow>
 *                 <mrow>
 *                   <munderover>
 *                     <mo>&sum;</mo>
 *                     <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *                     <mi>N</mi>
 *                   </munderover>
 *                   <msub><mi>w</mi><mi>i</mi></msub>
 *                 </mrow>
 *               </mfrac>
 *             </mfenced>
 *           </mtd>
 *           <mtd columnalign="left"><mo>else</mo></mtd>
 *         </mtr>
 *       </mtable>
 *     </mfenced>
 *   </mrow>
 * </math></inlineequation>
 *
 * Accumulation mode of MOV.
 */
typedef enum
{
  MODE_AVG,
  MODE_AVG_LOG,
  MODE_RMS,
  MODE_RMS_ASYM,
  MODE_AVG_WINDOW,
  MODE_FILTERED_MAX,
  MODE_ADB
} PeaqMovAccumMode;

GType peaq_movaccum_get_type ();
PeaqMovAccum *peaq_movaccum_new ();
void peaq_movaccum_set_channels (PeaqMovAccum *acc, guint channels);
guint peaq_movaccum_get_channels (PeaqMovAccum const *acc);
void peaq_movaccum_set_mode (PeaqMovAccum *acc, PeaqMovAccumMode mode);
PeaqMovAccumMode peaq_movaccum_get_mode (PeaqMovAccum *acc);
void peaq_movaccum_set_tentative (PeaqMovAccum *acc, gboolean tentative);
void peaq_movaccum_accumulate (PeaqMovAccum *acc, guint c, gdouble val,
                               gdouble weight);
gdouble peaq_movaccum_get_value (PeaqMovAccum const *acc);

#endif
