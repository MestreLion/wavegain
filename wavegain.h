#ifndef WAVEGAIN_H
#define WAVEGAIN_H

#include "main.h"

#define NO_PEAK -1.f
#define NO_GAIN -10000.f

extern int get_gain(const char *filename, float *track_peak, float *track_gain, double *dc_offset, double *offset,
	SETTINGS *settings);
extern int write_gains(const char *filename, float radio_gain, float audiophile_gain, float TitlePeak,
	double *dc_offset, double *album_dc_offset, SETTINGS *settings);

#endif /* WAVEGAIN_H */

