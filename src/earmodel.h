/* GstPEAQ
 * Copyright (C) 2006, 2011 Martin Holters <martin.holters@hsuhh.de>
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

#include <glib-object.h>

#define PEAQ_TYPE_EAR (peaq_ear_get_type ())
#define PEAQ_EAR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_EAR, PeaqEar))

#define PEAQ_TYPE_EARMODEL (peaq_earmodel_get_type ())
#define PEAQ_EARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_EARMODEL, PeaqEarModel))
#define PEAQ_EARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_EARMODEL, PeaqEarModelClass))
#define PEAQ_IS_EARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE (obj, PEAQ_TYPE_EARMODEL))
#define PEAQ_IS_EARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE (klass, PEAQ_TYPE_EARMODEL))
#define PEAQ_EARMODEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_EARMODEL, PeaqEarModelClass))

/**
 * FRAMESIZE:
 *
 * The length (in samples) of a frame to be processed by 
 * peaq_earmodel_process().
 */
#define FRAMESIZE 2048

/**
 * CRITICAL_BAND_COUNT:
 *
 * The number of auditory bands the signal is decomposed into by 
 * peaq_earmodel_group_into_bands().
 */
#define CRITICAL_BAND_COUNT 109

/**
 * EarModelOutput:
 * @power_spectrum: The power spectrum of the frame, up to half the sampling 
 * rate (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
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
 * <msup>
 *   <mfenced open="|" close="|"><mrow>
 *     <msub><mi>X</mi><mi>w</mi></msub>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow></mfenced>
 *   <mn>2</mn>
 * </msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @band_power: The total power in each auditory sub-band 
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>E</mi><mi>b</mi></msub>
 *   <mfenced open="[" close="]"><mi>i</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @unsmeared_excitation: The excitation patterns after frequency spreading, 
 * but before time-domain spreading
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>E</mi><mi>s</mi></msub>
 *   <mfenced open="[" close="]"><mi>i</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @excitation: The excitation patterns after frequency and time-domain 
 * spreading
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mover><mi>E</mi><mo>~</mo></mover><mi>s</mi></msub>
 *   <mfenced open="[" close="]"><mi>i</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @overall_loudness: The overall loundness in the frame 
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>N</mi><mi>tot</mi></msub>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />). Note that the loudness computation is
 * usually not considered part of the ear model, but the code fits in nicely
 * here.
 *
 * Holds the data calculated by the ear model for one frame of audio data.
 */
struct _EarModelOutput
{
  gdouble power_spectrum[FRAMESIZE / 2 + 1];
  gdouble weighted_power_spectrum[FRAMESIZE / 2 + 1];
  gdouble *band_power;
  gdouble *unsmeared_excitation;
  gdouble *excitation;
  gdouble overall_loudness;
};

typedef struct _PeaqEarClass PeaqEarClass;
typedef struct _PeaqEar PeaqEar;
typedef struct _PeaqEarModelClass PeaqEarModelClass;
typedef struct _PeaqEarModel PeaqEarModel;
typedef struct _EarModelOutput EarModelOutput;

GType peaq_ear_get_type ();
PeaqEarModel *peaq_ear_get_model(PeaqEar const* ear);
void peaq_ear_process (PeaqEar * ear, gfloat * sample_data,
                       EarModelOutput * output);

GType peaq_earmodel_get_type ();
guint peaq_earmodel_get_band_count (PeaqEarModel const *ear_model);
void peaq_earmodel_group_into_bands (PeaqEarModel const* ear_model,
				     gdouble * spectrum,
				     gdouble * band_power);
gdouble peaq_earmodel_get_band_center_frequency (guint band);
gdouble peaq_earmodel_get_internal_noise (PeaqEarModel const * ear_model,
					  guint band);
#endif
