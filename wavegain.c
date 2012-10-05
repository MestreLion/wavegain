/*
 * function: ReplayGain support for Wave files (http://www.replaygain.org)
 *
 * Reads in a Vorbis file, figures out the peak and ReplayGain levels and
 * applies the Gain tags to the Wave file
 *
 * This program is distributed under the GNU General Public License, version 
 * 2.1. A copy of this license is included with this source.
 *
 * Copyright (C) 2002-2004 John Edwards
 * Additional code by Magnus Holmgren and Gian-Carlo Pascutto
 * Linux patch by Marc Brooker
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#else
# ifndef __MACOSX__
#  include <sys/io.h>
# endif
#endif

#include <fcntl.h>

#ifndef __MACOSX__
#include <malloc.h>
#endif

#include "gain_analysis.h"
#include "i18n.h"
#include "getopt.h"
#include "misc.h"
#include "audio.h"
#include "dither.h"
#include "main.h"
#include "wavegain.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef ENABLE_RECURSIVE
#include "recurse.h"
#endif

/*Gcc uses LL as a suffix for long long int (64 bit) types - Marc Brooker 8/4/2004*/
#ifdef __GNUC__
#define ROUND64(x)   ( doubletmp = (x) + Dither.Add + (Int64_t)0x001FFFFD80000000LL, *(Int64_t*)(&doubletmp) - (Int64_t)0x433FFFFD80000000LL )
#else
#define ROUND64(x)   ( doubletmp = (x) + Dither.Add + (Int64_t)0x001FFFFD80000000L, *(Int64_t*)(&doubletmp) - (Int64_t)0x433FFFFD80000000L )
#endif

extern int          write_to_log;
dither_t            Dither;
double              doubletmp;
double              total_samples;
double              total_files;

/* Replaced with a double based function for consistency 2005-11-17
static float FABS(float x)
{
	unsigned int *ix=(unsigned int *)&x;
	*ix&=0x7fffffffUL;
	return(x);
}
*/
static double DABS(double x)
{
	Uint64_t *ix=(Uint64_t *)&x;
#ifdef __GNUC__
	*ix&=0x7fffffffffffffffULL;
#else
	*ix&=0x7fffffffffffffff;
#endif
	return(x);
}

/* Dither output */
Int64_t dither_output(int dithering, int shapingtype, int i, double Sum, int k, int format)
{
	double Sum2;
	Int64_t val;
	if(dithering) {
		if(!shapingtype) {
			double  tmp = Random_Equi ( Dither.Dither );
			Sum2 = tmp - Dither.LastRandomNumber [k];
			Dither.LastRandomNumber [k] = (int)tmp;
			Sum2 = Sum += Sum2;
			val = ROUND64 (Sum2)  &  Dither.Mask;
		}
		else {
			Sum2  = Random_Triangular ( Dither.Dither ) - scalar16 ( Dither.DitherHistory[k], Dither.FilterCoeff + i );
			Sum  += Dither.DitherHistory [k] [(-1-i)&15] = (float)Sum2;
			Sum2  = Sum + scalar16 ( Dither.ErrorHistory [k], Dither.FilterCoeff + i );
			val = ROUND64 (Sum2)  &  Dither.Mask;
			Dither.ErrorHistory [k] [(-1-i)&15] = (float)(Sum - val);
		}
	}
	else
		val = (Int64_t)(ROUND64 (Sum));

	if (format == WAV_FMT_8BIT)
		val = val >> 24;
	else if (format == WAV_FMT_16BIT || format == WAV_FMT_AIFF)
		val = val >> 16;
	else if (format == WAV_FMT_24BIT)
		val = val >> 8;

	return (val);
}

/* Get the gain and peak value for a file. Runs in audiophile mode if 
 * audiophile is true.
 *
 * If an error occured, 0 is returned (a message has been printed).
 */

