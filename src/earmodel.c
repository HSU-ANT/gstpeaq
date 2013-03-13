/* GstPEAQ
 * Copyright (C) 2006, 2007, 2011, 2012, 2013
 * Martin Holters <martin.holters@hsuhh.de>
 *
 * earmodel.c: Peripheral ear model part.
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
 * SECTION:earmodel
 * @short_description: Common base class for FFT and filter bank base ear model.
 *
 * #PeaqEarModel is the common base class of both #PeaqFFTEarModel and
 * #PeaqFilterbankEarModel. It contains code for those computations which are
 * same or similar if both ear models. The derived models specialize by
 * overloading the fields of #PeaqEarModelClass and appropriately setting the
 * #PeaqEarModel:band-centers property.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "earmodel.h"
#include "gstpeaq.h"

enum
{
  PROP_0,
  PROP_PLAYBACK_LEVEL,
  PROP_BAND_CENTER_FREQUENCIES
};


static void class_init (gpointer klass, gpointer class_data);
static void init (GTypeInstance *obj, gpointer klass);
static void finalize (GObject *obj);
static void update_ear_time_constants (PeaqEarModel *model);
static void get_property (GObject *obj, guint id, GValue *value,
                          GParamSpec *pspec);
static void set_property (GObject *obj, guint id, const GValue *value,
                          GParamSpec *pspec);


GType
peaq_earmodel_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqEarModelClass), /* class_size */
      NULL,                       /* base_init */
      NULL,                       /* base_finalize */
      class_init,                 /* class_init */
      NULL,                       /* class_finalize */
      NULL,                       /* class_data */
      sizeof (PeaqEarModel),      /* instance_size */
      0,                          /* n_preallocs */
      init                        /* instance_init */
    };
    type = g_type_register_static (G_TYPE_OBJECT, "PeaqEarModel", &info, 0);
  }
  return type;
}

static void
class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = finalize;

  /* set property setter/getter functions and install property for playback 
   * level */
  object_class->set_property = set_property;
  object_class->get_property = get_property;
  /**
   * PeaqEarModel:playback-level:
   *
   * The playback level (in dB) assumed. Defaults to 92dB SPL.
   */
  g_object_class_install_property (object_class,
                                   PROP_PLAYBACK_LEVEL,
                                   g_param_spec_double ("playback-level",
                                                        "playback level",
                                                        "Playback level in dB",
                                                        0, 130, 92,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));
  /**
   * PeaqEarModel:band-centers:
   *
   * The band center frequencies (as a #GArray of gdoubles).
   */
  g_object_class_install_property (object_class,
                                   PROP_BAND_CENTER_FREQUENCIES,
                                   g_param_spec_pointer ("band-centers",
                                                         "band center frequencies",
                                                         "Band center frequencies in Hz as gdoubles in a GArray",
                                                         G_PARAM_READWRITE));
}

static void
init (GTypeInstance *obj, gpointer klass)
{
  PeaqEarModel *model = PEAQ_EARMODEL (obj);

  model->band_count = 0;

  model->fc = g_new (gdouble, model->band_count);
  model->internal_noise = g_new (gdouble, model->band_count);
  model->ear_time_constants = g_new (gdouble, model->band_count);
  model->excitation_threshold = g_new (gdouble, model->band_count);
  model->threshold = g_new (gdouble, model->band_count);
  model->loudness_factor = g_new (gdouble, model->band_count);
}

static void
finalize (GObject *obj)
{
  PeaqEarModel *model = PEAQ_EARMODEL (obj);
  GObjectClass *parent_class =
    G_OBJECT_CLASS (g_type_class_peek_parent
                    (g_type_class_peek (PEAQ_TYPE_EARMODEL)));
  g_free (model->fc);
  g_free (model->internal_noise);
  g_free (model->ear_time_constants);
  g_free (model->excitation_threshold);
  g_free (model->threshold);
  g_free (model->loudness_factor);

  parent_class->finalize (obj);
}

/**
 * peaq_earmodel_state_alloc:
 * @model: The #PeaqEarModel instance to allocate state data for.
 *
 * The state data is allocated using the <structfield>state_alloc</structfield>
 * function provided by the derived class. When the state is no longer needed,
 * it should be freed with peaq_earmodel_state_free().
 *
 * Note that changing properties of the #PeaqEarModel may require obtaining a
 * new state data instance (of different size).
 *
 * Returns: Newly allocated state data for this #PeaqEarModel instance.
 */
