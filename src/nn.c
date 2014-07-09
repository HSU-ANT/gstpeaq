/* GstPEAQ
 * Copyright (C) 2014 Martin Holters <martin.holters@hsuhh.de>
 *
 * nn.c: Evaluate neural network to compute DI and ODG
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

#include <math.h>

#include "nn.h"
#include "settings.h"

static const gdouble amin_basic[] = {
  393.916656, 361.965332, -24.045116, 1.110661, -0.206623, 0.074318, 1.113683,
  0.950345, 0.029985, 0.000101, 0.
};

static const gdouble amax_basic[] = {
  921, 881.131226, 16.212030, 107.137772, 2.886017, 13.933351, 63.257874,
  1145.018555, 14.819740, 1., 1.
};

static const gdouble wx_basic[11][3] = {
  {-0.502657, 0.436333, 1.219602},
  {4.307481, 3.246017, 1.123743},
  {4.984241, -2.211189, -0.192096},
  {0.051056, -1.762424, 4.331315},
  {2.321580, 1.789971, -0.754560},
  {-5.303901, -3.452257, -10.814982},
  {2.730991, -6.111805, 1.519223},
  {0.624950, -1.331523, -5.955151},
  {3.102889, 0.871260, -5.922878},
  {-1.051468, -0.939882, -0.142913},
  {-1.804679, -0.503610, -0.620456}
};

static const double wxb_basic[] = { -2.518254, 0.654841, -2.207228 };
static const double wy_basic[] = { -3.817048, 4.107138, 4.629582, -0.307594 };

static const double wyb_basic = -0.307594;

static const gdouble amin_advanced[5] = {
  13.298751, 0.041073, -25.018791, 0.061560, 0.02452
};

static const gdouble amax_advanced[5] = {
  2166.5, 13.24326, 13.46708, 10.226771, 14.224874
};

static const gdouble wx_advanced[5][5] = {
  {21.211773, -39.013052, -1.382553, -14.545348, -0.320899},
  {-8.981803, 19.956049, 0.935389, -1.686586, -3.238586},
  {1.633830, -2.877505, -7.442935, 5.606502, -1.783120},
  {6.103821, 19.587435, -0.240284, 1.088213, -0.511314},
  {11.556344, 3.892028, 9.720441, -3.287205, -11.031250},
};

static const double wxb_advanced[5] =
  { 1.330890, 2.686103, 2.096598, -1.327851, 3.087055 };

static const double wy_advanced[5] =
  { -4.696996, -3.289959, 7.004782, 6.651897, 4.009144 };
static const double wyb_advanced = -1.360308;

static const gdouble bmin = -3.98;
static const gdouble bmax = 0.22;

gdouble
peaq_calculate_di_basic (gdouble *movs)
{
  guint i;
  gdouble x[3];
  gdouble distortion_index;

  for (i = 0; i < 3; i++)
    x[i] = 0;
  for (i = 0; i <= 10; i++) {
    guint j;
    gdouble m = (movs[i] - amin_basic[i]) / (amax_basic[i] - amin_basic[i]);
    /* according to [Kabal03], it is unclear whether the MOVs should be
     * clipped to within [amin, amax], although [BS1387] does not mention
     * clipping at all; doing so slightly improves the results of the
     * conformance test */
#if CLAMP_MOVS
    if (m < 0.)
      m = 0.;
    if (m > 1.)
      m = 1.;
#endif
    for (j = 0; j < 3; j++)
      x[j] += wx_basic[i][j] * m;
  }
  distortion_index = wyb_basic;
  for (i = 0; i < 3; i++)
    distortion_index += wy_basic[i] / (1 + exp (-(wxb_basic[i] + x[i])));

  return distortion_index;
}

gdouble
peaq_calculate_di_advanced (gdouble *movs)
{
  guint i;
  gdouble x[5];
  gdouble distortion_index;

  for (i = 0; i < 5; i++)
    x[i] = 0;
  for (i = 0; i <= 4; i++) {
    guint j;
    gdouble m =
      (movs[i] - amin_advanced[i]) / (amax_advanced[i] - amin_advanced[i]);
    /* according to [Kabal03], it is unclear whether the MOVs should be
     * clipped to within [amin, amax], although [BS1387] does not mention
     * clipping at all; doing so slightly improves the results of the
     * conformance test */
#if CLAMP_MOVS
    if (m < 0.)
      m = 0.;
    if (m > 1.)
      m = 1.;
#endif
    for (j = 0; j < 5; j++)
      x[j] += wx_advanced[i][j] * m;
  }
  distortion_index = wyb_advanced;
  for (i = 0; i < 5; i++)
    distortion_index +=
      wy_advanced[i] / (1 + exp (-(wxb_advanced[i] + x[i])));

  return distortion_index;
}

gdouble
peaq_calculate_odg (gdouble distortion_index)
{
  return bmin + (bmax - bmin) / (1 + exp (-distortion_index));
}
