#include "fft.h"

#include <stdlib.h>
#include <math.h>

FFTData *
create_fft_data (unsigned int N)
{
  unsigned int k;
  FFTData *data = malloc (sizeof (FFTData));
  data->N = N;
  data->wkr = calloc (N, sizeof (double));
  data->wki = calloc (N, sizeof (double));
  for (k = 0; k < N / 2; k++) {
    double kth = -2 * k * M_PI / N;
    data->wkr[k] = cos (kth);
    data->wki[k] = sin (kth);
  }
}

void
free_fft_data (FFTData * data)
{
  free (data->wkr);
  free (data->wki);
  free (data);
}

void
compute_fft (FFTData * data, double *xr, double *xi, double *yr, double *yi)
{
  unsigned int i, j;
  int transSize;
  unsigned int N = data->N;

  // Store in bit-reversed order
  yr[N - 1] = xr[N - 1];
  yi[N - 1] = xi[N - 1];
  j = 1;
  for (i = 1; i < N; i++) {
    unsigned int m;
    yr[i - 1] = xr[j - 1];
    yi[i - 1] = xi[j - 1];
    m = N / 2;
    while (m >= 1 && j > m) {
      j -= m;
      m /= 2;
    }
    j += m;
  }

  // Apply decimation algorithm
  for (transSize = 1; transSize < N; transSize *= 2) {
    int offset;
    int whop = N / 2 / transSize;
    for (offset = 0; offset < N; offset += 2 * transSize) {
      for (i = 0; i < transSize; i++) {
	int i1 = i + offset;
	int i2 = i + offset + transSize;
	int widx = i * whop;
	double qr = yr[i1];
	double qi = yi[i1];
	double rr = yr[i2];
	double ri = yi[i2];
	yr[i1] = qr + data->wkr[widx] * rr - data->wki[widx] * ri;
	yi[i1] = qi + data->wkr[widx] * ri + data->wki[widx] * rr;
	yr[i2] = qr - data->wkr[widx] * rr + data->wki[widx] * ri;
	yi[i2] = qi - data->wkr[widx] * ri - data->wki[widx] * rr;
      }
    }
  }
}
