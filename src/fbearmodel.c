/* GstPEAQ
 * Copyright (C) 2013 Martin Holters <martin.holters@hsuhh.de>
 *
 * fbearmodel.c: Filter bank-based peripheral ear model part.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fbearmodel.h"
#include "gstpeaq.h"

#include <math.h>
#include <string.h>

#define BUFFER_LENGTH 1457

const guint filter_length[40] = {
  1456,
  1438,
  1406,
  1362,
  1308,
  1244,
  1176,
  1104,
  1030,
  956,
  884,
  814,
  748,
  686,
  626,
  570,
  520,
  472,
  430,
  390,
  354,
  320,
  290,
  262,
  238,
  214,
  194,
  176,
  158,
  144,
  130,
  118,
  106,
  96,
  86,
  78,
  70,
  64,
  58,
  52
};

enum
{
  PROP_0,
  PROP_PLAYBACK_LEVEL
};

struct _PeaqFilterbankEarModelParams
{
  PeaqEarModelParams parent;
  gdouble level_factor;
};

struct _PeaqFilterbankEarModelParamsClass
{
  PeaqEarModelParamsClass parent;
};

/**
 * PeaqFilterbankEarModel:
 *
 * The opaque PeaqFilterbankEarModel structure.
 */
struct _PeaqFilterbankEarModel
{
  PeaqEarModel parent;

  gdouble *fbh_re[40];
  gdouble *fbh_im[40];

  gdouble hpfilter1_x1;
  gdouble hpfilter1_x2;
  gdouble hpfilter1_y1;
  gdouble hpfilter1_y2;
  gdouble hpfilter2_x1;
  gdouble hpfilter2_x2;
  gdouble hpfilter2_y1;
  gdouble hpfilter2_y2;
  gdouble fb_buf[BUFFER_LENGTH];
  gdouble cu[BUFFER_LENGTH];
  gdouble *E0_buf[40];
  gdouble excitation[40];
};

/**
 * PeaqFilterbankEarModelClass:
 *
 * The opaque PeaqFilterbankEarModelClass structure.
 */
struct _PeaqFilterbankEarModelClass
{
  PeaqEarModelClass parent;
};


static void params_class_init (gpointer klass, gpointer class_data);
static void params_init (GTypeInstance *obj, gpointer klass);
static gdouble params_get_playback_level (PeaqEarModelParams const *params);
static void params_set_playback_level (PeaqEarModelParams *params,
                                       double level);
static gdouble params_get_band_center_frequency (PeaqEarModelParams const
                                                 *params, guint band);
static gdouble params_get_internal_noise (PeaqEarModelParams const *params,
                                          guint band);

static void peaq_filterbankearmodel_class_init (gpointer klass,
                                                gpointer class_data);
static void peaq_filterbankearmodel_init (GTypeInstance *obj, gpointer klass);
static void peaq_filterbankearmodel_finalize (GObject *obj);

GType
peaq_filterbankearmodelparams_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqFilterbankEarModelParamsClass),       /* class_size */
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      params_class_init,        /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof (PeaqFilterbankEarModelParams),    /* instance_size */
      0,                        /* n_preallocs */
      params_init               /* instance_init */
    };
    type = g_type_register_static (PEAQ_TYPE_EARMODELPARAMS,
                                   "PeaqFilterbankEarModelParams", &info, 0);
  }
  return type;
}

static void
params_class_init (gpointer klass, gpointer class_data)
{
  PeaqEarModelParamsClass *ear_params_class =
    PEAQ_EARMODELPARAMS_CLASS (klass);

  ear_params_class->get_playback_level = params_get_playback_level;
  ear_params_class->set_playback_level = params_set_playback_level;
  ear_params_class->get_band_center_frequency =
    params_get_band_center_frequency;
  ear_params_class->get_internal_noise = params_get_internal_noise;

  ear_params_class->loudness_scale = 1.26539;
}

static void
params_init (GTypeInstance *obj, gpointer klass)
{
  PeaqEarModelParams *params = PEAQ_EARMODELPARAMS (obj);

  peaq_earmodelparams_set_band_count (PEAQ_EARMODELPARAMS (obj), 40);
  params->step_size = 192;
}

static gdouble
params_get_playback_level (PeaqEarModelParams const *params)
{
  return 20. * log10 (PEAQ_FILTERBANKEARMODELPARAMS (params)->level_factor);
}

static void
params_set_playback_level (PeaqEarModelParams *params, double level)
{
  PEAQ_FILTERBANKEARMODELPARAMS (params)->level_factor =
    pow (10., level / 20.);
}

static gdouble
params_get_band_center_frequency (PeaqEarModelParams const *params,
                                  guint band)
{
  return
    sinh ((asinh (50. / 650.) +
           band * (asinh (18000. / 650.) - asinh (50. / 650.)) / 39.)) * 650.;
}

