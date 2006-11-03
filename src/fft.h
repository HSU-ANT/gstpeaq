#ifndef _FFT_H
#define _FFT_H 1

typedef struct _FFTData
{
  unsigned int N;
  double *wkr;
  double *wki;
} FFTData;

FFTData *create_fft_data (unsigned int N);
void free_fft_data (FFTData * data);
void compute_fft (FFTData * data, double *xr, double *xi, double *yr,
		  double *yi);
void compute_real_fft (FFTData * data, double *x, double *yr, double *yi);

#endif
