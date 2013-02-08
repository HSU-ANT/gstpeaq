/* GstPEAQ
 * Copyright (C) 2006, 2007, 2011, 2012, 2013
 * Martin Holters <martin.holters@hsuhh.de>
 *
 * fftearmodel.c: FFT-based peripheral ear model part.
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

#include "fftearmodel.h"
#include "gstpeaq.h"

#include <math.h>

#define GAMMA 0.84971762641205
#define LOUDNESS_SCALE 1.07664

enum
{
  PROP_0,
  PROP_BAND_COUNT
};

/**
 * PeaqEarModel:
 *
 * The opaque PeaqEarModel structure.
 */
struct _PeaqFFTEarModelParams
{
  PeaqEarModelParams parent;
  gdouble deltaZ;
  gdouble level_factor;
  guint *band_lower_end;
  guint *band_upper_end;
  gdouble *band_lower_weight;
  gdouble *band_upper_weight;
  gdouble lower_spreading;
  gdouble lower_spreading_exponantiated;
  gdouble *spreading_normalization;
  gdouble *aUC;
  gdouble *gIL;
};

/**
 * PeaqEarModelClass:
 *
 * The opaque PeaqEarModelClass structure.
 */
struct _PeaqFFTEarModelParamsClass
{
  PeaqEarModelParamsClass parent;
  gdouble *hann_window;
  gdouble *outer_middle_ear_weight;
};

/**
 * PeaqEar:
 *
 * The opaque PeaqEar structure.
 */
struct _PeaqFFTEarModel
{
  PeaqEarModel parent;
  GstFFTF64 *gstfft;
  gdouble *filtered_excitation;
};

/**
 * PeaqEarClass:
 *
 * The opaque PeaqEarClass structure.
 */
struct _PeaqFFTEarModelClass
{
  PeaqEarModelClass parent;
};

static void params_class_init (gpointer klass, gpointer class_data);
static void params_init (GTypeInstance *obj, gpointer klass);
static void params_finalize (GObject *obj);
static void params_fill (PeaqFFTEarModelParams *params);
static gdouble params_get_playback_level (PeaqEarModelParams const *params);
static void params_set_playback_level (PeaqEarModelParams *params,
                                       double level);
static void params_do_spreading (PeaqFFTEarModelParams const *params,
                                 gdouble *Pp, gdouble *E2);

static void peaq_ear_class_init (gpointer klass, gpointer class_data);
static void peaq_ear_init (GTypeInstance *obj, gpointer klass);
static void peaq_ear_finalize (GObject *obj);
static void peaq_ear_get_property (GObject *obj, guint id, GValue *value,
                                   GParamSpec *pspec);
static void peaq_ear_set_property (GObject *obj, guint id, const GValue *value,
                                   GParamSpec *pspec);

/*
 * peaq_fftearmodelparams_get_type:
 *
 * Registers the type GstPeaqFFTEarModelParams if not already done so and
 * returns the respective #GType.
 *
 * Returns: the type for GstPeaqFFTEarModelParams.
 *
 * TODO: add a class_finalize function to free all the memory allocated in 
 * class_init. Or should all this be done by base_init/base_finalize?
 */
GType
peaq_fftearmodelparams_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqFFTEarModelParamsClass),      /* class_size */
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      params_class_init,        /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof (PeaqFFTEarModelParams),   /* instance_size */
      0,                        /* n_preallocs */
      params_init               /* instance_init */
    };
    type = g_type_register_static (PEAQ_TYPE_EARMODELPARAMS,
                                   "GstPeaqFFTEarModelParams", &info, 0);
  }
  return type;
}

/*
 * params_class_init:
 * @klass: pointer to the uninitialized class structure.
 * @class_data: pointer to data specified when registering the class (unused).
 *
 * Sets up the class data which mainly involves pre-computing helper data.
 */
