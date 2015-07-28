/* GstPEAQ
 * Copyright (C) 2006, 2007, 2012, 2013, 2015
 * Martin Holters <martin.holters@hsu-hh.de>
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

/**
 * SECTION:leveladapter
 * @short_description: Level and pattern adaptation.
 * @title: PeaqLevelAdapter
 *
 * #PeaqLevelAdapter encapsulates the level and pattern adaptation described in
 * section 3.1 of <xref linkend="BS1387" />. It estimates the per-band level
 * differences between reference and test signal and adapts them to each other
 * to compensate level differences and linear distortions.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>

#include "leveladapter.h"
#include "gstpeaq.h"

/**
 * PeaqLevelAdapterClass:
 *
 * The opaque PeaqLevelAdapterClass structure.
 */
struct _PeaqLevelAdapterClass
{
  GObjectClass parent;
};

/**
 * PeaqLevelAdapter:
 *
 * The opaque PeaqLevelAdapter structure.
 */
struct _PeaqLevelAdapter
{
  GObject parent;
  PeaqEarModel *ear_model;
  gdouble *ear_time_constants;
  gdouble *ref_filtered_excitation;
  gdouble *test_filtered_excitation;
  gdouble *filtered_num;
  gdouble *filtered_den;
  gdouble *pattcorr_ref;
  gdouble *pattcorr_test;
  gdouble *spectrally_adapted_ref_patterns;
  gdouble *spectrally_adapted_test_patterns;
};

static void class_init (gpointer klass, gpointer class_data);
static void init (GTypeInstance * obj, gpointer klass);
static void finalize (GObject * obj);

GType
peaq_leveladapter_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqLevelAdapterClass),
      NULL,			/* base_init */
      NULL,			/* base_finalize */
      class_init,
      NULL,			/* class_finalize */
      NULL,			/* class_data */
      sizeof (PeaqLevelAdapter),
      0,			/* n_preallocs */
      init                      /* instance_init */
    };
    type = g_type_register_static (G_TYPE_OBJECT, "PeaqLevelAdapter", &info, 0);
  }
  return type;
}

/**
 * peaq_leveladapter_new:
 * @ear_model: The #PeaqEarModel to get the band information from.
 *
 * Constructs a new #PeaqLevelAdapter, using the given @ear_model to obtain
 * information about the number of frequency bands used and their center
 * frequencies.
 *
 * Returns: The newly constructed #PeaqLevelAdapter.
 */
PeaqLevelAdapter *
peaq_leveladapter_new (PeaqEarModel *ear_model)
{
  PeaqLevelAdapter *level = g_object_new (PEAQ_TYPE_LEVELADAPTER, NULL);
  peaq_leveladapter_set_ear_model (level, ear_model);
  return level;
}

static void
class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = finalize;
}

static void
init (GTypeInstance * obj, gpointer klass)
{
  PeaqLevelAdapter *level = PEAQ_LEVELADAPTER (obj);
  level->ear_time_constants = NULL;
  level->ref_filtered_excitation = NULL;
  level->test_filtered_excitation = NULL;
  level->filtered_num = NULL;
  level->filtered_den = NULL;
  level->pattcorr_ref = NULL;
  level->pattcorr_test = NULL;
  level->spectrally_adapted_ref_patterns = NULL;
  level->spectrally_adapted_test_patterns = NULL;
}

static void 
finalize (GObject * obj)
{
  PeaqLevelAdapter *level = PEAQ_LEVELADAPTER (obj);
  GObjectClass *parent_class = 
    G_OBJECT_CLASS (g_type_class_peek_parent (g_type_class_peek
					      (PEAQ_TYPE_LEVELADAPTER)));
  if (level->ear_model) {
    g_object_unref (level->ear_model);
    g_free (level->ref_filtered_excitation);
    g_free (level->test_filtered_excitation);
    g_free (level->filtered_num);
    g_free (level->filtered_den);
    g_free (level->pattcorr_ref);
    g_free (level->pattcorr_test);
    g_free (level->spectrally_adapted_ref_patterns);
    g_free (level->spectrally_adapted_test_patterns);
    g_free (level->ear_time_constants);
  }
  parent_class->finalize(obj);
}

/**
 * peaq_leveladapter_set_ear_model:
 * @level: The #PeaqLevelAdapter to set the #PeaqEarModel of.
 * @ear_model: The #PeaqEarModel to get the band information from.
 *
 * Sets the #PeaqEarModel from which the frequency band information is used and
 * precomputes time constants that depend on the band center frequencies.
 */
