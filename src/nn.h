/* GstPEAQ
 * Copyright (C) 2014 Martin Holters <martin.holters@hsuhh.de>
 *
 * nn.h: Evaluate neural network to compute DI and ODG
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

#ifndef __NN_H_
#define __NN_H_ 1

#include <glib.h>

gdouble peaq_calculate_di_basic (gdouble *movs);
gdouble peaq_calculate_di_advanced (gdouble *movs);
gdouble peaq_calculate_odg (gdouble distortion_index);

#endif /* __NN_H__ */