static void
params_class_init (gpointer klass, gpointer class_data)
{
  guint k;
  guint N = FFT_FRAMESIZE;
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PeaqEarModelParamsClass *ear_params_class =
    PEAQ_EARMODELPARAMS_CLASS (klass);
  PeaqFFTEarModelParamsClass *fft_params_class =
    PEAQ_FFTEARMODELPARAMS_CLASS (klass);

  /* override finalize method */
  object_class->finalize = params_finalize;

  ear_params_class->get_playback_level = params_get_playback_level;
  ear_params_class->set_playback_level = params_set_playback_level;

  ear_params_class->loudness_scale = LOUDNESS_SCALE;
  ear_params_class->step_size = 1024;
  ear_params_class->tau_min = 0.008;
  ear_params_class->tau_100 = 0.030;

  /* pre-compute Hann window
   *
   * Compared to the BS.1387-1, this lacks the factor sqrt(8./3.) which is
   * subsumed in the GAMMA as defined in Kabal03. Note that Kabal03 erraneously
   * includes sqrt(8./3.) in (5).
   */
  fft_params_class->hann_window = g_new (gdouble, N);
  for (k = 0; k < N; k++) {
    fft_params_class->hann_window[k] =
      0.5 * (1. - cos (2 * M_PI * k / (N - 1)));
  }

  /* pre-compute weighting coefficients for outer and middle ear weighting 
   * function */
  fft_params_class->outer_middle_ear_weight = g_new (gdouble, N / 2 + 1);
  for (k = 0; k <= N / 2; k++) {
    gdouble f_kHz = (gdouble) k * SAMPLINGRATE / N / 1000;
    gdouble W_dB =
      (-0.6 * 3.64 * pow (f_kHz, -0.8)) +
      (6.5 * exp (-0.6 * pow (f_kHz - 3.3, 2))) - (1e-3 * pow (f_kHz, 3.6));
    fft_params_class->outer_middle_ear_weight[k] = pow (10, W_dB / 10);
  }
}

/*
 * params_init:
 * @obj: Pointer to the unitialized #PeaqEarModel structure.
 * @klass: The class structure of the class being instantiated.
 *
 * Initializes one instance of #PeaqEarModel, in particular, the state
 * variables for the time smearing are allocated and initialized to zero.
 */
static void
params_init (GTypeInstance *obj, gpointer klass)
{
  PeaqEarModelParams *params = PEAQ_EARMODELPARAMS (obj);
  g_signal_connect (params, "notify::band-centers",
                    G_CALLBACK (params_fill), NULL);
}

static void
params_fill (PeaqFFTEarModelParams *params)
{
  guint k;
  gdouble *spread;
  guint band_count = params->parent.band_count;

  params->deltaZ = 27. / (band_count - 1);

  /* pre-compute helper data for params_group_into_bands
   * The precomputed data is as proposed in [Kabal03], but the algorithm to
   * compute is somewhat simplified */
  params->band_lower_end = g_new (guint, band_count);
  params->band_upper_end = g_new (guint, band_count);
  params->band_lower_weight = g_new (gdouble, band_count);
  params->band_upper_weight = g_new (gdouble, band_count);
  gdouble zL = 7. * asinh (80. / 650.);
  gdouble zU = 7. * asinh (18000. / 650.);
  for (k = 0; k < band_count; k++) {
    gdouble zl = zL + k * params->deltaZ;
    gdouble zu = MIN(zU, zL + (k + 1) * params->deltaZ);
    gdouble fl = 650. * sinh (zl / 7.);
    gdouble fu = 650. * sinh (zu / 7.);
    params->band_lower_end[k]
      = (guint) round (fl / SAMPLINGRATE * FFT_FRAMESIZE);
    params->band_upper_end[k]
      = (guint) round (fu / SAMPLINGRATE * FFT_FRAMESIZE);
    gdouble upper_freq =
      (2 * params->band_lower_end[k] + 1) / 2. * SAMPLINGRATE / FFT_FRAMESIZE;
    gdouble U = upper_freq - fl;
    params->band_lower_weight[k] = U * FFT_FRAMESIZE / SAMPLINGRATE;
    if (params->band_lower_end[k] == params->band_upper_end[k]) {
      params->band_upper_weight[k] = 0;
    } else {
      gdouble lower_freq = (2 * params->band_upper_end[k] - 1) / 2.
        * SAMPLINGRATE / FFT_FRAMESIZE;
      U = fu - lower_freq;
      params->band_upper_weight[k] = U * FFT_FRAMESIZE / SAMPLINGRATE;
    }
  }

  /* pre-compute internal noise, time constants for time smearing, thresholds 
   * and helper data for spreading */
  params->spreading_normalization =
    g_new (gdouble, band_count);
  params->aUC = g_new (gdouble, band_count);
  params->gIL = g_new (gdouble, band_count);
  params->lower_spreading = pow (10, -2.7 * params->deltaZ);
  params->lower_spreading_exponantiated = pow (params->lower_spreading, 0.4);
  for (k = 0; k < band_count; k++) {
    gdouble curr_fc =
      peaq_earmodelparams_get_band_center_frequency (PEAQ_EARMODELPARAMS
                                                     (params), k);
    const gdouble aL = params->lower_spreading;
    params->aUC[k] = pow (10, (-2.4 - 23 / curr_fc) * params->deltaZ);
    params->gIL[k] = (1 - pow (aL, k + 1)) / (1 - aL);
    params->spreading_normalization[k] = 1.;
  }
  spread = g_newa (gdouble, band_count);
  params_do_spreading (params, params->spreading_normalization, spread);
  for (k = 0; k < band_count; k++)
    params->spreading_normalization[k] = spread[k];
}

