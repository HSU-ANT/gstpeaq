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

/**
 * SECTION:fbearmodel
 * @short_description: Filter-bank based ear model.
 * @title: PeaqFilterbankEarModel
 *
 * The processing is performed by calling peaq_earmodel_process_block(). The
 * first step is to
 * apply a DC rejection filter (high pass at 20 Hz) and decompose the signal
 * into 40 bands using an FIR filter bank. After weighting the individual bands
 * with the outer and middle ear filter, the signal energy in spread accross
 * frequency and time. Addition of the internal noise then yields the unsmeared
 * excitation patterns. Another time domain spreading finally gives the
 * excitation patterns.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "settings.h"
#include "fbearmodel.h"
#include "gstpeaq.h"

#include <math.h>
#include <string.h>

#define FB_FRAMESIZE 192
#define SLOPE_FILTER_A 0.993355506255034     /* exp (-32 / (48000 * 0.1)) */
#define DIST 0.921851456499719               /* pow(0.1,(z[39]-z[0])/(39*20)) */
#define CL 0.0802581846102741                /* pow (DIST, 31) */
#define BUFFER_LENGTH 1456

typedef struct _PeaqFilterbankEarModelState PeaqFilterbankEarModelState;

/* taken from Table 8 in [BS1387] */
static const guint filter_length[40] = {
  1456, 1438, 1406, 1362, 1308, 1244, 1176, 1104, 1030, 956, 884, 814, 748,
  686, 626, 570, 520, 472, 430, 390, 354, 320, 290, 262, 238, 214, 194, 176,
  158, 144, 130, 118, 106, 96, 86, 78, 70, 64, 58, 52
};

enum
{
  PROP_0,
  PROP_PLAYBACK_LEVEL
};

/**
 * PeaqFilterbankEarModel:
 *
 * The opaque PeaqFilterbankEarModel structure.
 */
struct _PeaqFilterbankEarModel
{
  PeaqEarModel parent;
  gdouble level_factor;
  gdouble *fbh_re[40];
  gdouble *fbh_im[40];
};

/**
 * PeaqFilterbankEarModelClass:
 *
 * The opaque PeaqFilterbankEarModelClass structure.
 */
struct _PeaqFilterbankEarModelClass
{
  PeaqEarModelClass parent;
  gdouble back_mask_h[6];
};

struct _PeaqFilterbankEarModelState
{
  gdouble hpfilter1_x1;
  gdouble hpfilter1_x2;
  gdouble hpfilter1_y1;
  gdouble hpfilter1_y2;
  gdouble hpfilter2_y1;
  gdouble hpfilter2_y2;
  gdouble fb_buf[2 * BUFFER_LENGTH];
  guint fb_buf_offset;
  gdouble cu[BUFFER_LENGTH];
  gdouble *E0_buf[40];
  gdouble excitation[40];
  gdouble unsmeared_excitation[40];
};


static void class_init (gpointer klass, gpointer class_data);
static void init (GTypeInstance *obj, gpointer klass);
static void finalize (GObject *obj);
static gdouble get_playback_level (PeaqEarModel const *model);
static void set_playback_level (PeaqEarModel *model, double level);
static gpointer state_alloc (PeaqEarModel const *model);
static void state_free (PeaqEarModel const *model, gpointer state);
static void process_block (PeaqEarModel const *model, gpointer state,
                           gfloat const *sample_data);
static gdouble const *get_excitation (PeaqEarModel const *model,
                                      gpointer state);
static gdouble const *get_unsmeared_excitation (PeaqEarModel const *model,
                                                gpointer state);
static void apply_filter_bank (PeaqFilterbankEarModel *model,
                               PeaqFilterbankEarModelState *fb_state,
                               gdouble *fb_out_re, gdouble *fb_out_im);


GType
peaq_filterbankearmodel_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqFilterbankEarModelClass),       /* class_size */
      NULL,                                       /* base_init */
      NULL,                                       /* base_finalize */
      class_init,                                 /* class_init */
      NULL,                                       /* class_finalize */
      NULL,                                       /* class_data */
      sizeof (PeaqFilterbankEarModel),            /* instance_size */
      0,                                          /* n_preallocs */
      init                                        /* instance_init */
    };
    type = g_type_register_static (PEAQ_TYPE_EARMODEL,
                                   "PeaqFilterbankEarModel", &info, 0);
  }
  return type;
}