gpointer
peaq_earmodel_state_alloc (PeaqEarModel const *model)
{
  return PEAQ_EARMODEL_GET_CLASS (model)->state_alloc (model);
}

/**
 * peaq_earmodel_state_free:
 * @model: The #PeaqEarModel instance to free state data for.
 * @state: The state data to free.
 *
 * Frees the state data allocated with peaq_earmodel_state_alloc().
 */
void
peaq_earmodel_state_free (PeaqEarModel const *model, gpointer state)
{
  PEAQ_EARMODEL_GET_CLASS (model)->state_free (model, state);
}

/**
 * peaq_earmodel_process_block:
 * @model: The #PeaqEarModel instance to free state data for.
 * @state: The instance state data.
 * @samples: One frame of input data.
 *
 * Invokes the <structfield>process_block</structfield> function of the derived
 * class to process one frame of audio. The required length of the frame
 * differs between #PeaqFFTEarModel and #PeaqFilterbankEarModel; it may be
 * determined by calling peaq_earmodel_get_frame_size(). Similarly, the amount
 * by which to advance the data between successive invocations can be
 * determined by calling peaq_earmodel_get_step_size(). Any state
 * information required between successive invocations is kept in @state, which
 * has be allocated with peaq_earmodel_state_alloc() beforehand.
 */
void
peaq_earmodel_process_block (PeaqEarModel const *model, gpointer state,
                             gfloat const *samples)
{
  PEAQ_EARMODEL_GET_CLASS (model)->process_block (model, state, samples);
}

/**
 * peaq_earmodel_get_excitation:
 * @model: The underlying #PeaqEarModel.
 * @state: The current state from which to extract the excitation.
 *
 * Returns the current excitation patterns after frequency and time-domain
 * spreading
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mover><mi>E</mi><mo>~</mo></mover><mi>s</mi></msub>
 *   <mfenced open="[" close="]"><mi>i</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />)
 * as computed during the last call to peaq_earmodel_process_block().
 *
 * Returns: The current excitation.
 */
gdouble const *
peaq_earmodel_get_excitation (PeaqEarModel const *model, gpointer state)
{
  return PEAQ_EARMODEL_GET_CLASS (model)->get_excitation (model, state);
}

/**
 * peaq_earmodel_get_unsmeared_excitation:
 * @model: The underlying #PeaqEarModel.
 * @state: The current state from which to extract the unsmeared excitation.
 *
 * Returns the current unsmeared excitation patterns after frequency, but
 * before time-domain spreading
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>E</mi><mi>s</mi></msub>
 *   <mfenced open="[" close="]"><mi>i</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />)
 * as computed during the last call to peaq_earmodel_process_block().
 *
 * Returns: The current excitation.
 */
gdouble const *
peaq_earmodel_get_unsmeared_excitation (PeaqEarModel const *model,
                                        gpointer state)
{
  return PEAQ_EARMODEL_GET_CLASS (model)->get_unsmeared_excitation (model,
                                                                    state);
}

/**
 * peaq_earmodel_get_band_count:
 * @model: The #PeaqEarModel to obtain the number of bands of.
 *
 * Returns the number of frequency bands, i.e. the length of the array the
 * #PeaqEarModel:band-centers property was set to.
 *
 * Returns: Number of frequency bands currently used by @model.
 */
guint
peaq_earmodel_get_band_count (PeaqEarModel const *model)
{
  return model->band_count;
}

