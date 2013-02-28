/* GstPEAQ
 * Copyright (C) 2006, 2013 Martin Holters <martin.holters@hsuhh.de>
 *
 * level.c: Level and pattern adaptation.
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

#include <math.h>

#include "leveladapter.h"
#include "gstpeaq.h"

struct _PeaqLevelAdapterClass
{
  GObjectClass parent;
};

struct _PeaqLevelAdapter
{
  GObjectClass parent;
  PeaqEarModel *ear_params;
  gdouble *ear_time_constants;
  gdouble *ref_filtered_excitation;
  gdouble *test_filtered_excitation;
  gdouble *filtered_num;
  gdouble *filtered_den;
  gdouble *pattcorr_ref;
  gdouble *pattcorr_test;
};

static void peaq_leveladapter_class_init (gpointer klass,
					  gpointer class_data);
static void peaq_leveladapter_init (GTypeInstance * obj, gpointer klass);
static void peaq_leveladapter_finalize (GObject * obj);

GType
peaq_leveladapter_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqLevelAdapterClass),
      NULL,			/* base_init */
      NULL,			/* base_finalize */
      peaq_leveladapter_class_init,
      NULL,			/* class_finalize */
      NULL,			/* class_data */
      sizeof (PeaqLevelAdapter),
      0,			/* n_preallocs */
      peaq_leveladapter_init	/* instance_init */
    };
    type = g_type_register_static (G_TYPE_OBJECT,
				   "PeaqLevelAdapter", &info, 0);
  }
  return type;
}

PeaqLevelAdapter *
peaq_leveladapter_new (PeaqEarModel *ear_params)
{
  PeaqLevelAdapter *level = g_object_new (PEAQ_TYPE_LEVELADAPTER, NULL);
  peaq_leveladapter_set_ear_model_params (level, ear_params);
  return level;
}

static void
peaq_leveladapter_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = peaq_leveladapter_finalize;
}

static void
peaq_leveladapter_init (GTypeInstance * obj, gpointer klass)
{
  PeaqLevelAdapter *level = PEAQ_LEVELADAPTER (obj);
  level->ear_time_constants = NULL;
  level->ref_filtered_excitation = NULL;
  level->test_filtered_excitation = NULL;
  level->filtered_num = NULL;
  level->filtered_den = NULL;
  level->pattcorr_ref = NULL;
  level->pattcorr_test = NULL;
}

static void 
peaq_leveladapter_finalize (GObject * obj)
{
  PeaqLevelAdapter *level = PEAQ_LEVELADAPTER (obj);
  GObjectClass *parent_class = 
    G_OBJECT_CLASS (g_type_class_peek_parent (g_type_class_peek
					      (PEAQ_TYPE_LEVELADAPTER)));
  if (level->ear_params) {
    g_object_unref (level->ear_params);
    g_free (level->ref_filtered_excitation);
    g_free (level->test_filtered_excitation);
    g_free (level->filtered_num);
    g_free (level->filtered_den);
    g_free (level->pattcorr_ref);
    g_free (level->pattcorr_test);
    g_free (level->ear_time_constants);
  }
  parent_class->finalize(obj);
}

void
peaq_leveladapter_set_ear_model_params (PeaqLevelAdapter *level,
                                        PeaqEarModel *ear_params)
{
  guint band_count, k;

  if (level->ear_params) {
    g_object_unref (level->ear_params);
    g_free (level->ref_filtered_excitation);
    g_free (level->test_filtered_excitation);
    g_free (level->filtered_num);
    g_free (level->filtered_den);
    g_free (level->pattcorr_ref);
    g_free (level->pattcorr_test);
    g_free (level->ear_time_constants);
  }
  g_object_ref (ear_params);
  level->ear_params = ear_params;

  band_count = peaq_earmodel_get_band_count (ear_params);

  level->ref_filtered_excitation = g_new0 (gdouble, band_count);
  level->test_filtered_excitation = g_new0 (gdouble, band_count);
  level->filtered_num = g_new0 (gdouble, band_count);
  level->filtered_den = g_new0 (gdouble, band_count);
  level->pattcorr_ref = g_new0 (gdouble, band_count);
  level->pattcorr_test = g_new0 (gdouble, band_count);

  level->ear_time_constants = g_new (gdouble, band_count);

  for (k = 0; k < band_count; k++) {
    gdouble tau;
    gdouble curr_fc;
    curr_fc = peaq_earmodel_get_band_center_frequency (ear_params, k);
    tau = 0.008 + 100 / curr_fc * (0.05 - 0.008);
    level->ear_time_constants[k] =
      exp (-(gdouble) peaq_earmodel_get_step_size (ear_params)
           / (SAMPLINGRATE * tau));
  }
#if 0
  /* [Kabal03] suggests initialization to 1, although the standard does not
   * mention it; there seems to be no difference on conformance, though */
  guint k;
  for (k = 0; k < band_count; k++) {
    level->pattcorr_ref[k] = 1.;
    level->pattcorr_test[k] = 1.;
  }
#endif
}