int get_gain(const char *filename, double *track_peak, double *track_gain, 
             double *dc_offset, double *offset, SETTINGS *settings)
{
	wavegain_opt *wg_opts = malloc(sizeof(wavegain_opt));
	FILE         *infile;
	int          result = 0;
	double       new_peak,
	             factor_clip,
	             scale,
	             peak = 0.,
	             dB;
	int          k, i;
	long         chunk;
	input_format *format;

	memset(wg_opts, 0, sizeof(wavegain_opt));

	wg_opts->force = settings->force;

	if(!strcmp(filename, "-")) {
		infile = stdin;
		settings->apply_gain = 0;
		wg_opts->std_in = 1;
#ifdef _WIN32
		_setmode( _fileno(stdin), _O_BINARY );
#endif
	}
	else
		infile = fopen(filename, "rb");

	if (infile == NULL) {
		fprintf (stderr, " Not able to open input file %s.\n", filename) ;
		goto exit;
	}
	wg_opts->apply_gain = 0;

	/*
	 * Now, we need to select an input audio format
	 */

	format = open_audio_file(infile, wg_opts);
	if (!format) {
		/* error reported by reader */
		fprintf (stderr, " Unrecognized file format for %s.\n", filename);
		goto exit;
	}

	if (wg_opts->gain_chunk == 1 && !wg_opts->force) {
		fprintf (stderr, " Skipping File %s, it has already been processed.\n", filename);
//		result = 1;
		goto exit;
	}

	if ((wg_opts->channels != 1) && (wg_opts->channels != 2)) {
		fprintf(stderr, " Unsupported number of channels.\n");
		goto exit;
	}

	/* Only initialize gain analysis once in audiophile mode */
	if (settings->first_file || !settings->audiophile) {
		if (InitGainAnalysis(wg_opts->rate) != INIT_GAIN_ANALYSIS_OK) {
			fprintf(stderr, " Error Initializing Gain Analysis (non-standard samplerate?)\n");
			goto exit;
		}
	}
    
	if (settings->first_file) {
		total_samples = (double)wg_opts->total_samples_per_channel;
		fprintf(stderr, "\n Analyzing...\n\n");
		fprintf(stderr, "    Gain   |  Peak  | Scale | New Peak |Left DC|Right DC| Track\n");
		fprintf(stderr, "           |        |       |          |Offset | Offset |\n");
		fprintf(stderr, " --------------------------------------------------------------\n");
		if(write_to_log) {
			write_log("\n Analyzing...\n\n");
			write_log("    Gain   |  Peak  | Scale | New Peak |Left DC|Right DC| Track\n");
			write_log("           |        |       |          |Offset | Offset |\n");
			write_log(" --------------------------------------------------------------\n");
		}
		settings->first_file = 0;
	}
	else
		total_samples += (double)wg_opts->total_samples_per_channel;

	if (settings->fast && (wg_opts->total_samples_per_channel * (wg_opts->samplesize / 8)
			* wg_opts->channels > 8192000)) {

		long samples_read;
		double **buffer = malloc(sizeof(double *) * wg_opts->channels);

		for (i = 0; i < wg_opts->channels; i++)
			buffer[i] = malloc(BUFFER_LEN * sizeof(double));

		chunk = ((wg_opts->total_samples_per_channel * (wg_opts->samplesize / 8) * wg_opts->channels) + 44) / 1200;

		for(k = 100; k < 1100; k+=5) {

			samples_read = wg_opts->read_samples(wg_opts->readdata, buffer, BUFFER_LEN,
							    settings->fast, chunk * k);
			if (samples_read == 0) {
				break;
			} 
			else {
				if (samples_read < 0) {
					/* Error in the stream. Not a problem, just reporting it in case 
					 * we (the app) cares. In this case, we don't
					 */
				} 
				else {
					int i;
					int j;

					for (i = 0; i < wg_opts->channels; i++) {
						for (j = 0; j < samples_read; j++) {
							buffer[i][j] *= 0x7fff;
							if (DABS(buffer[i][j]) > peak)
								peak = DABS(buffer[i][j]);
						}
					}

					if (AnalyzeSamples(buffer[0], buffer[1], samples_read,
							   wg_opts->channels) != GAIN_ANALYSIS_OK) {
						fprintf(stderr, " Error processing samples.\n");
						for (i = 0; i < wg_opts->channels; i++)
							if (buffer[i]) free(buffer[i]);
						if (buffer) free(buffer);
						goto exit;
					}
				}
			}
		}
		for (i = 0; i < wg_opts->channels; i++)
			if (buffer[i]) free(buffer[i]);
		if (buffer) free(buffer);
	}
	else
	{
		long samples_read;
		double **buffer = malloc(sizeof(double *) * wg_opts->channels);

		for (i = 0; i < wg_opts->channels; i++)
			buffer[i] = malloc(BUFFER_LEN * sizeof(double));

		while (1) {

			samples_read = wg_opts->read_samples(wg_opts->readdata, buffer, BUFFER_LEN, 0, 0);

			if (samples_read == 0) {
				break;
			} 
			else {
				if (samples_read < 0) {
					/* Error in the stream. Not a problem, just reporting it in case 
					 * we (the app) cares. In this case, we don't
					 */
				} 
				else {
					int i;
					int j;

					for (i = 0; i < wg_opts->channels; i++) {
						for (j = 0; j < samples_read; j++) {
							offset[i] += buffer[i][j];
							buffer[i][j] *= 0x7fff;
							if (DABS(buffer[i][j]) > peak)
								peak = DABS(buffer[i][j]);
						}
					}

					if (AnalyzeSamples(buffer[0], buffer[1], samples_read,
							   wg_opts->channels) != GAIN_ANALYSIS_OK) {
						fprintf(stderr, " Error processing samples.\n");
						for (i = 0; i < wg_opts->channels; i++)
							if (buffer[i]) free(buffer[i]);
						if (buffer) free(buffer);
						goto exit;
					}
				}
			}
		}

		for (i = 0; i < wg_opts->channels; i++) {
			if (buffer[i]) free(buffer[i]);
			dc_offset[i] = (double)(offset[i] / wg_opts->total_samples_per_channel);
		}
		if (buffer) free(buffer);
	}
	/*
	 * calculate factors for ReplayGain and ClippingPrevention
	 */
	*track_gain = (GetTitleGain() + settings->man_gain);
	scale = (pow(10., *track_gain * 0.05));
	if(settings->clip_prev) {
		factor_clip  = (32767./( peak + 1));
		if(scale < factor_clip)
			factor_clip = 1.0;
		else
			factor_clip /= scale;
		scale *= factor_clip;
	}
	new_peak = (peak * scale);

        dB = 20. * log10(scale);
	*track_gain = dB;
	{
		int dc_l;
		int dc_r;
		if (settings->no_offset) {
			dc_l = 0;
			dc_r = 0;
		}
		else {
			dc_l = (int)(dc_offset[0] * 32768 * -1);
			dc_r = (int)(dc_offset[1] * 32768 * -1);
		}
		fprintf(stderr, " %+6.2lf dB | %6.0lf | %5.2lf | %8.0lf | %4d  |  %4d  | %s\n",
			*track_gain, peak, scale, new_peak, dc_l, dc_r, filename);
		if(write_to_log) {
			write_log(" %+6.2lf dB | %6.0lf | %5.2lf | %8.0lf | %4d  |  %4d  | %s\n",
				*track_gain, peak, scale, new_peak, dc_l, dc_r, filename);
		}
	}
	if (settings->scale && !settings->audiophile)
		fprintf(stdout, "%8.6lf", scale);

	settings->album_peak = settings->album_peak < peak ? peak : settings->album_peak;
	*track_peak = new_peak;
	result = 1;

exit:
	if (result)
		format->close_func(wg_opts->readdata);
	if (wg_opts)
		free(wg_opts);
	if (infile)
		fclose(infile);
	return result;
}