static void
params_set_bands (PeaqEarModel *model, gdouble *fc, guint band_count)
{
  if (band_count != model->band_count) {
    guint band;

    model->band_count = band_count;

    g_free (model->fc);
    g_free (model->internal_noise);
    g_free (model->ear_time_constants);
    g_free (model->excitation_threshold);
    g_free (model->threshold);
    g_free (model->loudness_factor);

    model->fc = g_new (gdouble, model->band_count);
    model->internal_noise = g_new (gdouble, model->band_count);
    model->ear_time_constants = g_new (gdouble, model->band_count);
    model->excitation_threshold = g_new (gdouble, model->band_count);
    model->threshold = g_new (gdouble, model->band_count);
    model->loudness_factor = g_new (gdouble, model->band_count);

    for (band = 0; band < model->band_count; band++) {
      gdouble curr_fc = fc[band];
      model->fc[band] = curr_fc;
      /* internal noise; (13) in [BS1387] (18) in [Kabal03] */
      model->internal_noise[band] =
        pow (10., 0.4 * 0.364 * pow (curr_fc / 1000., -0.8));
      /* excitation threshold; (60) in [BS1387], (70) in [Kabal03] */
      model->excitation_threshold[band] =
        pow (10., 0.364 * pow (curr_fc / 1000., -0.8));
      /* threshold index; (61) in [BS1387], (69) in [Kabal03] */
      model->threshold[band] =
        pow (10.,
             0.1 * (-2. - 2.05 * atan (curr_fc / 4000.) -
                    0.75 * atan (curr_fc / 1600. * curr_fc / 1600.)));
      /* loudness scaling factor; part of (58) in [BS1387], (69) in [Kabal03] */
      model->loudness_factor[band] =
        PEAQ_EARMODEL_GET_CLASS (model)->loudness_scale *
        pow (model->excitation_threshold[band] /
             (1e4 * model->threshold[band]), 0.23);
    }

    update_ear_time_constants (model);
  }
}

/**
 * peaq_earmodel_get_frame_size:
 * @model: The #PeaqEarModel to obtain the frame size of.
 *
 * Returns the size of the frames needed by peaq_earmodel_process_block().
 *
 * Returns: The frame size required by @model.
 */
guint
peaq_earmodel_get_frame_size (PeaqEarModel const *model)
{
  return PEAQ_EARMODEL_GET_CLASS (model)->frame_size;
}

/**
 * peaq_earmodel_get_step_size:
 * @model: The #PeaqEarModel to obtain the step size of.
 *
 * Returns the step size with which the sample data has to advance between
 * successive calls to peaq_earmodel_process_block().
 *
 * Returns: The step size required by @model.
 */
guint
peaq_earmodel_get_step_size (PeaqEarModel const *model)
{
  return PEAQ_EARMODEL_GET_CLASS (model)->step_size;
}

/**
 * peaq_earmodel_get_band_center_frequency:
 * @model: The #PeaqEarModel to query for a band center frequency.
 * @band: The number of the band to obtain the center frequency of.
 *
 * Returns the center frequency of the given @band taken from the data the
 * #PeaqEarModel:band-centers property was set to.
 *
 * Returns: The center frequency of @band.
 */
gdouble
peaq_earmodel_get_band_center_frequency (PeaqEarModel const *model, guint band)
{
  return model->fc[band];
}

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
 * and 3.6 in <xref linkend="Kabal03" />), where <inlineequation>
 * <math xmlns="http://www.w3.org/1998/Math/MathML"><mi>k</mi></math>
 * </inlineequation> is the band number given by @band and <inlineequation>
 * <math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>f</mi><mi>c</mi></msub>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation> the center frequency of that band.
 *
 * Note that the actual computation is performed upon setting the
 * #PeaqEarModel:band-centers property, so this function is fast as it only
 * returns a stored value.
 *
 * Returns: The internal noise at @band.
 */
gdouble
peaq_earmodel_get_internal_noise (PeaqEarModel const *model, guint band)
{
  return model->internal_noise[band];
}

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
 * and 3.7 in <xref linkend="Kabal03" />), where <inlineequation>
 * <math xmlns="http://www.w3.org/1998/Math/MathML"><mi>k</mi></math>
 * </inlineequation> is the band number given by @band and <inlineequation>
 * <math xmlns="http://www.w3.org/1998/Math/MathML">
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
gdouble
peaq_earmodel_get_ear_time_constant (PeaqEarModel const *model,
                                     guint band)
{
  return model->ear_time_constants[band];
}

static void
update_ear_time_constants(PeaqEarModel *model)
{
  guint band;
  PeaqEarModelClass *model_class =
    PEAQ_EARMODEL_GET_CLASS (model);
  gdouble tau_min = model_class->tau_min;
  gdouble tau_100 = model_class->tau_100;

  for (band = 0; band < model->band_count; band++) {
    model->ear_time_constants[band] = 
      peaq_earmodel_calc_time_constant (model, band, tau_min, tau_100);
  }
}