void
peaq_leveladapter_process (PeaqLevelAdapter * level, gdouble * ref_exciation,
			   gdouble * test_exciation,
			   LevelAdapterOutput * output)
{
  guint band_count, k;
  gdouble num, den;
  gdouble lev_corr;
  gdouble *levcorr_ref_excitation;
  gdouble *levcorr_test_excitation;
  gdouble *pattadapt_ref;
  gdouble *pattadapt_test;
  band_count = peaq_earmodel_get_band_count (level->ear_params);
  pattadapt_ref = g_newa (gdouble, band_count);
  pattadapt_test = g_newa (gdouble, band_count);
  levcorr_ref_excitation = g_newa (gdouble, band_count);
  levcorr_test_excitation = g_newa (gdouble, band_count);

  g_assert (output);

  num = 0;
  den = 0;
  for (k = 0; k < band_count; k++) {
    level->ref_filtered_excitation[k] =
      level->ear_time_constants[k] * level->ref_filtered_excitation[k] +
      (1 - level->ear_time_constants[k]) * ref_exciation[k];
    level->test_filtered_excitation[k]
      = level->ear_time_constants[k] * level->test_filtered_excitation[k]
      + (1 - level-> ear_time_constants[k]) * test_exciation[k];
    num +=
      sqrt (level->ref_filtered_excitation[k] *
	    level->test_filtered_excitation[k]);
    den += level->test_filtered_excitation[k];
  }
  lev_corr = num * num / (den * den);
  if (lev_corr > 1) {
    levcorr_test_excitation = test_exciation;
    for (k = 0; k < band_count; k++)
      levcorr_ref_excitation[k] = ref_exciation[k] / lev_corr;
  } else {
    levcorr_ref_excitation = ref_exciation;
    for (k = 0; k < band_count; k++)
      levcorr_test_excitation[k] = test_exciation[k] * lev_corr;
  }
  for (k = 0; k < band_count; k++) {
    level->filtered_num[k] =
      level->ear_time_constants[k] * level->filtered_num[k] +
      levcorr_test_excitation[k] * levcorr_ref_excitation[k];
    level->filtered_den[k] =
      level->ear_time_constants[k] * level->filtered_den[k] +
      levcorr_ref_excitation[k] * levcorr_ref_excitation[k];
    /* these values cannot be zero [Kabal03], so the special case desribed in
     * [BS1387] is unnecessary */
    if (level->filtered_num[k] >= level->filtered_den[k]) {
      pattadapt_ref[k] = 1.;
      pattadapt_test[k] = level->filtered_den[k] / level->filtered_num[k];
    } else {
      pattadapt_ref[k] = level->filtered_num[k] / level->filtered_den[k];
      pattadapt_test[k] = 1.;
    }
  }
  for (k = 0; k < band_count; k++) {
    gdouble ra_ref, ra_test;
    guint l;
    /* dependence on band_count is an ugly hack to avoid a nasty switch/case */
    guint m1 = MIN (k, band_count / 36); /* 109 -> 3, 55 -> 1, 40 -> 1  */
    guint m2 = MIN (band_count - k - 1, band_count / 25); /* 109 -> 4, 55 -> 2, 40 -> 1  */
    ra_ref = 0;
    ra_test = 0;
    for (l = k - m1; l <= k + m2; l++) {
      ra_ref += pattadapt_ref[l];
      ra_test += pattadapt_test[l];
    }
    ra_ref /= (m1 + m2 + 1);
    ra_test /= (m1 + m2 + 1);
    level->pattcorr_ref[k] =
      level->ear_time_constants[k] * level->pattcorr_ref[k] +
      (1 - level->ear_time_constants[k]) * ra_ref;
    level->pattcorr_test[k] =
      level->ear_time_constants[k] * level->pattcorr_test[k] +
      (1 - level->ear_time_constants[k]) * ra_test;
    output->spectrally_adapted_ref_patterns[k] =
      levcorr_ref_excitation[k] * level->pattcorr_ref[k];
    output->spectrally_adapted_test_patterns[k] =
      levcorr_test_excitation[k] * level->pattcorr_test[k];
  }
}
