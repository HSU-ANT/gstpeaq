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
 * @short_description: Transforms a time domain signal into pitch domain 
 * excitation patterns.
 *
 * The computation is thoroughly described in section 2 of 
 * <xref linkend="Kabal03" />.
 *
 * The main processing is performed in peaq_earmodel_process(), where frames of
 * length %FRAMESIZE samples are processed, which should be overlapped by 50% 
 * from one invocation to the next. The first step is to apply a Hann window 
 * and transform the frame to the frequency domain. Then, a filter modelling
 * the effects of the outer and middle ear is applied by weighting the 
 * spectral coefficients. These are grouped into frequency bands of one fourth 
 * the width of the critical bands of auditoriy perception to reach the pitch 
 * domain. To model the internal noise of the ear, a pitch-dependent term is
 * added to the power in each band. Finally, spreading in the pitch domain and 
 * smearing in the time-domain are performed. The necessary state information 
 * for the time smearing is stored in the #PeaqEarModel.
 *
 * Additionally, the overall loudness of the frame is computed, although this 
 * is not strictly part of the ear model.
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


static void peaq_earmodelparams_class_init (gpointer klass,
                                            gpointer class_data);
static void peaq_earmodelparams_init (GTypeInstance *obj, gpointer klass);
static void peaq_earmodelparams_finalize (GObject *obj);
static void update_ear_time_constants (PeaqEarModelParams *params);
static void peaq_earmodelparams_get_property (GObject *obj, guint id,
                                              GValue *value,
                                              GParamSpec *pspec);
static void peaq_earmodelparams_set_property (GObject *obj, guint id,
                                              const GValue *value,
                                              GParamSpec *pspec);

static void peaq_earmodel_class_init (gpointer klass, gpointer class_data);
static void peaq_earmodel_init (GTypeInstance *obj, gpointer klass);


GType
peaq_earmodelparams_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqEarModelParamsClass), /* class_size */
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      peaq_earmodelparams_class_init,   /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof (PeaqEarModelParams),      /* instance_size */
      0,                        /* n_preallocs */
      peaq_earmodelparams_init  /* instance_init */
    };
    type = g_type_register_static (G_TYPE_OBJECT,
                                   "GstPeaqEarModelParams", &info, 0);
  }
  return type;
}

static void
peaq_earmodelparams_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = peaq_earmodelparams_finalize;

  /* set property setter/getter functions and install property for playback 
   * level */
  object_class->set_property = peaq_earmodelparams_set_property;
  object_class->get_property = peaq_earmodelparams_get_property;
  g_object_class_install_property (object_class,
                                   PROP_PLAYBACK_LEVEL,
                                   g_param_spec_double ("playback_level",
                                                        "playback level",
                                                        "Playback level in dB",
                                                        0, 130, 92,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));
  g_object_class_install_property (object_class,
                                   PROP_BAND_CENTER_FREQUENCIES,
                                   g_param_spec_pointer ("band-centers",
                                                         "band center frequencies",
                                                         "Band center frequencies in Hz as gdoubles in a GArray",
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT));
}

static void
peaq_earmodelparams_init (GTypeInstance *obj, gpointer klass)
{
  PeaqEarModelParams *params = PEAQ_EARMODELPARAMS (obj);

  params->band_count = 0;

  params->fc = g_new (gdouble, params->band_count);
  params->internal_noise = g_new (gdouble, params->band_count);
  params->ear_time_constants = g_new (gdouble, params->band_count);
  params->excitation_threshold = g_new (gdouble, params->band_count);
  params->threshold = g_new (gdouble, params->band_count);
  params->loudness_factor = g_new (gdouble, params->band_count);
}

static void
peaq_earmodelparams_finalize (GObject *obj)
{
  PeaqEarModelParams *params = PEAQ_EARMODELPARAMS (obj);
  GObjectClass *parent_class =
    G_OBJECT_CLASS (g_type_class_peek_parent
                    (g_type_class_peek (PEAQ_TYPE_FFTEARMODELPARAMS)));
  g_free (params->fc);
  g_free (params->internal_noise);
  g_free (params->ear_time_constants);
  g_free (params->excitation_threshold);
  g_free (params->threshold);
  g_free (params->loudness_factor);

  parent_class->finalize (obj);
}

guint
peaq_earmodelparams_get_band_count (PeaqEarModelParams const *params)
{
  return params->band_count;
}