static gdouble
params_get_internal_noise (PeaqEarModelParams const *params, guint band)
{
  gdouble fc =
    sinh ((asinh (50. / 650.) +
           band * (asinh (18000. / 650.) - asinh (50. / 650.)) / 39.)) * 650.;
  return pow (10., 0.4 * 0.364 * pow (fc / 1000., -0.8));
}


GType
peaq_filterbankearmodel_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqFilterbankEarModelClass),     /* class_size */
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      peaq_filterbankearmodel_class_init,       /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof (PeaqFilterbankEarModel),  /* instance_size */
      0,                        /* n_preallocs */
      peaq_filterbankearmodel_init      /* instance_init */
    };
    type = g_type_register_static (PEAQ_TYPE_EARMODEL,
                                   "PeaqFilterbankEarModel", &info, 0);
  }
  return type;
}

static void
peaq_filterbankearmodel_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  /* override finalize method */
  object_class->finalize = peaq_filterbankearmodel_finalize;
}

static void
peaq_filterbankearmodel_init (GTypeInstance *obj, gpointer klass)
{
  guint band;

  PeaqFilterbankEarModel *ear = PEAQ_FILTERBANKEARMODEL (obj);

  ear->parent.params =
    g_object_new (PEAQ_TYPE_FILTERBANKEARMODELPARAMS, NULL);

  for (band = 0; band < 40; band++) {
    guint n;
    gdouble fc =
      sinh ((asinh (50. / 650.) +
             band * (asinh (18000. / 650.) -
                     asinh (50. / 650.)) / 39.)) * 650.;
    guint N = filter_length[band];
    /* include outer and middle ear filtering in filter bank coefficients */
    gdouble W =
      -0.6 * 3.64 * pow (fc / 1000., -0.8) +
      6.5 * exp (-6.5 * pow (fc / 1000.0 - 3.3, 2)) -
      1e-3 * pow (fc / 1000., 3.6);
    gdouble Wt = pow (10., W / 20.);
    ear->fbh_re[band] = g_new (gdouble, N);
    ear->fbh_im[band] = g_new (gdouble, N);
    for (n = 0; n < N; n++) {
      gdouble win = 4. / N * sin (M_PI * n / N) * sin (M_PI * n / N) * Wt;
      ear->fbh_re[band][n] =
        win * cos (2 * M_PI * fc * (n - N / 2.) / 48000.);
      ear->fbh_im[band][n] =
        win * sin (2 * M_PI * fc * (n - N / 2.) / 48000.);
    }
    ear->E0_buf[band] = g_new0 (gdouble, 12);
  }

  ear->hpfilter1_x1 = 0.;
  ear->hpfilter1_x2 = 0.;
  ear->hpfilter1_y1 = 0.;
  ear->hpfilter1_y2 = 0.;
  ear->hpfilter2_x1 = 0.;
  ear->hpfilter2_x2 = 0.;
  ear->hpfilter2_y1 = 0.;
  ear->hpfilter2_y2 = 0.;
  memset (ear->fb_buf, 0, BUFFER_LENGTH * sizeof (gdouble));
  memset (ear->cu, 0, BUFFER_LENGTH * sizeof (gdouble));
}

static void
peaq_filterbankearmodel_finalize (GObject *obj)
{
  guint band;
  PeaqFilterbankEarModel *ear = PEAQ_FILTERBANKEARMODEL (obj);

  GObjectClass *parent_class =
    G_OBJECT_CLASS (g_type_class_peek_parent (g_type_class_peek
                                              (PEAQ_TYPE_FILTERBANKEARMODEL)));
  for (band = 0; band < 40; band++) {
    g_free (ear->fbh_re[band]);
    g_free (ear->fbh_im[band]);
  }

  parent_class->finalize (obj);
}

