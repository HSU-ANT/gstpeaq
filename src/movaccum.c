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

/**
 * SECTION:movaccum
 * @short_description: Model output variable accumulator.
 * @title: PeaqMovAccum
 *
 */

#include "movaccum.h"

#include <math.h>

typedef enum _Status Status;
typedef struct _Fraction Fraction;
typedef struct _TwinFraction TwinFraction;
typedef struct _WinAvgData WinAvgData;
typedef struct _FiltMaxData FiltMaxData;

enum _Status
{
  STATUS_INIT,
  STATUS_NORMAL,
  STATUS_TENTATIVE
};

struct _Fraction {
  gdouble num;
  gdouble den;
};

struct _TwinFraction {
  gdouble num1;
  gdouble num2;
  gdouble den;
};

struct _WinAvgData {
  Fraction frac;
  gdouble past_sqrts[3];
};

struct _FiltMaxData {
  gdouble max;
  gdouble filt_state;
};

/**
 * PeaqMovAccumClass:
 * 
 * The opaque PeaqMovAccumClass structure.
 */
struct _PeaqMovAccumClass
{
  GObjectClass parent;
};

struct _PeaqMovAccum
/**
 * PeaqMovAccum:
 * 
 * The opaque PeaqMovAccum structure.
 */
{
  GObjectClass parent;
  Status status;
  PeaqMovAccumMode mode;
  guint channels;
  gpointer *data;
  gpointer *data_saved;
};

static void class_init (gpointer klass, gpointer class_data);
static void init (GTypeInstance *obj, gpointer klass);
static void finalize (GObject *obj);
static void realloc_data (PeaqMovAccum *acc, guint old_channels);

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
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = finalize;
}

static void
init (GTypeInstance *obj, gpointer klass)
{
  PeaqMovAccum *acc = PEAQ_MOVACCUM (obj);

  acc->channels = 0;
  acc->data = NULL;
  acc->data_saved = NULL;
  acc->status = STATUS_INIT;
  acc->mode = MODE_AVG;
  realloc_data (acc, 0);
};

static void
finalize (GObject *obj)
{
  guint c;
  PeaqMovAccum *acc = PEAQ_MOVACCUM (obj);

  for (c = 0; c < acc->channels; c++) {
    g_free (acc->data[c]);
    g_free (acc->data_saved[c]);
  }

  g_free (acc->data);
  g_free (acc->data_saved);
}

void
peaq_movaccum_set_channels (PeaqMovAccum *acc, guint channels)
{
  if (acc->channels != channels) {
    guint old_channels = acc->channels;
    acc->channels = channels;
    realloc_data (acc, old_channels);
  }
}

guint
peaq_movaccum_get_channels (PeaqMovAccum const *acc)
{
  return acc->channels;
}

void
peaq_movaccum_set_mode (PeaqMovAccum *acc, PeaqMovAccumMode mode)
{
  if (acc->mode != mode) {
    acc->mode = mode;
    realloc_data (acc, acc->channels);
  }
}

PeaqMovAccumMode
peaq_movaccum_get_mode (PeaqMovAccum *acc)
{
  return acc->mode;
}

static void
realloc_data (PeaqMovAccum *acc, guint old_channels)
{
  guint c;

  for (c = 0; c < old_channels; c++) {
    g_free (acc->data[c]);
    g_free (acc->data_saved[c]);
  }

  if (acc->channels != old_channels) {
    g_free (acc->data);
    g_free (acc->data_saved);
    acc->data = g_new0 (gpointer, acc->channels);
    acc->data_saved = g_new0 (gpointer, acc->channels);
  }

  for (c = 0; c < acc->channels; c++)
    switch (acc->mode) {
      case MODE_AVG:
      case MODE_AVG_LOG:
      case MODE_RMS:
      case MODE_ADB:
        acc->data[c] = g_new0 (Fraction, acc->channels);
        acc->data_saved[c] = g_new0 (Fraction, acc->channels);
        break;
      case MODE_RMS_ASYM:
        acc->data[c] = g_new0 (TwinFraction, acc->channels);
        acc->data_saved[c] = g_new0 (TwinFraction, acc->channels);
        break;
      case MODE_AVG_WINDOW:
        {
          acc->data[c] = g_new0 (WinAvgData, acc->channels);
          acc->data_saved[c] = g_new0 (Fraction, acc->channels);
          guint i;
          for (i = 0; i < 3; i++)
            ((WinAvgData *) acc->data[c])->past_sqrts[i] = NAN;
        }
        break;
      case MODE_FILTERED_MAX:
        acc->data[c] = g_new0 (FiltMaxData, acc->channels);
        acc->data_saved[c] = g_new0 (FiltMaxData, acc->channels);
        break;
    }
}


