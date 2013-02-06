/* GstPEAQ
 * Copyright (C) 2013 Martin Holters <martin.holters@hsuhh.de>
 *
 * fbearmodel.h: Filer bank-based peripheral ear model part.
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


#ifndef __FBEARMODEL_H__
#define __FBEARMODEL_H__ 1

#include <glib-object.h>
#include <earmodel.h>

#define PEAQ_TYPE_FILTERBANKEARMODELPARAMS \
  (peaq_filterbankearmodelparams_get_type ())
#define PEAQ_FILTERBANKEARMODELPARAMS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_FILTERBANKEARMODELPARAMS, \
                               PeaqFilterbankEarModelParams))

#define PEAQ_TYPE_FILTERBANKEARMODEL (peaq_filterbankearmodel_get_type ())
#define PEAQ_FILTERBANKEARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_FILTERBANKEARMODEL, \
                               PeaqFilterbankEarModel))

/**
 * FB_FRAMESIZE:
 *
 * The length (in samples) of a frame to be processed by 
 * peaq_filterbankearmodel_process().
 */
#define FB_FRAMESIZE 192

#if 0
struct _EarModelOutput
{
  gdouble power_spectrum[FRAMESIZE / 2 + 1];
  gdouble weighted_power_spectrum[FRAMESIZE / 2 + 1];
  gdouble *band_power;
  gdouble *unsmeared_excitation;
  gdouble *excitation;
  gdouble overall_loudness;
};
#endif

typedef struct _PeaqFilterbankEarModelParamsClass
  PeaqFilterbankEarModelParamsClass;
typedef struct _PeaqFilterbankEarModelParams PeaqFilterbankEarModelParams;

typedef struct _PeaqFilterbankEarModelClass PeaqFilterbankEarModelClass;
typedef struct _PeaqFilterbankEarModel PeaqFilterbankEarModel;
#if 0
typedef struct _EarModelOutput EarModelOutput;
#endif

GType peaq_filterbankearmodelparams_get_type ();

GType peaq_filterbankearmodel_get_type ();
void peaq_filterbankearmodel_process (PeaqFilterbankEarModel *ear,
                                      gfloat *sample_data,
                                      EarModelOutput *output);
#endif