static void
class_init (gpointer klass, gpointer class_data)
{
  guint i;

  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PeaqEarModelClass *ear_model_class =
    PEAQ_EARMODEL_CLASS (klass);
  PeaqFilterbankEarModelClass *fb_ear_model_class =
    PEAQ_FILTERBANKEARMODEL_CLASS (klass);

  /* override finalize method */
  object_class->finalize = finalize;

  ear_model_class->get_playback_level = get_playback_level;
  ear_model_class->set_playback_level = set_playback_level;
  ear_model_class->state_alloc = state_alloc;
  ear_model_class->state_free = state_free;
  ear_model_class->process_block = process_block;
  ear_model_class->get_excitation = get_excitation;
  ear_model_class->get_unsmeared_excitation = get_unsmeared_excitation;
  ear_model_class->frame_size = FB_FRAMESIZE;
  ear_model_class->step_size = FB_FRAMESIZE;
  /* see section 3.3 in [BS1387], section 4.3 in [Kabal03] */
  ear_model_class->loudness_scale = 1.26539;
  /* see section 2.2.11 in [BS1387], section 3.7 in [Kabal03] */
  ear_model_class->tau_min = 0.004;
  ear_model_class->tau_100 = 0.020;

  /* precompute coefficients of the backward masking filter, see section 2.2.9
   * in [BS1387] and section 3.5 in [Kabal03]; due to symmetry, storing the
   * first six coefficients is sufficient */
  for (i = 0; i < 6; i++) {
    fb_ear_model_class->back_mask_h[i] =
      cos (M_PI * (i - 5.) / 12.) * cos (M_PI * (i - 5.) / 12.) * 0.9761 / 6.;
  }
}

static void
init (GTypeInstance *obj, gpointer klass)
{
  guint band;
  PeaqFilterbankEarModel *model = PEAQ_FILTERBANKEARMODEL (obj);

  GArray *fc_array = g_array_sized_new (FALSE, FALSE, sizeof (gdouble), 40);

  /* precompute filter bank impulse responses */
  for (band = 0; band < 40; band++) {
    guint n;
    /* use (36) and (37) from [Kabal03] to determine the center frequencies
     * instead of the tabulated values from [BS1387] */
    gdouble fc =
      sinh ((asinh (50. / 650.) +
             band * (asinh (18000. / 650.) -
                     asinh (50. / 650.)) / 39.)) * 650.;
    g_array_append_val (fc_array, fc);
    guint N = filter_length[band];
    /* include outer and middle ear filtering in filter bank coefficients */
    gdouble Wt = peaq_earmodel_calc_ear_weight (fc);
    /* due to symmetry, it is sufficient to compute the first half of the
     * coefficients */
    model->fbh_re[band] = g_new (gdouble, N / 2 + 1);
    model->fbh_im[band] = g_new (gdouble, N / 2 + 1);
    for (n = 0; n < N / 2 + 1; n++) {
      /* (29) in [BS1387], (39) and (38) in [Kabal03] */
      gdouble win = 4. / N * sin (M_PI * n / N) * sin (M_PI * n / N) * Wt;
      model->fbh_re[band][n] =
        win * cos (2 * M_PI * fc * (n - N / 2.) / 48000.);
      model->fbh_im[band][n] =
        win * sin (2 * M_PI * fc * (n - N / 2.) / 48000.);
    }
  }

  g_object_set (obj, "band-centers", fc_array, NULL);
  g_array_unref (fc_array);
}

static void
finalize (GObject *obj)
{
  PeaqFilterbankEarModel *model = PEAQ_FILTERBANKEARMODEL (obj);
  guint band;
  for (band = 0; band < 40; band++) {
    g_free (model->fbh_re[band]);
    g_free (model->fbh_im[band]);
  }
}

static gdouble
get_playback_level (PeaqEarModel const *model)
{
  return 20. * log10 (PEAQ_FILTERBANKEARMODEL (model)->level_factor);
}

static void
set_playback_level (PeaqEarModel *model, double level)
{
  /* scale factor for playback level; (27) in [BS1387], (34) in [Kabal03] */
  PEAQ_FILTERBANKEARMODEL (model)->level_factor =
    pow (10., level / 20.);
}