void
peaq_leveladapter_set_ear_model (PeaqLevelAdapter *level,
                                 PeaqEarModel *ear_model)
{
  guint band_count, k;

  if (level->ear_model) {
    g_object_unref (level->ear_model);
    g_free (level->ref_filtered_excitation);
    g_free (level->test_filtered_excitation);
    g_free (level->filtered_num);
    g_free (level->filtered_den);
    g_free (level->pattcorr_ref);
    g_free (level->pattcorr_test);
    g_free (level->spectrally_adapted_ref_patterns);
    g_free (level->spectrally_adapted_test_patterns);
    g_free (level->ear_time_constants);
  }
  g_object_ref (ear_model);
  level->ear_model = ear_model;

  band_count = peaq_earmodel_get_band_count (ear_model);

  level->ref_filtered_excitation = g_new0 (gdouble, band_count);
  level->test_filtered_excitation = g_new0 (gdouble, band_count);
  level->filtered_num = g_new0 (gdouble, band_count);
  level->filtered_den = g_new0 (gdouble, band_count);
  level->pattcorr_ref = g_new0 (gdouble, band_count);
  level->pattcorr_test = g_new0 (gdouble, band_count);
  level->spectrally_adapted_ref_patterns = g_new0 (gdouble, band_count);
  level->spectrally_adapted_test_patterns = g_new0 (gdouble, band_count);

  level->ear_time_constants = g_new (gdouble, band_count);

  /* see section 3.1 in [BS1387], section 4.1 in [Kabal03] */
  for (k = 0; k < band_count; k++) {
    level->ear_time_constants[k] =
      peaq_earmodel_calc_time_constant (ear_model, k, 0.008, 0.05);
  }
#if 0
  /* [Kabal03] suggests initialization to 1, although the standard does not
   * mention it; there seems to be almost no difference on conformance, though
   */
  for (k = 0; k < band_count; k++) {
    level->pattcorr_ref[k] = 1.;
    level->pattcorr_test[k] = 1.;
  }
#endif
}

/**
 * peaq_leveladapter_process:
 * @level: The #PeaqLevelAdapter.
 * @ref_exciation: The excitation patterns of the reference signal
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mover accent="true"><mi>E</mi><mo>~</mo></mover><mi>sR</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @test_exciation: The excitation patterns of the test signal
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>Test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mover accent="true"><mi>E</mi><mo>~</mo></mover><mi>sT</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 *
 * Performs the actual level and pattern adaptation as described in 
 * section 3.1 of <xref linkend="BS1387" /> and section 4.1 of <xref
 * linkend="Kabal03" />. The number of elements in the input data
 * @ref_exciation and @test_exciation as well as in the preallocated output
 * data @spectrally_adapted_ref_patterns and @spectrally_adapted_test_patterns
 * has to match the number of bands specified by the underlying #PeaqEarModel
 * as set with peaq_leveladapter_set_ear_model() or upon construction with
 * peaq_leveladapter_new().
 */
