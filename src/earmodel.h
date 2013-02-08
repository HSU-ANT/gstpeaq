/* GstPEAQ
 * Copyright (C) 2006, 2011, 2013 Martin Holters <martin.holters@hsuhh.de>
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

#define PEAQ_TYPE_EARMODELPARAMS (peaq_earmodelparams_get_type ())
#define PEAQ_EARMODELPARAMS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_EARMODELPARAMS, \
                               PeaqEarModelParams))
#define PEAQ_EARMODELPARAMS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_EARMODELPARAMS, \
                            PeaqEarModelParamsClass))
#define PEAQ_EARMODELPARAMS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_EARMODELPARAMS, \
                              PeaqEarModelParamsClass))

#define PEAQ_TYPE_EARMODEL (peaq_earmodel_get_type ())
#define PEAQ_EARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_EARMODEL, PeaqEarModel))

/**
 * FRAMESIZE:
 *
 * The length (in samples) of a frame to be processed by 
 * peaq_earmodel_process().
 */

typedef struct _PeaqEarModelClass PeaqEarModelClass;
typedef struct _PeaqEarModel PeaqEarModel;

typedef struct _PeaqEarModelParamsClass PeaqEarModelParamsClass;
typedef struct _PeaqEarModelParams PeaqEarModelParams;
typedef struct _EarModelOutput EarModelOutput;

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
  gdouble *unsmeared_excitation;
  gdouble *excitation;
  gdouble overall_loudness;
};

struct _PeaqEarModelParams
{
  GObject parent;
  guint band_count;
  gdouble *fc;
  gdouble *internal_noise;
  gdouble *ear_time_constants;
  gdouble *excitation_threshold;
  gdouble *threshold;
  gdouble *loudness_factor;
};

struct _PeaqEarModelParamsClass
{
  GObjectClass parent;
  gdouble loudness_scale;
  guint step_size;
  gdouble tau_min;
  gdouble tau_100;
  gdouble (*get_playback_level) (PeaqEarModelParams const *params);
  void (*set_playback_level) (PeaqEarModelParams *params, gdouble level);
};

struct _PeaqEarModel
{
  GObject parent;
  PeaqEarModelParams *params;
};

struct _PeaqEarModelClass
{
  GObjectClass parent;
};

GType peaq_earmodelparams_get_type ();
guint peaq_earmodelparams_get_band_count (PeaqEarModelParams const *params);
guint peaq_earmodelparams_get_step_size (PeaqEarModelParams const *params);
gdouble peaq_earmodelparams_get_band_center_frequency (PeaqEarModelParams
                                                       const *params,
                                                       guint band);
gdouble peaq_earmodelparams_get_internal_noise (PeaqEarModelParams const
                                                *params, guint band);
gdouble peaq_earmodelparams_get_ear_time_constant (PeaqEarModelParams const
                                                   *params, guint band);

GType peaq_earmodel_get_type ();
PeaqEarModelParams *peaq_earmodel_get_model_params (PeaqEarModel const *ear);
gdouble peaq_earmodel_calc_loudness (PeaqEarModel const *model,
                                     gdouble *excitation);

#endif