void
peaq_movaccum_set_tentative (PeaqMovAccum *acc, gboolean tentative)
{
  if (tentative) {
    if (acc->status == STATUS_NORMAL) {
      /* transition to tentative status */
      guint i;
      for (i = 0; i < acc->channels; i++) {
        switch (acc->mode) {
          case MODE_AVG:
          case MODE_AVG_LOG:
          case MODE_RMS:
          case MODE_ADB:
          case MODE_AVG_WINDOW:
            ((Fraction *) acc->data_saved[i])->num =
              ((Fraction *) acc->data[i])->num;
            ((Fraction *) acc->data_saved[i])->den =
              ((Fraction *) acc->data[i])->den;
            break;
          case MODE_RMS_ASYM:
            ((TwinFraction *) acc->data_saved[i])->num1 =
              ((TwinFraction *) acc->data[i])->num1;
            ((TwinFraction *) acc->data_saved[i])->num2 =
              ((TwinFraction *) acc->data[i])->num2;
            ((TwinFraction *) acc->data_saved[i])->den =
              ((TwinFraction *) acc->data[i])->den;
            break;
          case MODE_FILTERED_MAX:
            ((FiltMaxData *) acc->data_saved[i])->max =
              ((FiltMaxData *) acc->data[i])->max;
            break;
        }
      }
      acc->status = STATUS_TENTATIVE;
    }
  } else {
    acc->status = STATUS_NORMAL;
  }
}

void
peaq_movaccum_accumulate (PeaqMovAccum *acc, guint c, gdouble val,
                          gdouble weight)
{
  if (acc->status == STATUS_INIT)
    return;
  switch (acc->mode) {
    case MODE_RMS:
      weight *= weight;
      ((Fraction *)acc->data[c])->num += weight * val * val;
      ((Fraction *)acc->data[c])->den += weight;
      break;
    case MODE_RMS_ASYM:
      /* abuse weight as second input */
      ((TwinFraction *)acc->data[c])->num1 += val * val;
      ((TwinFraction *)acc->data[c])->num2 += weight * weight;
      ((TwinFraction *)acc->data[c])->den += 1.;
      break;
    case MODE_AVG:
    case MODE_AVG_LOG:
    case MODE_ADB:
      ((Fraction *)acc->data[c])->num += weight * val;
      ((Fraction *)acc->data[c])->den += weight;
      break;
    case MODE_AVG_WINDOW:
      /* weight is ignored */
      {
        guint i;
        gdouble val_sqrt = sqrt (val);
        if (!isnan (((WinAvgData *) acc->data[c])->past_sqrts[0])) {
          gdouble winsum = val_sqrt;
          for (i = 0; i < 3; i++) {
            winsum += ((WinAvgData *) acc->data[c])->past_sqrts[i];
          }
          winsum /= 4.;
          winsum *= winsum;
          winsum *= winsum;
          ((Fraction *) acc->data[c])->num += winsum;
          ((Fraction *) acc->data[c])->den += 1.;
        }
        for (i = 0; i < 2; i++) {
          ((WinAvgData *) acc->data[c])->past_sqrts[i] =
            ((WinAvgData *) acc->data[c])->past_sqrts[i + 1];
        }
        ((WinAvgData *) acc->data[c])->past_sqrts[2] = val_sqrt;
      }
      break;
    case MODE_FILTERED_MAX:
      /* weight is ignored */
      {
        FiltMaxData *filt_data = (FiltMaxData *) acc->data[c];
        filt_data->filt_state = 0.9 * filt_data->filt_state + 0.1 * val;
        if (filt_data->filt_state > filt_data->max)
          filt_data->max = filt_data->filt_state;
      }
      break;
  }
}

gdouble
peaq_movaccum_get_value (PeaqMovAccum const *acc)
{
  gpointer *data;
  gdouble value = 0.;
  guint c;
  if (acc->status == STATUS_TENTATIVE) {
    data = acc->data_saved;
  } else {
    data = acc->data;
  }
  for (c = 0; c < acc->channels; c++) {
    switch (acc->mode) {
      case MODE_AVG:
        value += ((Fraction *) data[c])->num / ((Fraction *) data[c])->den;
        break;
      case MODE_AVG_LOG:
        value += 10. * log10 (((Fraction *) data[c])->num /
                              ((Fraction *) data[c])->den);
        break;
      case MODE_AVG_WINDOW:
      case MODE_RMS:
        value += sqrt (((Fraction *) data[c])->num /
                       ((Fraction *) data[c])->den);
        break;
      case MODE_RMS_ASYM:
        value += sqrt (((TwinFraction *) data[c])->num1 /
                       ((TwinFraction *) data[c])->den);
        value += 0.5 * sqrt (((TwinFraction *) data[c])->num2 /
                             ((TwinFraction *) data[c])->den);
        break;
      case MODE_FILTERED_MAX:
        value += ((FiltMaxData *) data[c])->max;
        break;
      case MODE_ADB:
        if (((Fraction *) data[c])->den > 0)
          value += ((Fraction *) data[c])->num == 0. ?
            -0.5 :
            log10 (((Fraction *) data[c])->num / ((Fraction *) data[c])->den);
        break;
    }
  }
  value /= acc->channels;
  return value;
}