void
peaq_filterbankearmodel_process (PeaqFilterbankEarModel *ear,
                                 gfloat *sample_data, EarModelOutput *output)
{
  guint k;
  guint band;

  for (k = 0; k < FB_FRAMESIZE; k++) {
    /* 2.2.3 Setting of playback level */
    gdouble scaled_input =
      sample_data[k] *
      PEAQ_FILTERBANKEARMODELPARAMS (ear->parent.params)->level_factor;

    /* 2.2.4 DC rejection filter */
    gdouble hpfilter1_out
      = scaled_input - 2. * ear->hpfilter1_x1 + ear->hpfilter1_x2
      + 1.99517 * ear->hpfilter1_y1 - 0.995174 * ear->hpfilter1_y2;
    ear->hpfilter1_x2 = ear->hpfilter1_x1;
    ear->hpfilter1_x1 = scaled_input;
    ear->hpfilter1_y2 = ear->hpfilter1_y1;
    ear->hpfilter1_y1 = hpfilter1_out;
    gdouble hpfilter2_out
      = hpfilter1_out - 2. * ear->hpfilter2_x1 + ear->hpfilter2_x2
      + 1.99517 * ear->hpfilter2_y1 - 0.995174 * ear->hpfilter2_y2;
    ear->hpfilter2_x2 = ear->hpfilter2_x1;
    ear->hpfilter2_x1 = hpfilter1_out;
    ear->hpfilter2_y2 = ear->hpfilter2_y1;
    ear->hpfilter2_y1 = hpfilter2_out;

    /* 2.2.5 Filter bank; 2.2.6 Outer and middle ear filtering */
    memmove (ear->fb_buf + 1, ear->fb_buf,
             (BUFFER_LENGTH - 1) * sizeof (gdouble));
    ear->fb_buf[0] = hpfilter2_out;
    if (k % 32 == 0) {
      gdouble fb_out_re[40];
      gdouble fb_out_im[40];
      for (band = 0; band < 40; band++) {
        guint n;
        guint D = 1 + (filter_length[0] - filter_length[band]) / 2;
        fb_out_re[band] = 0.;
        fb_out_im[band] = 0.;
        for (n = 0; n < filter_length[band]; n++) {
          fb_out_re[band] += ear->fb_buf[D + n] * ear->fbh_re[band][n];
          fb_out_im[band] += ear->fb_buf[D + n] * ear->fbh_im[band][n];
        }
      }

      /* 2.2.7 Frequency domain spreading */
      gdouble fc =
        sinh ((asinh (50. / 650.) +
               band * (asinh (18000. / 650.) -
                       asinh (50. / 650.)) / 39.)) * 650.;
      gdouble dist = pow (0.1,
                          7. * (asinh (18000. / 650.) -
                                asinh (50. / 650.)) / (39. * 20.));
      gdouble a = exp (-32 / (48000 * 0.1));
      gdouble b = 1 - a;
      gdouble A_re[40];
      gdouble A_im[40];
      for (band = 0; band < 40; band++) {
        A_re[band] = fb_out_re[band];
        A_im[band] = fb_out_im[band];
      }
      for (band = 0; band < 40; band++) {
        gdouble L =
          10 * log10 (fb_out_re[band] * fb_out_re[band] +
                      fb_out_im[band] * fb_out_im[band]);
        gdouble s = MAX (4, 24 + 230 / fc - 0.2 * L);
        /* a and b are probably swapped in the standard's pseudo code */
        // ear->cu[band] = a * pow(dist, s) + b * ear->cu[band];
        ear->cu[band] = b * pow (dist, s) + a * ear->cu[band];
        gdouble d1 = fb_out_re[band];
        gdouble d2 = fb_out_im[band];
        guint j;
        for (j = band + 1; j < 40; j++) {
          d1 *= ear->cu[band];
          d2 *= ear->cu[band];
          A_re[j] += d1;
          A_im[j] += d2;
        }
      }

      gdouble cl = pow (dist, 31);
      gdouble d1 = 0.;
      gdouble d2 = 0.;
      for (band = 0; band < 40; band++) {
        d1 = d1 * cl + A_re[band];
        d2 = d2 * cl + A_im[band];
        A_re[band] = d1;
        A_im[band] = d2;
      }

      /* 2.2.8. Rectification */
      gdouble E0[40];
      for (band = 0; band < 40; band++) {
        E0[band] = A_re[band] * A_re[band] + A_im[band] * A_im[band];
      }

      /* 2.2.9 Time domain smearing (1) - Backward masking */
      for (band = 0; band < 40; band++) {
        memmove (ear->E0_buf[band] + 1, ear->E0_buf[band],
                 11 * sizeof (gdouble));
        ear->E0_buf[band][0] = E0[band];
      }
    }
  }
  gdouble E1[40];
  for (band = 0; band < 40; band++) {
    guint i;
    E1[band] = 0.;
    for (i = 0; i < 12; i++) {
      E1[band] +=
        ear->E0_buf[band][i] * cos (M_PI * (i - 5.) / 12.) *
        cos (M_PI * (i - 5.) / 12.);
    }
    E1[band] *= 0.9761 / 6;

    /* 2.2.10 Adding of internal noise */
    gdouble fc =
      sinh ((asinh (50. / 650.) +
             band * (asinh (18000. / 650.) -
                     asinh (50. / 650.)) / 39.)) * 650.;
    gdouble EThres = pow (10., 0.4 * 0.364 * pow (fc / 1000., -0.8));
    output->unsmeared_excitation[band] = E1[band] + EThres;

    /* 2.2.11 Time domain smearing (2) - Forward masking */
    gdouble tau = 0.004 + 100. / fc * (0.02 - 0.004);
    gdouble a = exp (-192. / (48000. * tau));

    ear->excitation[band] =
      a * ear->excitation[band] +
      (1. - a) * output->unsmeared_excitation[band];
    output->excitation[band] = ear->excitation[band];
  }

  output->overall_loudness =
    peaq_earmodelparams_calc_loudness (PEAQ_EARMODELPARAMS
                                       (ear->parent.params),
                                       output->excitation);
}
