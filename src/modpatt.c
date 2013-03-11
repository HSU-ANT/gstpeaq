/* GstPEAQ
 * Copyright (C) 2006, 2013 Martin Holters <martin.holters@hsuhh.de>
 *
 * modpatt.c: Modulation pattern processor.
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
 * SECTION:modpatt
 * @short_description: Modulation Pattern Processing.
 * @title: PeaqModulationProcessor
 *
 * #PeaqModulationProcessor encapsulates the modulation processing described in
 * section 3.2 of <xref linkend="BS1387" />. It computes the per-band
 * modulation parameters.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "modpatt.h"
#include "gstpeaq.h"

/**
 * PeaqModulationProcessorClass:
 *
 * The opaque PeaqModulationProcessorClass structure.
 */
struct _PeaqModulationProcessorClass
{
  GObjectClass parent;
};

/**
 * PeaqModulationProcessor:
 *
 * The opaque PeaqModulationProcessor structure.
 */
struct _PeaqModulationProcessor
{
  GObjectClass parent;
  PeaqEarModel *ear_model;
  gdouble *ear_time_constants;
  gdouble *previous_loudness;
  gdouble *filtered_loudness;
  gdouble *filtered_loudness_derivative;
  gdouble *modulation;
};

static void class_init (gpointer klass, gpointer class_data);
static void init (GTypeInstance *obj, gpointer klass);
static void finalize (GObject *obj);

GType
peaq_modulationprocessor_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqModulationProcessorClass),
      NULL,			/* base_init */
      NULL,			/* base_finalize */
      class_init,
      NULL,			/* class_finalize */
      NULL,			/* class_data */
      sizeof (PeaqModulationProcessor),
      0,			/* n_preallocs */
      init	                /* instance_init */
    };
    type = g_type_register_static (G_TYPE_OBJECT,
				   "GstPeaqModulationProcessor", &info, 0);
  }
  return type;
}

/**
 * peaq_modulationprocessor_new:
 * @ear_model: The #PeaqEarModel to get the band information from.
 *
 * Constructs a new #PeaqModulationProcessor, using the given @ear_model to
 * obtain information about the number of frequency bands used and their center
 * frequencies.
 *
 * Returns: The newly constructed #PeaqModulationProcessor.
 */
PeaqModulationProcessor *
peaq_modulationprocessor_new (PeaqEarModel *ear_model)
{
  PeaqModulationProcessor *modproc
    = g_object_new (PEAQ_TYPE_MODULATIONPROCESSOR, NULL);
  peaq_modulationprocessor_set_ear_model (modproc, ear_model);
  return modproc;
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
  PeaqModulationProcessor *modproc = PEAQ_MODULATIONPROCESSOR (obj);
  modproc->ear_model = NULL;
  modproc->previous_loudness = NULL;
  modproc->filtered_loudness = NULL;
  modproc->filtered_loudness_derivative = NULL;
  modproc->ear_time_constants = NULL;
  modproc->modulation = NULL;
}

static void 
finalize (GObject * obj)
{
  PeaqModulationProcessor *modproc = PEAQ_MODULATIONPROCESSOR (obj);
  GObjectClass *parent_class = 
    G_OBJECT_CLASS (g_type_class_peek_parent (g_type_class_peek
					      (PEAQ_TYPE_MODULATIONPROCESSOR)));
  g_free (modproc->previous_loudness);
  g_free (modproc->filtered_loudness);
  g_free (modproc->filtered_loudness_derivative);
  g_free (modproc->ear_time_constants);
  g_free (modproc->modulation);
  g_object_unref (modproc->ear_model);
  parent_class->finalize(obj);
}

/**
 * peaq_modulationprocessor_set_ear_model:
 * @modproc: The #PeaqModulationProcessor to set the #PeaqEarModel of.
 * @ear_model: The #PeaqEarModel to get the band information from.
 *
 * Sets the #PeaqEarModel from which the frequency band information is used and
 * precomputes time constants that depend on the band center frequencies.
 */