static void
params_set_bands (PeaqEarModelParams *params, gdouble *fc, guint band_count)
{
  if (band_count != params->band_count) {
    guint band;

    params->band_count = band_count;

    g_free (params->fc);
    g_free (params->internal_noise);
    g_free (params->ear_time_constants);
    g_free (params->excitation_threshold);
    g_free (params->threshold);
    g_free (params->loudness_factor);

    params->fc = g_new (gdouble, params->band_count);
    params->internal_noise = g_new (gdouble, params->band_count);
    params->ear_time_constants = g_new (gdouble, params->band_count);
    params->excitation_threshold = g_new (gdouble, params->band_count);
    params->threshold = g_new (gdouble, params->band_count);
    params->loudness_factor = g_new (gdouble, params->band_count);

    for (band = 0; band < params->band_count; band++) {
      gdouble curr_fc = fc[band];
      params->fc[band] = curr_fc;
      params->internal_noise[band] =
        pow (10., 0.4 * 0.364 * pow (curr_fc / 1000., -0.8));
      params->excitation_threshold[band] =
        pow (10, 0.364 * pow (curr_fc / 1000, -0.8));
      params->threshold[band] =
        pow (10,
             0.1 * (-2 - 2.05 * atan (curr_fc / 4000) -
                    0.75 * atan (curr_fc / 1600 * curr_fc / 1600)));
      params->loudness_factor[band] =
        PEAQ_EARMODELPARAMS_GET_CLASS (params)->loudness_scale *
        pow (params->excitation_threshold[band] /
             (1e4 * params->threshold[band]), 0.23);
    }

    update_ear_time_constants (params);
  }
}

guint
peaq_earmodelparams_get_step_size (PeaqEarModelParams const *params)
{
  return PEAQ_EARMODELPARAMS_GET_CLASS (params)->step_size;
}

gdouble
peaq_earmodelparams_get_band_center_frequency (PeaqEarModelParams const
                                               *params, guint band)
{
  return params->fc[band];
}

gdouble
peaq_earmodelparams_get_internal_noise (PeaqEarModelParams const *params,
                                        guint band)
{
  return params->internal_noise[band];
}

gdouble
peaq_earmodelparams_get_ear_time_constant (PeaqEarModelParams const *params,
                                           guint band)
{
  return params->ear_time_constants[band];
}

gdouble
peaq_earmodelparams_calc_loudness (PeaqEarModelParams const *params,
                                   gdouble * excitation)
{
  guint i;
  gdouble overall_loudness = 0.;
  for (i = 0; i < params->band_count; i++) {
    gdouble loudness = params->loudness_factor[i]
      * (pow (1. - params->threshold[i] +
              params->threshold[i] * excitation[i] /
              params->excitation_threshold[i], 0.23) - 1.);
    overall_loudness += MAX (loudness, 0.);
  }
  overall_loudness *= 24. / params->band_count;
  return overall_loudness;
}

static void
update_ear_time_constants(PeaqEarModelParams *params)
{
  guint band;
  PeaqEarModelParamsClass *params_class =
    PEAQ_EARMODELPARAMS_GET_CLASS (params);
  guint step_size = params_class->step_size;
  gdouble tau_min = params_class->tau_min;
  gdouble tau_100 = params_class->tau_100;

  for (band = 0; band < params->band_count; band++) {
    gdouble tau = tau_min + 100. / params->fc[band] * (tau_100 - tau_min);
    params->ear_time_constants[band] = exp (step_size / (-48000 * tau));
  }
}

static void
peaq_earmodelparams_get_property (GObject *obj, guint id, GValue *value,
                                  GParamSpec *pspec)
{
  PeaqEarModelParams *params = PEAQ_EARMODELPARAMS (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      g_value_set_double (value,
                          PEAQ_EARMODELPARAMS_GET_CLASS
                          (obj)->get_playback_level (params));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
      break;
  }
}

/*
 * peaq_earmodel_set_property:
 * @obj: the object structure.
 * @id: the id of the property to be set.
 * @value: the value to set the property to.
 * @pspec: the #GParamSpec of the property to be set.
 *
 * Sets the property specified by @id to the given @value.
 */
static void
peaq_earmodelparams_set_property (GObject *obj, guint id,
                                  const GValue *value, GParamSpec *pspec)
{
  PeaqEarModelParams *params = PEAQ_EARMODELPARAMS (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      PEAQ_EARMODELPARAMS_GET_CLASS (obj)->set_playback_level (params,
                                                               g_value_get_double
                                                               (value));
      break;
    case PROP_BAND_CENTER_FREQUENCIES:
      {
        GArray *fc_array;
        fc_array = g_value_get_pointer (value);
        if (fc_array) {
          params_set_bands (params, (gdouble *) fc_array->data, fc_array->len);
        } else {
          params_set_bands (params, NULL, 0);
        }
        break;
      }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
      break;
  }
}

GType
peaq_earmodel_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqEarModelClass),       /* class_size */
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      peaq_earmodel_class_init, /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof (PeaqEarModel),    /* instance_size */
      0,                        /* n_preallocs */
      peaq_earmodel_init        /* instance_init */
    };
    type = g_type_register_static (G_TYPE_OBJECT,
                                   "GstPeaqEarModel", &info, 0);
  }
  return type;
}

static void
peaq_earmodel_class_init (gpointer klass, gpointer class_data)
{
}

static void
peaq_earmodel_init (GTypeInstance *obj, gpointer klass)
{
}

PeaqEarModelParams *
peaq_earmodel_get_model_params (PeaqEarModel const *model)
{
  return model->params;
}