static
gpointer state_alloc (PeaqEarModel const *model)
{
  guint band;
  PeaqFilterbankEarModelState *state = g_new0 (PeaqFilterbankEarModelState, 1);
  for (band = 0; band < 40; band++)
    state->E0_buf[band] = g_new0 (gdouble, 11);
  return state;
}

static
void state_free (PeaqEarModel const *model, gpointer state)
{
  guint band;
  for (band = 0; band < 40; band++)
    g_free (((PeaqFilterbankEarModelState *) state)->E0_buf[band]);
  g_free (state);
}

static void
process_block (PeaqEarModel const *model, gpointer state,
               gfloat const *sample_data)
{
  guint k;
  guint band;
  gdouble level_factor =
    PEAQ_FILTERBANKEARMODEL (model)->level_factor;
  PeaqFilterbankEarModelClass *fb_ear_model_class =
    PEAQ_FILTERBANKEARMODEL_GET_CLASS (model);
  PeaqFilterbankEarModelState *fb_state = (PeaqFilterbankEarModelState *) state;

  for (k = 0; k < FB_FRAMESIZE; k++) {
    /* setting of playback level; 2.2.3 in [BS1387], 3 in [Kabal03] */
    gdouble scaled_input = sample_data[k] * level_factor;

    /* DC rejection filter; 2.2.4 in [BS1387], 3.1 in [Kabal03] */
    gdouble hpfilter1_out
      = scaled_input - 2. * fb_state->hpfilter1_x1 + fb_state->hpfilter1_x2
      + 1.99517 * fb_state->hpfilter1_y1 - 0.995174 * fb_state->hpfilter1_y2;
    gdouble hpfilter2_out
      = hpfilter1_out - 2. * fb_state->hpfilter1_y1 + fb_state->hpfilter1_y2
      + 1.99799 * fb_state->hpfilter2_y1 - 0.997998 * fb_state->hpfilter2_y2;
    fb_state->hpfilter1_x2 = fb_state->hpfilter1_x1;
    fb_state->hpfilter1_x1 = scaled_input;
    fb_state->hpfilter1_y2 = fb_state->hpfilter1_y1;
    fb_state->hpfilter1_y1 = hpfilter1_out;
    fb_state->hpfilter2_y2 = fb_state->hpfilter2_y1;
    fb_state->hpfilter2_y1 = hpfilter2_out;

    /* Filter bank; 2.2.5 in [BS1387], 3.2 in [Kabal03]; include outer and
     * middle ear filtering; 2.2.6 in [BS1387] 3.3 in [Kabal03] */
    if (fb_state->fb_buf_offset == 0)
      fb_state->fb_buf_offset = BUFFER_LENGTH;
    fb_state->fb_buf_offset--;
    /* filterbank input is stored twice s.t. starting at fb_buf_offset there
     * are always at least BUFFER_LENGTH samples of past data available */
    fb_state->fb_buf[fb_state->fb_buf_offset] = hpfilter2_out;
    fb_state->fb_buf[fb_state->fb_buf_offset + BUFFER_LENGTH] = hpfilter2_out;
    if (k % 32 == 0) {
      gdouble fb_out_re[40];
      gdouble fb_out_im[40];
      gdouble A_re[40];
      gdouble A_im[40];

      apply_filter_bank (PEAQ_FILTERBANKEARMODEL(model), fb_state, fb_out_re, fb_out_im);
      for (band = 0; band < 40; band++) {
        A_re[band] = fb_out_re[band];
        A_im[band] = fb_out_im[band];
      }

      /* frequency domain spreading; 2.2.7 in [BS1387], 3.4 in [Kabal03] */
      for (band = 0; band < 40; band++) {
        gdouble fc = peaq_earmodel_get_band_center_frequency (model, band);
        gdouble L =
          10 * log10 (fb_out_re[band] * fb_out_re[band] +
                      fb_out_im[band] * fb_out_im[band]);
        gdouble s = MAX (4, 24 + 230 / fc - 0.2 * L);
        gdouble dist_s = pow (DIST, s);
        /* a and b=1-a are probably swapped in the standard's pseudo code */
#if defined(SWAP_SLOPE_FILTER_COEFFICIENTS) && SWAP_SLOPE_FILTER_COEFFICIENTS
        fb_state->cu[band] = dist_s + SLOPE_FILTER_A * (fb_state->cu[band] - dist_s);
#else
        fb_state->cu[band] = fb_state->cu[band] + SLOPE_FILTER_A * (dist_s - fb_state->cu[band]);
#endif
        gdouble d1 = fb_out_re[band];
        gdouble d2 = fb_out_im[band];
        guint j;
        for (j = band + 1; j < 40; j++) {
          d1 *= fb_state->cu[band];
          d2 *= fb_state->cu[band];
          A_re[j] += d1;
          A_im[j] += d2;
        }
      }

      for (band = 39; band > 0; band--) {
        A_re[band - 1] += CL * A_re[band];
        A_im[band - 1] += CL * A_im[band];
      }

      /* rectification; 2.2.8. in [BS1387], part of 3.4 in [Kabal03] */
      gdouble E0[40];
      for (band = 0; band < 40; band++) {
        E0[band] = A_re[band] * A_re[band] + A_im[band] * A_im[band];
      }

      /* time domain smearing (1) - backward masking; 2.2.9 in [BS1387], 3.5 in
       * [Kabal03] */
      for (band = 0; band < 40; band++) {
        memmove (fb_state->E0_buf[band] + 1, fb_state->E0_buf[band],
                 10 * sizeof (gdouble));
        fb_state->E0_buf[band][0] = E0[band];
      }
    }
  }
  gdouble E1[40];
  for (band = 0; band < 40; band++) {
    guint i;
    E1[band] = 0.;
    /* exploit symmetry */
    for (i = 0; i < 5; i++) {
      E1[band] +=
        (fb_state->E0_buf[band][i] + fb_state->E0_buf[band][10 - i]) *
        fb_ear_model_class->back_mask_h[i];
    }
    /* include term for n=N/2 only once */
    E1[band] += fb_state->E0_buf[band][5] * fb_ear_model_class->back_mask_h[5];

    /* adding of internal noise; 2.2.10 in [BS1387], 3.6 in [Kabal03] */
    gdouble EThres = peaq_earmodel_get_internal_noise (model, band);
    fb_state->unsmeared_excitation[band] = E1[band] + EThres;

    /* time domain smearing (2) - forward masking; 2.2.11 in [BS1387], 3.7 in
     * [Kabal03] */
    gdouble a = peaq_earmodel_get_ear_time_constant (model, band);

    fb_state->excitation[band] =
      a * fb_state->excitation[band] +
      (1. - a) * fb_state->unsmeared_excitation[band];
  }
}

