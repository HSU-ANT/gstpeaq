/* GstPEAQ
 * Copyright (C) 2006 Martin Holters <martin.holters@hsuhh.de>
 *
 * earmodel.h: Peripheral ear model part.
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


#ifndef __EARMODEL_H__
#define __EARMODEL_H__ 1

#include <fft.h>
#include <glib-object.h>

#define PEAQ_TYPE_EARMODEL (peaq_earmodel_get_type ())
#define PEAQ_EARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_EARMODEL, PeaqEarModel))
#define PEAQ_EARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_EARMODEL, PeaqEarModelClass))
#define PEAQ_IS_EARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE (obj, PEAQ_TYPE_EARMODEL))
#define PEAQ_IS_EARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE (klass, PEAQ_TYPE_EARMODEL))
#define PEAQ_EARMODEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_EARMODEL, PeaqEarModelClass))


typedef struct _EarModelOutput {
} EarModelOutput;

typedef struct _PeaqEarModelClass PeaqEarModelClass;
typedef struct _PeaqEarModel PeaqEarModel;

struct _PeaqEarModelClass {
  GObjectClass parent;
  gdouble *hann_window;
  FFTData *fft_data;
  gdouble *outer_middle_ear_weight;
  guint *band_lower_end;
  guint *band_upper_end;
  gdouble *band_lower_weight;
  gdouble *band_upper_weight;
  gdouble *internal_noise_level;
  gdouble lower_spreading;
  gdouble lower_spreading_exponantiated;
  gdouble *spreading_normalization;
  gdouble *ear_time_constants;
};

struct _PeaqEarModel {
  GObjectClass parent;
  gdouble level_factor;
  gdouble *filtered_excitation;
};

GType peaq_earmodel_get_type ();
void peaq_earmodel_process (PeaqEarModel * ear, gfloat *sample_data, 
				EarModelOutput * output);
gdouble peaq_earmodel_get_band_center_frequency (guint band);
#endif