/*
 * params_finalize:
 * @obj: Pointer to the #PeaqEarModel to be finalized.
 *
 * Disposes the given instance of #PeaqEarModel, in particular, the state 
 * variables for the time smearing are deallocated.
 */
static void
params_finalize (GObject *obj)
{
  PeaqFFTEarModelParams *params = PEAQ_FFTEARMODELPARAMS (obj);
  GObjectClass *parent_class =
    G_OBJECT_CLASS (g_type_class_peek_parent (g_type_class_peek
                                              (PEAQ_TYPE_FFTEARMODELPARAMS)));
  g_free (params->band_lower_end);
  g_free (params->band_upper_end);
  g_free (params->band_lower_weight);
  g_free (params->band_upper_weight);
  g_free (params->spreading_normalization);
  g_free (params->aUC);
  g_free (params->gIL);

  parent_class->finalize (obj);
}

/*
 * params_get_property:
 * @obj: the object structure.
 * @id: the id of the property queried.
 * @value: the value to be filled in.
 * @pspec: the #GParamSpec of the property queried.
 *
 * Fills the given @value with the value currently set for the property 
 * specified by @id.
 */
static gdouble
params_get_playback_level (PeaqEarModelParams const *params)
{
  PeaqFFTEarModelParams *fftparams = PEAQ_FFTEARMODELPARAMS (params);
  return 10. * log10 (fftparams->level_factor *
                      (GAMMA / 4 * (FFT_FRAMESIZE - 1) / FFT_FRAMESIZE));
}

static void
params_set_playback_level (PeaqEarModelParams *params, gdouble level)
{
  PeaqFFTEarModelParams *fftparams = PEAQ_FFTEARMODELPARAMS (params);
  /* ear_model->level_factor is the square of G_Li/N_F in [Kabal03], which
   * equals fac/N in [BS1387] except for a factor of sqrt(8/3) which is part of
   * the Hann window in [BS1387] */
  fftparams->level_factor = pow (10, level / 10) /
    ((GAMMA / 4 * (FFT_FRAMESIZE - 1)) * (GAMMA / 4 * (FFT_FRAMESIZE - 1)));
}

/**
 * peaq_ear_process:
 * @ear: the #PeaqEar instance structure.
 * @sample_data: pointer to a frame of #FFT_FRAMESIZE samples to be processed.
 * @output: pointer to a #EarModelOutput structure which is filled with the 
 * computed output data.
 *
 * Performs the computation described in section 2 of 
 * <xref linkend="Kabal03" /> for one single frame. The input is assumed to be 
 * sampled at 48 kHz. To follow the specification, the frames of successive 
 * invocations of peaq_ear_process() have to overlap by 50%.
 *
 * The first step is to apply a Hann window to the input frame and transform 
 * it to the frequency domain using FFT. The squared magnitude 
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <mi>X</mi>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * of the frequency coefficients up to half the frame length are stored in 
 * <structfield>power_spectrum</structfield> of @output.
 */
