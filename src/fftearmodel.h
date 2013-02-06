/* GstPEAQ
 * Copyright (C) 2006, 2011, 2013 Martin Holters <martin.holters@hsuhh.de>
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

#define PEAQ_TYPE_FFTEARMODELPARAMS (peaq_fftearmodelparams_get_type ())
#define PEAQ_FFTEARMODELPARAMS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_FFTEARMODELPARAMS, \
                               PeaqFFTEarModelParams))
#define PEAQ_FFTEARMODELPARAMS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_FFTEARMODELPARAMS, \
                            PeaqFFTEarModelParamsClass))
#define PEAQ_IS_FFTEARMODELPARAMS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE (obj, PEAQ_TYPE_FFTEARMODELPARAMS))
#define PEAQ_IS_FFTEARMODELPARAMS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE (klass, PEAQ_TYPE_FFTEARMODELPARAMS))
#define PEAQ_FFTEARMODELPARAMS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_FFTEARMODELPARAMS, \
                              PeaqFFTEarModelParamsClass))

/**
 * FFT_FRAMESIZE:
 *
 * The length (in samples) of a frame to be processed by 
 * peaq_earmodel_process().
 */
#define FFT_FRAMESIZE 2048

typedef struct _PeaqFFTEarModelClass PeaqFFTEarModelClass;
typedef struct _PeaqFFTEarModel PeaqFFTEarModel;
typedef struct _PeaqFFTEarModelParamsClass PeaqFFTEarModelParamsClass;
typedef struct _PeaqFFTEarModelParams PeaqFFTEarModelParams;

GType peaq_fftearmodel_get_type ();
PeaqFFTEarModelParams *peaq_fftearmodel_get_fftmodel_params (PeaqFFTEarModel
                                                             const *ear);
void peaq_fftearmodel_process (PeaqFFTEarModel *ear, gfloat *sample_data,
                               EarModelOutput *output);

GType peaq_fftearmodelparams_get_type ();
void peaq_fftearmodelparams_group_into_bands (PeaqFFTEarModelParams const
                                              *params, gdouble *spectrum,
                                              gdouble *band_power);
#endif
