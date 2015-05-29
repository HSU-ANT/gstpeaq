/* GstPEAQ
 * Copyright (C) 2006, 2011, 2013, 2014, 2015
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * fftearmodel.h: FFT-based peripheral ear model part.
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

#ifndef __FFTEARMODEL_H__
#define __FFTEARMODEL_H__ 1

#include "earmodel.h"

#define PEAQ_TYPE_FFTEARMODEL (peaq_fftearmodel_get_type ())
#define PEAQ_FFTEARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_FFTEARMODEL, PeaqFFTEarModel))
#define PEAQ_FFTEARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_FFTEARMODEL, PeaqFFTEarModelClass))
#define PEAQ_IS_FFTEARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE (obj, PEAQ_TYPE_FFTEARMODEL))
#define PEAQ_IS_FFTEARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE (klass, PEAQ_TYPE_FFTEARMODEL))
#define PEAQ_FFTEARMODEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_FFTEARMODEL, PeaqFFTEarModelClass))

typedef struct _PeaqFFTEarModelClass PeaqFFTEarModelClass;
typedef struct _PeaqFFTEarModel PeaqFFTEarModel;

void peaq_fftearmodel_group_into_bands (PeaqFFTEarModel const *model,
                                        gdouble const *spectrum,
                                        gdouble *band_power);
gdouble const *peaq_fftearmodel_get_masking_difference (PeaqFFTEarModel const *model);
gdouble const *peaq_fftearmodel_get_power_spectrum (gpointer state);
gdouble const *peaq_fftearmodel_get_weighted_power_spectrum (gpointer state);
gboolean peaq_fftearmodel_is_energy_threshold_reached (gpointer state);
GType peaq_fftearmodel_get_type ();
#endif
