/* GstPEAQ
 * Copyright (C) 2013 Martin Holters <martin.holters@hsuhh.de>
 *
 * movaccum.c: Model out variable (MOV) accumulation.
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

#include "movaccum.h"

#include <math.h>

typedef enum _Status Status;

enum _Status
{
  STATUS_INIT,
  STATUS_NORMAL,
  STATUS_TENTATIVE
};

struct _PeaqMovAccumClass
{
  GObjectClass parent;
};

struct _PeaqMovAccum
{
  GObjectClass parent;
  Status status;
  PeaqMovAccumMode mode;
  gdouble num[2];
  gdouble den[2];
  gdouble num_saved[2];
  gdouble den_saved[2];
  gdouble past_sqrts[2][3];
};

static void class_init (gpointer klass, gpointer class_data);
static void init (GTypeInstance *obj, gpointer klass);
static gboolean handle_state (PeaqMovAccum *acc, gboolean tentative);

GType
peaq_movaccum_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqMovAccumClass),
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      class_init,
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof (PeaqMovAccum),
      0,                        /* n_preallocs */
      init                      /* instance_init */
    };
    type =
      g_type_register_static (G_TYPE_OBJECT, "GstPeaqMovAccum", &info, 0);
  }
  return type;
}

PeaqMovAccum *
peaq_movaccum_new ()
{
  PeaqMovAccum *acc = g_object_new (PEAQ_TYPE_MOVACCUM, NULL);
  return acc;
}

static void
class_init (gpointer klass, gpointer class_data)
{
  //GObjectClass *object_class = G_OBJECT_CLASS (klass);

  //object_class->finalize = finalize;
}

static void
init (GTypeInstance *obj, gpointer klass)
{
  guint c;
  PeaqMovAccum *acc = PEAQ_MOVACCUM (obj);
  acc->status = STATUS_INIT;
  acc->mode = MODE_AVG;
  for (c = 0; c < 2; c++) {
    guint i;
    acc->num[c] = 0.;
    acc->den[c] = 0.;
    for (i = 0; i < 3; i++)
      acc->past_sqrts[c][i] = NAN;
  }
};

void
peaq_movaccum_set_mode (PeaqMovAccum *acc, PeaqMovAccumMode mode)
{
  acc->mode = mode;
}

void
peaq_movaccum_accumulate (PeaqMovAccum *acc, guint c, gdouble val,
                          gboolean tentative)
{
  if (!handle_state (acc, tentative))
    return;
  switch (acc->mode) {
    case MODE_RMS:
      acc->num[c] += val * val;
      acc->den[c] += 1.;
      break;
    case MODE_AVG:
    case MODE_AVG_LOG:
    case MODE_ADB:
      acc->num[c] += val;
      acc->den[c] += 1.;
      break;
    case MODE_AVG_WINDOW:
      {
        guint i;
        gdouble val_sqrt = sqrt (val);
        if (!isnan (acc->past_sqrts[c][0])) {
          gdouble winsum = val_sqrt;
          for (i = 0; i < 3; i++) {
            winsum += acc->past_sqrts[c][i];
          }
          winsum /= 4.;
          winsum *= winsum;
          winsum *= winsum;
          acc->num[c] += winsum;
          acc->den[c] += 1.;
        }
        for (i = 0; i < 2; i++) {
          acc->past_sqrts[c][i] = acc->past_sqrts[c][i + 1];
        }
        acc->past_sqrts[c][2] = val_sqrt;
      }
      break;
    case MODE_FILTERED_MAX:
      acc->den[c] = 0.9 * acc->den[c] + 0.1 * val;
      if (acc->den[c] > acc->num[c])
        acc->num[c] = acc->den[c];
      break;
  }
}

void
peaq_movaccum_accumulate_weighted (PeaqMovAccum *acc, guint c, gdouble val,
                                   gdouble weight, gboolean tentative)
{
  if (!handle_state (acc, tentative))
    return;
  switch (acc->mode) {
    case MODE_RMS:
      weight *= weight;
      acc->num[c] += val * val * weight;
      acc->den[c] += weight;
      break;
    case MODE_AVG:
      acc->num[c] += weight * val;
      acc->den[c] += weight;
      break;
    case MODE_AVG_LOG:
    case MODE_ADB:
    case MODE_AVG_WINDOW:
    case MODE_FILTERED_MAX:
      g_warn_if_reached ();
      break;
  }
}

static gboolean
handle_state (PeaqMovAccum *acc, gboolean tentative)
{
  if (tentative) {
    switch (acc->status) {
      case STATUS_INIT:
        /* ignore initial tentative values */
        return FALSE;
      case STATUS_NORMAL:
        /* transition to tentative status */
        {
          guint i;
          for (i = 0; i < 2; i++) {
            acc->num_saved[i] = acc->num[i];
            acc->den_saved[i] = acc->den[i];
          }
          acc->status = STATUS_TENTATIVE;
        }
        break;
      case STATUS_TENTATIVE:
        break;
    }
  } else {
    acc->status = STATUS_NORMAL;
  }
  return TRUE;
}

gdouble
peaq_movaccum_get_value (PeaqMovAccum *acc, guint channels)
{
  gdouble *num;
  gdouble *den;
  gdouble value = 0.;
  guint c;
  if (acc->status == STATUS_TENTATIVE) {
    num = acc->num_saved;
    den = acc->den_saved;
  } else {
    num = acc->num;
    den = acc->den;
  }
  for (c = 0; c < channels; c++) {
    switch (acc->mode) {
      case MODE_AVG:
        value += num[c] / den[c];
        break;
      case MODE_AVG_LOG:
        value += 10. * log10 (num[c] / den[c]);
        break;
      case MODE_AVG_WINDOW:
      case MODE_RMS:
        value += sqrt (num[c] / den[c]);
        break;
      case MODE_FILTERED_MAX:
        value += num[c];
        break;
      case MODE_ADB:
        if (den[c] > 0)
          value += num[c] == 0. ? -0.5 : log10 (num[c] / den[c]);
        break;
    }
  }
  value /= channels;
  return value;
}
