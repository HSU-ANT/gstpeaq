/* GstPEAQ
 * Copyright (C) 2006, 2011, 2013 Martin Holters <martin.holters@hsuhh.de>
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
 * FFT_FRAMESIZE:
 *
 * The length (in samples) of a frame to be processed by 
 * peaq_earmodel_process_block() for #PeaqFFTEarModel instances.
 */
#define FFT_FRAMESIZE 2048

typedef struct _PeaqFFTEarModelClass PeaqFFTEarModelClass;
typedef struct _PeaqFFTEarModel PeaqFFTEarModel;
typedef struct _FFTEarModelOutput FFTEarModelOutput;

/**
 * FFTEarModelOutput:
 * @ear_model_output: The basic #EarModelOutput structure.
 * @power_spectrum: The power spectrum of the frame, up to half the sampling 
 * rate (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <mi>F</mi>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />,<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 * <msubsup><mi>G</mi><mi>L</mi><mn>2</mn></msubsup>
 * <mo>&InvisibleTimes;</mo>
 * <msup>
 *   <mfenced open="|" close="|"><mrow>
 *     <mi>X</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow></mfenced>
 *   <mn>2</mn>
 * </msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @weighted_power_spectrum: The power spectrum weighted with the outer ear
 * weighting function 
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
 * in <xref linkend="BS1387" />,<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 * <msup>
 *   <mfenced open="|" close="|"><mrow>
 *     <msub><mi>X</mi><mi>w</mi></msub>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow></mfenced>
 *   <mn>2</mn>
 * </msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 *
 * Extends the #EarModelOutput structure with additional fields only computed
 * by the #PeaqFFTEarModel.
 */
struct _FFTEarModelOutput
{
  EarModelOutput ear_model_output;
  gdouble power_spectrum[FFT_FRAMESIZE / 2 + 1];
  gdouble weighted_power_spectrum[FFT_FRAMESIZE / 2 + 1];
};

void peaq_fftearmodel_group_into_bands (PeaqFFTEarModel const *model,
                                        gdouble const *spectrum,
                                        gdouble *band_power);
gdouble const *peaq_fftearmodel_get_masking_difference (PeaqFFTEarModel const *model);
GType peaq_fftearmodel_get_type ();
#endif