void
peaq_leveladapter_process (PeaqLevelAdapter *level,
                           gdouble const *ref_exciation,
			   gdouble const *test_exciation)
{
  guint band_count, k;
  gdouble num, den;
  gdouble lev_corr;
  gdouble *levcorr_excitation;
  gdouble const *levcorr_ref_excitation;
  gdouble const *levcorr_test_excitation;
  gdouble *pattadapt_ref;
  gdouble *pattadapt_test;
  band_count = peaq_earmodel_get_band_count (level->ear_model);
  pattadapt_ref = g_newa (gdouble, band_count);
  pattadapt_test = g_newa (gdouble, band_count);
  levcorr_excitation = g_newa (gdouble, band_count);

  num = 0.;
  den = 0.;
  for (k = 0; k < band_count; k++) {
    /* (42) in [BS1387], (56) in [Kabal03] */
    level->ref_filtered_excitation[k] =
      level->ear_time_constants[k] * level->ref_filtered_excitation[k] +
      (1 - level->ear_time_constants[k]) * ref_exciation[k];
    /* (43) in [BS1387], (56) in [Kabal03] */
    level->test_filtered_excitation[k]
      = level->ear_time_constants[k] * level->test_filtered_excitation[k]
      + (1 - level-> ear_time_constants[k]) * test_exciation[k];
    /* (45) in [BS1387], (57) in [Kabal03] */
    num +=
      sqrt (level->ref_filtered_excitation[k] *
	    level->test_filtered_excitation[k]);
    den += level->test_filtered_excitation[k];
  }
  lev_corr = num * num / (den * den);
  if (lev_corr > 1) {
    levcorr_test_excitation = test_exciation;
    for (k = 0; k < band_count; k++)
      /* (46) in [BS1387], (58) in [Kabal03] */
      levcorr_excitation[k] = ref_exciation[k] / lev_corr;
    levcorr_ref_excitation = levcorr_excitation;
  } else {
    levcorr_ref_excitation = ref_exciation;
      /* (47) in [BS1387], (58) in [Kabal03] */
    for (k = 0; k < band_count; k++)
      levcorr_excitation[k] = test_exciation[k] * lev_corr;
    levcorr_test_excitation = levcorr_excitation;
  }
  for (k = 0; k < band_count; k++) {
    /* (48) in [BS1387], (59) in [Kabal03] */
    level->filtered_num[k] =
      level->ear_time_constants[k] * level->filtered_num[k] +
      levcorr_test_excitation[k] * levcorr_ref_excitation[k];
    level->filtered_den[k] =
      level->ear_time_constants[k] * level->filtered_den[k] +
      levcorr_ref_excitation[k] * levcorr_ref_excitation[k];
    /* (49) in [BS1387], (60) in [Kabal03] */
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
    /* (51) in [BS1387], (63) in [Kabal03] */
    /* dependence on band_count is an ugly hack to avoid a nasty switch/case */
    guint m1 = MIN (k, band_count / 36); /* 109 -> 3, 55 -> 1, 40 -> 1  */
    guint m2 = MIN (band_count - k - 1, band_count / 25); /* 109 -> 4, 55 -> 2, 40 -> 1  */
    /* (50) in [BS1387], (62) in [Kabal03] */
    ra_ref = 0.;
    ra_test = 0.;
    for (l = k - m1; l <= k + m2; l++) {
      ra_ref += pattadapt_ref[l];
      ra_test += pattadapt_test[l];
    }
    ra_ref /= (m1 + m2 + 1);
    ra_test /= (m1 + m2 + 1);
    /* (50) in [BS1387], (61) in [Kabal03] */
    level->pattcorr_ref[k] =
      level->ear_time_constants[k] * level->pattcorr_ref[k] +
      (1 - level->ear_time_constants[k]) * ra_ref;
    level->pattcorr_test[k] =
      level->ear_time_constants[k] * level->pattcorr_test[k] +
      (1 - level->ear_time_constants[k]) * ra_test;
    /* (52) in [BS1387], (64) in [Kabal03] */
    level->spectrally_adapted_ref_patterns[k] =
      levcorr_ref_excitation[k] * level->pattcorr_ref[k];
    /* (53) in [BS1387], (64) in [Kabal03] */
    level->spectrally_adapted_test_patterns[k] =
      levcorr_test_excitation[k] * level->pattcorr_test[k];
  }
}

/**
 * peaq_leveladapter_get_adapted_ref:
 * @level: The #PeaqLevelAdapter to get the current spectrally adapted patterns
 * from.
 *
 * Returns the spectrally adapted patterns of the reference signal as computed
 * during the last call to peaq_leveladapter_process().
 *
 * Returns: The spectrally adapted patterns of the reference signal
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mover accent="true"><mi>E</mi><mo>~</mo></mover><mi>PR</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * The pointer points to internal data of the #PeaqLevelAdapter and must not be
 * freed.
 */
gdouble const*
peaq_leveladapter_get_adapted_ref (PeaqLevelAdapter const* level)
{
  return level->spectrally_adapted_ref_patterns;
}

/**
 * peaq_leveladapter_get_adapted_test:
 * @level: The #PeaqLevelAdapter to get the current spectrally adapted patterns
 * from.
 *
 * Returns the spectrally adapted patterns of the test signal as computed
 * during the last call to peaq_leveladapter_process().
 *
 * Returns: The spectrally adapted patterns of the test signal
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mover accent="true"><mi>E</mi><mo>~</mo></mover><mi>PT</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * The pointer points to internal data of the #PeaqLevelAdapter and must not be
 * freed.
 */
gdouble const*
peaq_leveladapter_get_adapted_test (PeaqLevelAdapter const* level)
{
  return level->spectrally_adapted_test_patterns;
}
