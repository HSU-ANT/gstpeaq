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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>

#include "modpatt.h"
#include "gstpeaq.h"

struct _PeaqModulationProcessorClass
{
  GObjectClass parent;
};

struct _PeaqModulationProcessor
{
  GObjectClass parent;
  PeaqEarModel *ear_params;
  gdouble *ear_time_constants;
  gdouble *previous_loudness;
  gdouble *filtered_loudness;
  gdouble *filtered_loudness_derivative;
};

static void peaq_modulationprocessor_class_init (gpointer klass,
						 gpointer class_data);
static void peaq_modulationprocessor_init (GTypeInstance * obj,
					   gpointer klass);
static void peaq_modulationprocessor_finalize (GObject * obj);

GType
peaq_modulationprocessor_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqModulationProcessorClass),
      NULL,			/* base_init */
      NULL,			/* base_finalize */
      peaq_modulationprocessor_class_init,
      NULL,			/* class_finalize */
      NULL,			/* class_data */
      sizeof (PeaqModulationProcessor),
      0,			/* n_preallocs */
      peaq_modulationprocessor_init	/* instance_init */
    };
    type = g_type_register_static (G_TYPE_OBJECT,
				   "GstPeaqModulationProcessor", &info, 0);
  }
  return type;
}

PeaqModulationProcessor *
peaq_modulationprocessor_new (PeaqEarModel *ear_params)
{
  PeaqModulationProcessor *modproc
    = g_object_new (PEAQ_TYPE_MODULATIONPROCESSOR, NULL);
  peaq_modulationprocessor_set_ear_model_params (modproc, ear_params);
  return modproc;
}


static void
peaq_modulationprocessor_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = peaq_modulationprocessor_finalize;
}

static void
peaq_modulationprocessor_init (GTypeInstance * obj, gpointer klass)
{
  PeaqModulationProcessor *modproc = PEAQ_MODULATIONPROCESSOR (obj);
  modproc->ear_params = NULL;
  modproc->previous_loudness = NULL;
  modproc->filtered_loudness = NULL;
  modproc->filtered_loudness_derivative = NULL;
  modproc->ear_time_constants = NULL;
}

static void 
peaq_modulationprocessor_finalize (GObject * obj)
{
  PeaqModulationProcessor *modproc = PEAQ_MODULATIONPROCESSOR (obj);
  GObjectClass *parent_class = 
    G_OBJECT_CLASS (g_type_class_peek_parent (g_type_class_peek
					      (PEAQ_TYPE_MODULATIONPROCESSOR)));
  g_free (modproc->previous_loudness);
  g_free (modproc->filtered_loudness);
  g_free (modproc->filtered_loudness_derivative);
  g_free (modproc->ear_time_constants);
  g_object_unref (modproc->ear_params);
  parent_class->finalize(obj);
}

void
peaq_modulationprocessor_set_ear_model_params (PeaqModulationProcessor *modproc,
                                               PeaqEarModel *ear_params)
{
  guint band_count, k;
  if (modproc->ear_params) {
    g_object_unref (modproc->ear_params);
    g_free (modproc->ear_time_constants);
    g_free (modproc->previous_loudness);
    g_free (modproc->filtered_loudness);
    g_free (modproc->filtered_loudness_derivative);
  }
  g_object_ref (ear_params);
  modproc->ear_params = ear_params;

  band_count = peaq_earmodel_get_band_count (ear_params);

  modproc->previous_loudness = g_new0 (gdouble, band_count);
  modproc->filtered_loudness = g_new0 (gdouble, band_count);
  modproc->filtered_loudness_derivative = g_new0 (gdouble, band_count);

  modproc->ear_time_constants = g_new0 (gdouble, band_count);
  for (k = 0; k < band_count; k++) {
    gdouble tau;
    gdouble curr_fc;
    curr_fc = peaq_earmodel_get_band_center_frequency (ear_params, k);
    tau = 0.008 + 100 / curr_fc * (0.05 - 0.008);
    modproc->ear_time_constants[k] =
      exp (-(gdouble) peaq_earmodel_get_step_size (ear_params)
           / (SAMPLINGRATE * tau));
  }
}

void
peaq_modulationprocessor_process (PeaqModulationProcessor * modproc,
				  gdouble const* unsmeared_exciation,
				  ModulationProcessorOutput * output)
{
  guint band_count, step_size, k;

  band_count = peaq_earmodel_get_band_count (modproc->ear_params);
  step_size = peaq_earmodel_get_step_size (modproc->ear_params);

  for (k = 0; k < band_count; k++) {
    gdouble loudness = pow (unsmeared_exciation[k], 0.3);
    gdouble loudness_derivative = (gdouble) SAMPLINGRATE / step_size *
      ABS (loudness - modproc->previous_loudness[k]);
    modproc->filtered_loudness_derivative[k] =
      modproc->ear_time_constants[k] *
      modproc->filtered_loudness_derivative[k] +
      (1 - modproc->ear_time_constants[k]) * loudness_derivative;
    modproc->filtered_loudness[k] =
      modproc->ear_time_constants[k] * modproc->filtered_loudness[k] +
      (1 - modproc->ear_time_constants[k]) * loudness;
    output->modulation[k] = modproc->filtered_loudness_derivative[k] /
      (1 + modproc->filtered_loudness[k] / 0.3);
    modproc->previous_loudness[k] = loudness;
  }
  output->average_loudness = modproc->filtered_loudness;
}