/* Use the ReplayGain calculations to adjust the gain on the wave file.
 * If audiophile_gain is selected, that value is used, otherwise the
 * radio_gain value is used.
 */
int write_gains(const char *filename, double radio_gain, double audiophile_gain, double TitlePeak, 
                double *dc_offset, double *album_dc_offset, SETTINGS *settings)
{
	wavegain_opt *wg_opts = malloc(sizeof(wavegain_opt));
	FILE         *infile;
	audio_file   *aufile;
	int          readcount,
	             result = 0,
	             delete_temp = 0,
	             i;
	double       Gain;
	double       scale;
	double       total_read = 0.;
	double       wrap_prev_pos;
	double       wrap_prev_neg;
	void         *sample_buffer;
	input_format *format;
	char         tempName[24] = "";
	unsigned int serial;
	char         tempSerial[7] = "";

	memset(wg_opts, 0, sizeof(wavegain_opt));

	wg_opts->force = settings->force;
	wg_opts->undo = settings->undo;

	infile = fopen(filename, "rb");

	if (infile == NULL) {
		fprintf (stderr, " Not able to open input file %s.\n", filename) ;
		goto exit;
	}
	wg_opts->apply_gain = 1;
	wg_opts->write_chunk = settings->write_chunk;

	/*
	 * Now, we need to select an input audio format
	 */

	format = open_audio_file(infile, wg_opts);
	if (!format) {
		format->close_func(wg_opts->readdata);
		if (wg_opts)
			free(wg_opts);
		fclose(infile);
		/* error reported by reader */
		fprintf (stderr, " Unrecognized file format for %s.\n", filename) ;
	}
	else if (wg_opts->undo && !wg_opts->gain_chunk) {
		format->close_func(wg_opts->readdata);
		if (wg_opts)
			free(wg_opts);
		fclose(infile);
		fprintf(stderr, " Skipping file: %s - 'gain' chunk not found.\n", filename);
		if (write_to_log) {
			write_log(" Skipping file: %s - 'gain' chunk not found.\n", filename);
		}
		result = 1;
	}
	else if (wg_opts->gain_scale == 1.0 && !wg_opts->force) {
		format->close_func(wg_opts->readdata);
		if (wg_opts)
			free(wg_opts);
		fclose(infile);
		fprintf(stderr, " Skipping file: %s - Gain already undone.\n", filename);
		if (write_to_log) {
			write_log(" Skipping file: %s - Gain already undone.\n", filename);
		}
		result = 1;
	}
	else {
		double **pcm = malloc(sizeof(double *) * wg_opts->channels);

		for (i = 0; i < wg_opts->channels; i++)
			pcm[i] = malloc(BUFFER_LEN * sizeof(double));

		switch(settings->format) {
			case WAV_NO_FMT:
				if (wg_opts->format == WAV_FMT_AIFF || wg_opts->format == WAV_FMT_AIFC8
								   || wg_opts->format == WAV_FMT_AIFC16) {
					wg_opts->format = WAV_FMT_AIFF;
					wrap_prev_pos = 0x7fff;
					wrap_prev_neg = -0x8000;
				}
				else if (wg_opts->format == WAV_FMT_8BIT) {
					wrap_prev_pos = 0x7f;
					wrap_prev_neg = -0x80;
				}
				else if (wg_opts->format == WAV_FMT_16BIT) {
					wrap_prev_pos = 0x7fff;
					wrap_prev_neg = -0x8000;
				}
				else if (wg_opts->format == WAV_FMT_24BIT) {
					wrap_prev_pos = 0x7fffff;
					wrap_prev_neg = -0x800000;
				}
				else if (wg_opts->format == WAV_FMT_32BIT) {
					wrap_prev_pos = 0x7fffffff;
					wrap_prev_neg = -0x7fffffff;
				}
				else if (wg_opts->format == WAV_FMT_FLOAT) {
					wrap_prev_pos = 1 - (1 / 0x80000000);
					wrap_prev_neg = -1.;
				}
				break;
			case WAV_FMT_8BIT:
				wg_opts->format = WAV_FMT_8BIT;
				wg_opts->samplesize = 8;
				wrap_prev_pos = 0x7f;
				wrap_prev_neg = -0x80;
				break;
			case WAV_FMT_AIFF:
				wg_opts->format = WAV_FMT_AIFF;
				wg_opts->samplesize = 16;
				wg_opts->endianness = BIG;
				wrap_prev_pos = 0x7fff;
				wrap_prev_neg = -0x8000;
				break;
			case WAV_FMT_16BIT:
				wg_opts->format = WAV_FMT_16BIT;
				wg_opts->samplesize = 16;
				wg_opts->endianness = LITTLE;
				wrap_prev_pos = 0x7fff;
				wrap_prev_neg = -0x8000;
				break;
			case WAV_FMT_24BIT:
				wg_opts->format = WAV_FMT_24BIT;
				wg_opts->samplesize = 24;
				wg_opts->endianness = LITTLE;
				wrap_prev_pos = 0x7fffff;
				wrap_prev_neg = -0x800000;
				break;
			case WAV_FMT_32BIT:
				wg_opts->format = WAV_FMT_32BIT;
				wg_opts->samplesize = 32;
				wg_opts->endianness = LITTLE;
				wrap_prev_pos = 0x7fffffff;
				wrap_prev_neg = -0x7fffffff;
				break;
			case WAV_FMT_FLOAT:
				wg_opts->format = WAV_FMT_FLOAT;
				wg_opts->samplesize = 32;
				wg_opts->endianness = LITTLE;
				wrap_prev_pos = 1 - (1 / 0x80000000);
				wrap_prev_neg = -1.;
				break;
		}

		wg_opts->std_out = settings->std_out;

		/* Create temp file name */
                srand(time(NULL) ^ getpid());
		serial = rand();
#ifdef _WIN32
		sprintf(tempSerial,  "%d", serial);
		strcpy(tempName, TEMP_NAME);
		strcat(tempName, tempSerial);
#else
		snprintf(tempSerial, 6, "%d", serial);
		strncpy(tempName, TEMP_NAME, 17);
		printf("tempName=%s tempSerial=%s\n", tempName, tempSerial);
		strncat(tempName, tempSerial, 6);
#endif		
		aufile = open_output_audio_file(tempName, wg_opts);

		if (aufile == NULL) {
			fprintf (stderr, " Not able to open output file %s.\n", TEMP_NAME);
			fclose(infile);
			goto exit;
		}

		Init_Dither (wg_opts->samplesize, settings->shapingtype);

		if (wg_opts->undo) {
			scale = 1.0 / wg_opts->gain_scale;
		        Gain = 20. * log10(scale);
			wg_opts->gain_scale = 1.0;
		}
		else {
			if (settings->audiophile)
				Gain = audiophile_gain;
			else
				Gain = radio_gain;

			scale = pow(10., Gain * 0.05);
			wg_opts->gain_scale = scale;
		}

		fprintf(stderr, "                                             \r");
		fprintf(stderr, " Applying Gain of %+5.2lf dB to file: %s\n", Gain, filename);
		if (write_to_log) {
			write_log(" Applying Gain of %+5.2lf dB to file: %s\n", Gain, filename);
		}

		while (1) {

			readcount = wg_opts->read_samples(wg_opts->readdata, pcm, BUFFER_LEN, 0, 0);

			total_read += ((double)readcount / wg_opts->rate);
			total_files += ((double)readcount / wg_opts->rate);
			if( (long)total_files % 4 == 0) {
				if (wg_opts->undo)
					fprintf(stderr, "This file %3.0lf%% done\r", 
						total_read / (wg_opts->total_samples_per_channel / wg_opts->rate) * 100);
				else
					fprintf(stderr, "This file %3.0lf%% done\tAll files %3.0lf%% done\r", 
						total_read / (wg_opts->total_samples_per_channel / wg_opts->rate) * 100,
						total_files / (total_samples / wg_opts->rate) * 100);
			}

			if (readcount == 0) {
				break;
			} 
			else if (readcount < 0) {
				/* Error in the stream. Not a problem, just reporting it in case 
				 * we (the app) cares. In this case, we don't
				 */
			} 
			else {
				int   convsize = BUFFER_LEN;
				int   j,
				      i = 0,
				      k;
				int   bout = (readcount < convsize ? readcount : convsize);

				/* scale doubles to 8, 16, 24 or 32 bit signed ints 
				 * (host order) (unless float output)
				 * and apply ReplayGain scaling, etc. 
				 */
				sample_buffer = malloc(sizeof(double) * wg_opts->channels * bout);
				for(k = 0; k < wg_opts->channels; k++) {
					for(j = 0; j < bout; j++, i++) {
						Int64_t val;
						double Sum;

						if (!settings->no_offset) {
							if (settings->adc)
								pcm[k][j] -= album_dc_offset[k];
							else
								pcm[k][j] -= dc_offset[k];
						}

						pcm[k][j] *= scale;
						if (settings->limiter) {	/* hard 6dB limiting */
							if (pcm[k][j] < -0.5)
								pcm[k][j] = tanh((pcm[k][j] + 0.5) / (1-0.5)) * (1-0.5) - 0.5;
							else if (pcm[k][j] > 0.5)
								pcm[k][j] = tanh((pcm[k][j] - 0.5) / (1-0.5)) * (1-0.5) + 0.5;
						}
						if (wg_opts->format != WAV_FMT_FLOAT) {
							Sum = pcm[k][j]*2147483647.f;
							if (i > 31)
								i = 0;
							val = dither_output(settings->dithering, settings->shapingtype, i,
									    Sum, k, wg_opts->format);
							if (val > (Int64_t)wrap_prev_pos)
								val = (Int64_t)wrap_prev_pos;
							else if (val < (Int64_t)wrap_prev_neg)
								val = (Int64_t)wrap_prev_neg;
							pcm[k][j] = (double)val;
						}
						else {
							if (pcm[k][j] > wrap_prev_pos)
								pcm[k][j] = wrap_prev_pos;
							else if (pcm[k][j] < wrap_prev_neg)
								pcm[k][j] = wrap_prev_neg;
						}
					}
				}
				sample_buffer = output_to_PCM(pcm, sample_buffer, wg_opts->channels,
						bout, wg_opts->format);
				/* write to file */
				write_audio_file(aufile, sample_buffer, bout * wg_opts->channels);

				free(sample_buffer);
			}
		}
		for (i = 0; i < wg_opts->channels; i++)
			if (pcm[i]) free(pcm[i]);
		if (pcm) free(pcm);
		format->close_func(wg_opts->readdata);
		close_audio_file(infile, aufile, wg_opts);
		fclose(infile);

		if (!settings->std_out) {
			if (remove(filename) != 0) {
				fprintf(stderr, " Error deleting old file '%s'\n", filename);
				goto exit;
			}
    
			if (rename(tempName, filename) != 0) {
				fprintf(stderr, " Error renaming '%s' to '%s' (uh-oh)\n", tempName, filename);
				goto exit;
			}
		}
		result = 1;
	}
exit:
	return result;
}


