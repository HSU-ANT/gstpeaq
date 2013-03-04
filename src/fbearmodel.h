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

#define PEAQ_TYPE_FILTERBANKEARMODEL \
  (peaq_filterbankearmodel_get_type ())
#define PEAQ_FILTERBANKEARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_FILTERBANKEARMODEL, \
                               PeaqFilterbankEarModel))
#define PEAQ_FILTERBANKEARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_FILTERBANKEARMODEL, PeaqFilterbankEarModelClass))
#define PEAQ_FILTERBANKEARMODEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_FILTERBANKEARMODEL, PeaqFilterbankEarModelClass))


/**
 * FB_FRAMESIZE:
 *
 * The length (in samples) of a frame to be processed by 
 * peaq_earmodel_process_block() for #PeaqFilterbankEarModel instances.
 */
#define FB_FRAMESIZE 192

typedef struct _PeaqFilterbankEarModelClass PeaqFilterbankEarModelClass;
typedef struct _PeaqFilterbankEarModel PeaqFilterbankEarModel;

GType peaq_filterbankearmodel_get_type ();
#endif