static void
apply_filter_bank (PeaqFilterbankEarModel *model,
                   PeaqFilterbankEarModelState *fb_state,
                   gdouble *fb_out_re, gdouble *fb_out_im)
{
  guint band;
  for (band = 0; band < 40; band++) {
    guint n;
    guint N = filter_length[band];
    /* additional delay, (31) in [BS1387] */
    guint D = 1 + (filter_length[0] - N) / 2;
    gdouble re_out = 0;
    gdouble im_out = 0;
    /* exploit symmetry in filter responses */
    guint N_2 = N / 2;
    gdouble *in1 = fb_state->fb_buf + D + fb_state->fb_buf_offset;
    gdouble *in2 = fb_state->fb_buf + D + N + fb_state->fb_buf_offset;
    gdouble *h_re = model->fbh_re[band];
    gdouble *h_im = model->fbh_im[band];
    /* first filter coefficient is zero, so skip it */
    for (n = 1; n < N_2; n++) {
      in1++;
      h_re++;
      h_im++;
      in2--;
      re_out += (*in1 + *in2) * *h_re; /* even symmetry */
      im_out += (*in1 - *in2) * *h_im; /* odd symmetry */
    }
    /* include term for n=N/2 only once */
    in1++;
    h_re++;
    h_im++;
    re_out += *in1 * *h_re;
    im_out += *in1 * *h_im;
    fb_out_re[band] = re_out;
    fb_out_im[band] = im_out;
  }
}

static gdouble const *
get_excitation (PeaqEarModel const *model, gpointer state)
{
  return ((PeaqFilterbankEarModelState *) state)->excitation;
}

static gdouble const *
get_unsmeared_excitation (PeaqEarModel const *model, gpointer state)
{
  return ((PeaqFilterbankEarModelState *) state)->unsmeared_excitation;
}