static void
get_property (GObject *obj, guint id, GValue *value,
                            GParamSpec *pspec)
{
  PeaqEarModel *model = PEAQ_EARMODEL (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      g_value_set_double (value,
                          PEAQ_EARMODEL_GET_CLASS
                          (obj)->get_playback_level (model));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
      break;
  }
}

/*
 * set_property:
 * @obj: the object structure.
 * @id: the id of the property to be set.
 * @value: the value to set the property to.
 * @pspec: the #GParamSpec of the property to be set.
 *
 * Sets the property specified by @id to the given @value.
 */
static void
set_property (GObject *obj, guint id,
                                  const GValue *value, GParamSpec *pspec)
{
  PeaqEarModel *model = PEAQ_EARMODEL (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      PEAQ_EARMODEL_GET_CLASS (obj)->set_playback_level (model,
                                                         g_value_get_double (value));
      break;
    case PROP_BAND_CENTER_FREQUENCIES:
      {
        GArray *fc_array;
        fc_array = g_value_get_pointer (value);
        if (fc_array) {
          params_set_bands (model, (gdouble *) fc_array->data, fc_array->len);
        } else {
          params_set_bands (model, NULL, 0);
        }
        break;
      }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
      break;
  }
}

/**
 * peaq_earmodel_calc_time_constant:
 * @model: The #PeaqEarModel to use.
 * @band: The frequency band 
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>k</mi>
 * </math></inlineequation>
 * for which to calculate the time constant.
 * @tau_min: The value of
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>&tau;</mi><mi>min</mi></msub>
 * </math></inlineequation>.
 * @tau_100: The value of
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>&tau;</mi><mn>100</mn></msub>
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
 * where 
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>StepSize</mi>
 * </math></inlineequation>
 * and the center frequency
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>f</mi><mi>c</mi></msub>
 *   <mfenced open="[" close="]">
 *     <mi>k</mi>
 *   </mfenced>
 * </math></inlineequation>
 * of the
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>k</mi>
 * </math></inlineequation>-th band are taken from the given #PeaqEarModel
 * @model.
 *
 * Returns: The time constant
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>a</mi>
 * </math></inlineequation>.
 */
gdouble
peaq_earmodel_calc_time_constant (PeaqEarModel const *model, guint band,
                                  gdouble tau_min, gdouble tau_100)
{
  guint step_size = peaq_earmodel_get_step_size (model);
  /* (21), (38), (41), and (56) in [BS1387], (32) in [Kabal03] */
  gdouble tau = tau_min + 100. / model->fc[band] * (tau_100 - tau_min);
  /* (24), (40), and (44) in [BS1387], (33) in [Kabal03] */
  return exp (step_size / (-48000. * tau));
}

/**
 * peaq_earmodel_calc_ear_weight:
 * @frequency: The frequency
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>f</mi>
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
 * for the given frequency (see sections 2.1.4 and 2.2.6 in
 * <xref linkend="BS1387" /> and section 2.5 in <xref linkend="Kabal03" />).
 *
 * Returns: The middle and outer ear weight at the given frequency.
 */
gdouble
peaq_earmodel_calc_ear_weight (gdouble frequency)
{
  gdouble f_kHz = frequency / 1000.;
  gdouble W_dB =
    -0.6 * 3.64 * pow (f_kHz, -0.8) + 6.5 * exp (-0.6 * pow (f_kHz - 3.3, 2)) -
    1e-3 * pow (f_kHz, 3.6);
  return pow (10, W_dB / 20);
}


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
 * (see section 3.3 in <xref linkend="BS1387" /> and section 4.3 in
 * <xref linkend="Kabal03" />), where
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>E</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
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
gdouble
peaq_earmodel_calc_loudness (PeaqEarModel const *model,
                             gpointer state)
{

  guint i;
  gdouble overall_loudness = 0.;
  gdouble const* excitation = peaq_earmodel_get_excitation (model, state);
  for (i = 0; i < model->band_count; i++) {
    gdouble loudness = model->loudness_factor[i]
      * (pow (1. - model->threshold[i] +
              model->threshold[i] * excitation[i] /
              model->excitation_threshold[i], 0.23) - 1.);
    overall_loudness += MAX (loudness, 0.);
  }
  overall_loudness *= 24. / model->band_count;
  return overall_loudness;
}