void
peaq_fftearmodel_process (PeaqFFTEarModel *ear, gfloat *sample_data,
                          EarModelOutput *output)
{
  guint k, i;
  PeaqFFTEarModelParams *params = PEAQ_FFTEARMODELPARAMS (ear->parent.params);
  PeaqFFTEarModelParamsClass *fft_params_class =
    PEAQ_FFTEARMODELPARAMS_GET_CLASS (params);
  gdouble *windowed_data = g_newa (gdouble, FFT_FRAMESIZE);
  GstFFTF64Complex *fftoutput =
    g_newa (GstFFTF64Complex, FFT_FRAMESIZE / 2 + 1);
  gdouble *noisy_band_power =
    g_newa (gdouble, peaq_earmodelparams_get_band_count (ear->parent.params));

  g_assert (output);

  /* apply a Hann window to the input data frame */
  for (k = 0; k < FFT_FRAMESIZE; k++)
    windowed_data[k] = fft_params_class->hann_window[k] * sample_data[k];

  gst_fft_f64_fft (ear->gstfft, windowed_data, fftoutput);
  for (k = 0; k < FFT_FRAMESIZE / 2 + 1; k++) {
    output->power_spectrum[k] =
      (fftoutput[k].r * fftoutput[k].r + fftoutput[k].i * fftoutput[k].i) *
      params->level_factor;
    output->weighted_power_spectrum[k] =
      output->power_spectrum[k] *
      fft_params_class->outer_middle_ear_weight[k];
  }

  peaq_fftearmodelparams_group_into_bands (params,
                                           output->weighted_power_spectrum,
                                           output->band_power);

  for (i = 0; i < params->parent.band_count; i++)
    noisy_band_power[i] =
      output->band_power[i] +
      peaq_earmodelparams_get_internal_noise(PEAQ_EARMODELPARAMS (params), i);

  params_do_spreading (params, noisy_band_power,
                       output->unsmeared_excitation);

  /* NOTE: according to [BS1387], the filtered_excitation after processing the
   * first frame should be all zero; we follow the interpretation of [Kabal03]
   * and only initialize to zero before the first frame. */
  for (i = 0; i < params->parent.band_count; i++) {
    gdouble a =
      peaq_earmodelparams_get_ear_time_constant (PEAQ_EARMODELPARAMS (params),
                                                 i);
    ear->filtered_excitation[i] =
      a * ear->filtered_excitation[i] +
      (1 - a) * output->unsmeared_excitation[i];
    output->excitation[i] =
      ear->filtered_excitation[i] > output->unsmeared_excitation[i] ?
      ear->filtered_excitation[i] : output->unsmeared_excitation[i];
  }

  output->overall_loudness =
    peaq_earmodelparams_calc_loudness (PEAQ_EARMODELPARAMS (params),
                                       output->excitation);
}

/*
 * The grouping into bands follows the algorithm proposed in [Kabal03].
 */
void
peaq_fftearmodelparams_group_into_bands (PeaqFFTEarModelParams const *params,
                                         gdouble *spectrum,
                                         gdouble *band_power)
{
  guint i;
  guint k;
  for (i = 0; i < params->parent.band_count; i++) {
    band_power[i] =
      (params->band_lower_weight[i] *
       spectrum[params->band_lower_end[i]]) +
      (params->band_upper_weight[i] * spectrum[params->band_upper_end[i]]);
    for (k = params->band_lower_end[i] + 1;
         k < params->band_upper_end[i]; k++)
      band_power[i] += spectrum[k];
    if (band_power[i] < 1e-12)
      band_power[i] = 1e-12;
  }
}

/* this computation follows the algorithm in [Kabal03] where the
 * correspondances between variables in the code and in [Kabal[03] are as
 * follows:
 *
 *    code     | [Kabal03]
 *    ---------+----------
 *    aLe      | a_L^-0.4
 *    aUCE     | a_Ua_C[l]a_E(E)
 *    gIU      | (1-(a_Ua_C[l]a_E(E))^(N_c-l)) / (1-a_Ua_C[l]a_E(E))
 *    En       | E[l] / A(l,E)
 *    aUCEe    | (a_Ua_C[l]a_E(E))^0.4
 *    En       | (E[l] / A(l,E))^0.4
 *    E2       | Es[l]
 */
static void
params_do_spreading (PeaqFFTEarModelParams const *params, gdouble *Pp,
                     gdouble *E2)
{
  guint i;
  gdouble *aUCEe = g_newa (gdouble, params->parent.band_count);
  gdouble *Ene = g_newa (gdouble, params->parent.band_count);
  const gdouble aLe = params->lower_spreading_exponantiated;

  for (i = 0; i < params->parent.band_count; i++) {
    gdouble aUCE = params->aUC[i] * pow (Pp[i], 0.2 * params->deltaZ);
    gdouble gIU =
      (1 - pow (aUCE, params->parent.band_count - i)) / (1 - aUCE);
    gdouble En = Pp[i] / (params->gIL[i] + gIU - 1);
    aUCEe[i] = pow (aUCE, 0.4);
    Ene[i] = pow (En, 0.4);
  }
  E2[params->parent.band_count - 1] = Ene[params->parent.band_count - 1];
  for (i = params->parent.band_count - 1; i > 0; i--)
    E2[i - 1] = aLe * E2[i] + Ene[i - 1];
  for (i = 0; i < params->parent.band_count - 1; i++) {
    guint j;
    gdouble r = Ene[i];
    for (j = i + 1; j < params->parent.band_count; j++) {
      r *= aUCEe[i];
      E2[j] += r;
    }
  }
  for (i = 0; i < params->parent.band_count; i++) {
    E2[i] = pow (E2[i], 1 / 0.4) / params->spreading_normalization[i];
  }
}

