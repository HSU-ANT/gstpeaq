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

#define SLOPE_FILTER_A 0.993355506255034        /* exp (-32 / (48000 * 0.1)) */
#define BUFFER_LENGTH 1457

const guint filter_length[40] = {
  1456, 1438, 1406, 1362, 1308, 1244, 1176, 1104, 1030, 956, 884, 814, 748,
  686, 626, 570, 520, 472, 430, 390, 354, 320, 290, 262, 238, 214, 194, 176,
  158, 144, 130, 118, 106, 96, 86, 78, 70, 64, 58, 52
};

enum
{
  PROP_0,
  PROP_PLAYBACK_LEVEL
};

struct _PeaqFilterbankEarModel
{
  PeaqEarModel parent;
  gdouble level_factor;
  gdouble *fbh_re[40];
  gdouble *fbh_im[40];
};

struct _PeaqFilterbankEarModelClass
{
  PeaqEarModelClass parent;
  gdouble back_mask_h[12];
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
};


static void class_init (gpointer klass, gpointer class_data);
static void init (GTypeInstance *obj, gpointer klass);
static void finalize (GObject *obj);
static gdouble get_playback_level (PeaqEarModel const *model);
static void set_playback_level (PeaqEarModel *model, double level);
static gpointer state_alloc (PeaqEarModel const *model);
static void state_free (PeaqEarModel const *model, gpointer state);
static void process_block (PeaqEarModel const *model, gpointer state,
                           gfloat const *sample_data, EarModelOutput *output);


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
  ear_model_class->loudness_scale = 1.26539;
  ear_model_class->step_size = 192;
  ear_model_class->tau_min = 0.004;
  ear_model_class->tau_100 = 0.020;

  for (i = 0; i < 12; i++) {
    fb_ear_model_class->back_mask_h[i] =
      cos (M_PI * (i - 5.) / 12.) * cos (M_PI * (i - 5.) / 12.) * 0.9761 / 6;
  }
}

static void
init (GTypeInstance *obj, gpointer klass)
{
  guint band;
  PeaqFilterbankEarModel *model = PEAQ_FILTERBANKEARMODEL (obj);

  GArray *fc_array = g_array_sized_new (FALSE, FALSE, sizeof (gdouble), 40);

  for (band = 0; band < 40; band++) {
    guint n;
    gdouble fc =
      sinh ((asinh (50. / 650.) +
             band * (asinh (18000. / 650.) -
                     asinh (50. / 650.)) / 39.)) * 650.;
    g_array_append_val (fc_array, fc);
    guint N = filter_length[band];
    /* include outer and middle ear filtering in filter bank coefficients */
    gdouble Wt = peaq_earmodel_calc_ear_weight (fc);
    model->fbh_re[band] = g_new (gdouble, N);
    model->fbh_im[band] = g_new (gdouble, N);
    for (n = 0; n < N; n++) {
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
  PEAQ_FILTERBANKEARMODEL (model)->level_factor =
    pow (10., level / 20.);
}

static
gpointer state_alloc (PeaqEarModel const *model)
{
  guint band;
  PeaqFilterbankEarModelState *state = g_new0 (PeaqFilterbankEarModelState, 1);
  for (band = 0; band < 40; band++)
    state->E0_buf[band] = g_new0 (gdouble, 12);
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
               gfloat const *sample_data, EarModelOutput *output)
{
  guint k;
  gint band;
  gdouble level_factor =
    PEAQ_FILTERBANKEARMODEL (model)->level_factor;
  PeaqFilterbankEarModelClass *fb_ear_model_class =
    PEAQ_FILTERBANKEARMODEL_GET_CLASS (model);
  PeaqFilterbankEarModelState *fb_state = (PeaqFilterbankEarModelState *) state;
  gdouble dist = pow (0.1,
                      7. * (asinh (18000. / 650.) -
                            asinh (50. / 650.)) / (39. * 20.));
  gdouble cl = pow (dist, 31);

  for (k = 0; k < FB_FRAMESIZE; k++) {
    /* 2.2.3 Setting of playback level */
    gdouble scaled_input = sample_data[k] * level_factor;

    /* 2.2.4 DC rejection filter */
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

    /* 2.2.5 Filter bank; 2.2.6 Outer and middle ear filtering */
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
      for (band = 0; band < 40; band++) {
        guint n;
        guint N = filter_length[band];
        guint D = 1 + (filter_length[0] - N) / 2;
        gdouble re_out = 0;
        gdouble im_out = 0;
        /* exploit symmetry in filter responses */
        guint N_2 = N / 2;
        gdouble *in1 = fb_state->fb_buf + D + fb_state->fb_buf_offset;
        gdouble *in2 = fb_state->fb_buf + D + N + fb_state->fb_buf_offset;
        gdouble *h_re = PEAQ_FILTERBANKEARMODEL (model)->fbh_re[band];
        gdouble *h_im = PEAQ_FILTERBANKEARMODEL (model)->fbh_im[band];
        for (n = 1; n < N_2; n++) {
          in1++;
          h_re++;
          h_im++;
          in2--;
          re_out += (*in1 + *in2) * *h_re;
          im_out += (*in1 - *in2) * *h_im;
        }
        in1++;
        h_re++;
        h_im++;
        re_out += *in1 * *h_re;
        im_out += *in1 * *h_im;
        fb_out_re[band] = re_out;
        fb_out_im[band] = im_out;
        A_re[band] = re_out;
        A_im[band] = im_out;
      }

      /* 2.2.7 Frequency domain spreading */
      for (band = 0; band < 40; band++) {
        gdouble fc = peaq_earmodel_get_band_center_frequency (model, band);
        gdouble L =
          10 * log10 (fb_out_re[band] * fb_out_re[band] +
                      fb_out_im[band] * fb_out_im[band]);
        gdouble s = MAX (4, 24 + 230 / fc - 0.2 * L);
        /* a and b=1-a are probably swapped in the standard's pseudo code */
        // ear->cu[band] = a * pow (dist, s) + b * ear->cu[band];
        // ear->cu[band] = b * pow (dist, s) + a * ear->cu[band];
        gdouble dist_s = pow (dist, s);
        fb_state->cu[band] = dist_s + SLOPE_FILTER_A * (fb_state->cu[band] - dist_s);
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

      gdouble d1 = 0.;
      gdouble d2 = 0.;
      for (band = 39; band >= 0; band--) {
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
        memmove (fb_state->E0_buf[band] + 1, fb_state->E0_buf[band],
                 11 * sizeof (gdouble));
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
    E1[band] += fb_state->E0_buf[band][5] * fb_ear_model_class->back_mask_h[5];

    /* 2.2.10 Adding of internal noise */
    gdouble EThres = peaq_earmodel_get_internal_noise (model, band);
    output->unsmeared_excitation[band] = E1[band] + EThres;

    /* 2.2.11 Time domain smearing (2) - Forward masking */
    gdouble a = peaq_earmodel_get_ear_time_constant (model, band);

    fb_state->excitation[band] =
      a * fb_state->excitation[band] +
      (1. - a) * output->unsmeared_excitation[band];
    output->excitation[band] = fb_state->excitation[band];
  }
}
