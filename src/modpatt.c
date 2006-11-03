/* GstPEAQ
 * Copyright (C) 2006 Martin Holters <martin.holters@hsuhh.de>
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

static void peaq_modulationprocessor_class_init (gpointer klass,
						 gpointer class_data);
static void peaq_modulationprocessor_init (GTypeInstance * obj,
					   gpointer klass);

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

static void
peaq_modulationprocessor_class_init (gpointer klass, gpointer class_data)
{
  guint k;
  PeaqModulationProcessorClass *level_class =
    PEAQ_MODULATIONPROCESSOR_CLASS (klass);

  level_class->ear_time_constants = g_new (gdouble, CRITICAL_BAND_COUNT);
  for (k = 0; k < CRITICAL_BAND_COUNT; k++) {
    gdouble tau;
    gdouble curr_fc;
    curr_fc = peaq_earmodel_get_band_center_frequency (k);
    tau = 0.008 + 100 / curr_fc * (0.05 - 0.008);
    level_class->ear_time_constants[k] =
      exp (-(gdouble) FRAMESIZE / (2 * SAMPLINGRATE) / tau);
  }
}

static void
peaq_modulationprocessor_init (GTypeInstance * obj, gpointer klass)
{
  PeaqModulationProcessor *modproc = PEAQ_MODULATIONPROCESSOR (obj);
  guint i;
  modproc->previous_loudness = g_new (gdouble, CRITICAL_BAND_COUNT);
  modproc->filtered_loudness = g_new (gdouble, CRITICAL_BAND_COUNT);
  modproc->filtered_loudness_derivative =
    g_new (gdouble, CRITICAL_BAND_COUNT);
  for (i = 0; i < CRITICAL_BAND_COUNT; i++) {
    modproc->previous_loudness[i] = 0;
    modproc->filtered_loudness[i] = 0;
    modproc->filtered_loudness_derivative[i] = 0;
  }
}

void
peaq_modulationprocessor_process (PeaqModulationProcessor * modproc,
				  gdouble * unsmeared_exciation,
				  ModulationProcessorOutput * output)
{
  guint k;
  PeaqModulationProcessorClass *modproc_class =
    PEAQ_MODULATIONPROCESSOR_GET_CLASS (modproc);
  for (k = 0; k < CRITICAL_BAND_COUNT; k++) {
    gdouble loudness = pow (unsmeared_exciation[k], 0.3);
    gdouble loudness_derivative = (gdouble) SAMPLINGRATE / (FRAMESIZE / 2) *
      ABS (loudness - modproc->previous_loudness[k]);
    modproc->filtered_loudness_derivative[k] =
      modproc_class->ear_time_constants[k] *
      modproc->filtered_loudness_derivative[k] +
      (1 - modproc_class->ear_time_constants[k]) * loudness_derivative;
    modproc->filtered_loudness[k] =
      modproc_class->ear_time_constants[k] * modproc->filtered_loudness[k] +
      (1 - modproc_class->ear_time_constants[k]) * loudness;
    output->modulation[k] = modproc->filtered_loudness_derivative[k] /
      (1 + modproc->filtered_loudness[k] / 0.3);
    modproc->previous_loudness[k] = loudness;
  }
  output->average_loudness = modproc->filtered_loudness;
}