void
peaq_modulationprocessor_set_ear_model (PeaqModulationProcessor *modproc,
                                        PeaqEarModel *ear_model)
{
  guint band_count, k;
  if (modproc->ear_model) {
    g_object_unref (modproc->ear_model);
    g_free (modproc->previous_loudness);
    g_free (modproc->filtered_loudness);
    g_free (modproc->filtered_loudness_derivative);
  }
  g_object_ref (ear_model);
  modproc->ear_model = ear_model;

  band_count = peaq_earmodel_get_band_count (ear_model);

  modproc->previous_loudness = g_new0 (gdouble, band_count);
  modproc->filtered_loudness = g_new0 (gdouble, band_count);
  modproc->filtered_loudness_derivative = g_new0 (gdouble, band_count);

  modproc->modulation = g_renew (gdouble, modproc->modulation, band_count);

  modproc->ear_time_constants =
    g_renew (gdouble, modproc->ear_time_constants, band_count);
  for (k = 0; k < band_count; k++) {
    /* (56) in [BS1387] */ 
    modproc->ear_time_constants[k] =
      peaq_earmodel_calc_time_constant (ear_model, k, 0.008, 0.05);
  }
}

/**
 * peaq_modulationprocessor_process:
 * @modproc: The #PeaqModulationProcessor.
 * @unsmeared_excitation: The unsmeared excitation patterns
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>E</mi><mn>2</mn></msub>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />,
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>E</mi><mi>s</mi></msub>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 *
 * Performs the actual computation of the modulation as described in section
 * 3.2 of <xref linkend="BS1387" /> and section 4.2 of <xref linkend="Kabal03"
 * />. The number of elements in the input data @unsmeared_excitation has to
 * match the number of bands specified by the underlying #PeaqEarModel as set
 * with peaq_modulationprocessor_set_ear_model() or upon construction with
 * peaq_modulationprocessor_new().
 */
void
peaq_modulationprocessor_process (PeaqModulationProcessor *modproc,
				  gdouble const* unsmeared_excitation)
{
  guint band_count, step_size, k;

  band_count = peaq_earmodel_get_band_count (modproc->ear_model);
  step_size = peaq_earmodel_get_step_size (modproc->ear_model);

  for (k = 0; k < band_count; k++) {
    /* (54) in [BS1387] */ 
    gdouble loudness = pow (unsmeared_excitation[k], 0.3);
    gdouble loudness_derivative = (gdouble) SAMPLINGRATE / step_size *
      ABS (loudness - modproc->previous_loudness[k]);
    modproc->filtered_loudness_derivative[k] =
      modproc->ear_time_constants[k] *
      modproc->filtered_loudness_derivative[k] +
      (1 - modproc->ear_time_constants[k]) * loudness_derivative;
    /* (55) in [BS1387] */ 
    modproc->filtered_loudness[k] =
      modproc->ear_time_constants[k] * modproc->filtered_loudness[k] +
      (1. - modproc->ear_time_constants[k]) * loudness;
    /* (57) in [BS1387] */ 
    modproc->modulation[k] = modproc->filtered_loudness_derivative[k] /
      (1. + modproc->filtered_loudness[k] / 0.3);
    modproc->previous_loudness[k] = loudness;
  }
}

/**
 * peaq_modulationprocessor_get_average_loudness:
 * @modproc: The #PeaqModulationProcessor to get the current average loudness from.
 *
 * Returns the average loudness
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mover accent="true"><mi>E</mi><mi>-</mi></mover>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * as computed during the last call to peaq_modulationprocessor_process().
 *
 * Returns: The average loudness
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mover accent="true"><mi>E</mi><mi>-</mi></mover>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>.
 * The pointer points to internal data of the #PeaqModulationProcessor and must
 * not be freed.
 */
gdouble const *
peaq_modulationprocessor_get_average_loudness (PeaqModulationProcessor const *modproc)
{
  return modproc->filtered_loudness;
}

/**
 * peaq_modulationprocessor_get_modulation:
 * @modproc: The #PeaqModulationProcessor to get the current modulation from.
 *
 * Returns the modulation
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>Mod</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * as computed during the last call to peaq_modulationprocessor_process().
 *
 * Returns: The modulation
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>Mod</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>.
 * The pointer points to internal data of the #PeaqModulationProcessor and must
 * not be freed.
 */
gdouble const *
peaq_modulationprocessor_get_modulation (PeaqModulationProcessor const *modproc)
{
  return modproc->modulation;
}