GType
peaq_fftearmodel_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqFFTEarModelClass),    /* class_size */
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      peaq_ear_class_init,      /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof (PeaqFFTEarModel), /* instance_size */
      0,                        /* n_preallocs */
      peaq_ear_init             /* instance_init */
    };
    type = g_type_register_static (PEAQ_TYPE_EARMODEL,
                                   "GstPeaqFFTEarModel", &info, 0);
  }
  return type;
}

static void
peaq_ear_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  /* override finalize method */
  object_class->finalize = peaq_ear_finalize;
  object_class->set_property = peaq_ear_set_property;
  object_class->get_property = peaq_ear_get_property;
  g_object_class_install_property (object_class,
                                   PROP_BAND_COUNT,
                                   g_param_spec_uint ("number-of-bands",
                                                      "number of bands",
                                                      "Number of bands (55 or 109)",
                                                      55, 109, 109,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT));
}

static void
peaq_ear_init (GTypeInstance *obj, gpointer klass)
{
  PeaqFFTEarModel *ear = PEAQ_FFTEARMODEL (obj);
  ear->parent.params = NULL;

  ear->gstfft = gst_fft_f64_new (FFT_FRAMESIZE, FALSE);
  ear->filtered_excitation = NULL;
}

static void
peaq_ear_finalize (GObject *obj)
{
  PeaqFFTEarModel *ear = PEAQ_FFTEARMODEL (obj);
  GObjectClass *parent_class =
    G_OBJECT_CLASS (g_type_class_peek_parent (g_type_class_peek
                                              (PEAQ_TYPE_FFTEARMODEL)));

  g_free (ear->filtered_excitation);
  gst_fft_f64_free (ear->gstfft);

  parent_class->finalize (obj);
}

static void
peaq_ear_get_property (GObject *obj, guint id, GValue *value, GParamSpec *pspec)
{
  PeaqEarModel *model = PEAQ_EARMODEL (obj);
  switch (id) {
    case PROP_BAND_COUNT:
      g_value_set_uint (value,
                        peaq_earmodelparams_get_band_count
                        (peaq_earmodel_get_model_params (model)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
      break;
  }
}

static void
peaq_ear_set_property (GObject *obj, guint id, const GValue *value,
                       GParamSpec *pspec)
{
  PeaqFFTEarModel *ear = PEAQ_FFTEARMODEL (obj);
  switch (id) {
    case PROP_BAND_COUNT:
      {
        guint band;
        gdouble delta_z = 27. / (g_value_get_uint(value) - 1);
        gdouble zL = 7. * asinh (80. / 650.);
        gdouble zU = 7. * asinh (18000. / 650.);
        guint band_count = ceil ((zU - zL) / delta_z);
        g_assert (band_count == g_value_get_uint(value));
        GArray *fc_array = g_array_sized_new (FALSE, FALSE, sizeof (gdouble),
                                              band_count);
        for (band = 0; band < band_count; band++) {
          gdouble zl = zL + band * delta_z;
          gdouble zu = MIN(zU, zL + (band + 1) * delta_z);
          gdouble zc = (zu + zl) / 2.;
          gdouble curr_fc = 650. * sinh (zc / 7.);
          g_array_append_val (fc_array, curr_fc);
        }

        if (ear->parent.params)
          g_object_set (ear->parent.params, "band-centers", fc_array, NULL);
        else
          ear->parent.params = g_object_new (PEAQ_TYPE_FFTEARMODELPARAMS, 
                                             "band-centers", fc_array,
                                             NULL);
        if (ear->filtered_excitation)
          g_free (ear->filtered_excitation);
        ear->filtered_excitation = g_new0 (gdouble, band_count);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
      break;
  }
}

PeaqFFTEarModelParams *
peaq_fftearmodel_get_fftmodel_params (PeaqFFTEarModel const *ear)
{
  return PEAQ_FFTEARMODELPARAMS (ear->parent.params);
}
