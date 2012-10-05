/* WaveGain - Filename: AUDIO.H
 *
 *   Copyright (c) 2002 - 2005 John Edwards <john.edwards33@ntlworld.com>
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Library General Public
 *   License as published by the Free Software Foundation; either
 *   version 2 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Portions Copyright 2000-2002, Michael Smith <msmith@labyrinth.net.au>
 *
 * AIFF/AIFC support from OggSquish, (c) 1994-1996 Monty <xiphmont@xiph.org>
 */


#ifndef AUDIO_H_INCLUDED
#define AUDIO_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "misc.h"

/* In WIN32, the following bitmap can be found in sdk\inc\ksmedia.h and sdk\inc\mmreg.h: */

#ifndef SPEAKER_FRONT_LEFT
#	define SPEAKER_FRONT_LEFT             0x1
#	define SPEAKER_FRONT_RIGHT            0x2
#endif

#ifndef	WAVE_FORMAT_PCM
#	define	WAVE_FORMAT_PCM                0x0001
#endif
#ifndef	WAVE_FORMAT_IEEE_FLOAT
#	define	WAVE_FORMAT_IEEE_FLOAT         0x0003
#endif
#ifndef	WAVE_FORMAT_EXTENSIBLE
#	define	WAVE_FORMAT_EXTENSIBLE         0xfffe
#endif

typedef long (*audio_read_func)(void *src,
                                double **buffer,
                                int samples,
                                int fast,
                                int chunk);

typedef struct
{
	audio_read_func read_samples;
	
	void *readdata;

	unsigned long total_samples_per_channel;
	int channels;
	long rate;
	int samplesize;
	int endianness;
	int format;
	int gain_chunk;
	double gain_scale;
	int std_in;
	int std_out;
	int apply_gain;
	int write_chunk;
	int force;
	int undo;
	int header_size;
	unsigned char *header;

	FILE *out;
	char *filename;
} wavegain_opt;

typedef struct
{
	int  (*id_func)(unsigned char *buf, int len); /* Returns true if can load file */
	int  id_data_len; /* Amount of data needed to id whether this can load the file */
	int  (*open_func)(FILE *in, wavegain_opt *opt, unsigned char *buf, int buflen);
	void (*close_func)(void *);
	char *format;
	char *description;
} input_format;


typedef struct {
	unsigned short format;
	unsigned short channels;
	unsigned int   channel_mask;
	unsigned int   samplerate;
	unsigned int   bytespersec;
	unsigned short align;
	unsigned short samplesize;
} wav_fmt;

typedef struct {
	short channels;
	short samplesize;
	unsigned long  totalsamples;
	unsigned long  samplesread;
	FILE  *f;
	short bigendian;
} wavfile;

typedef struct {
	short channels;
	unsigned long   totalframes;
	short samplesize;
	int   rate;
	int   offset;
	int   blocksize;
} aiff_fmt;

typedef wavfile aifffile; /* They're the same */

input_format *open_audio_file(FILE *in, wavegain_opt *opt);

int raw_open(FILE *in, wavegain_opt *opt);
int wav_open(FILE *in, wavegain_opt *opt, unsigned char *buf, int buflen);
int aiff_open(FILE *in, wavegain_opt *opt, unsigned char *buf, int buflen);
int wav_id(unsigned char *buf, int len);
int aiff_id(unsigned char *buf, int len);
void wav_close(void *);
void raw_close(void *);

long wav_read(void *, double **buffer, int samples, int fast, int chunk);
long wav_ieee_read(void *, double **buffer, int samples, int fast, int chunk);

enum {
	WAV_NO_FMT = 0,
	WAV_FMT_8BIT,
	WAV_FMT_16BIT,
	WAV_FMT_24BIT,
	WAV_FMT_32BIT,
	WAV_FMT_FLOAT,
	WAV_FMT_AIFF,
	WAV_FMT_AIFC8,
	WAV_FMT_AIFC16
} file_formats;

typedef struct
{
	int           outputFormat;
	FILE          *sndfile;
	unsigned long samplerate;
	unsigned long bits_per_sample;
	unsigned long channels;
	unsigned long samples;
	int           endianness;
	int           format;
} audio_file;

audio_file *open_output_audio_file(char *infile, wavegain_opt *opt);
int write_audio_file(audio_file *aufile, void *sample_buffer, int samples);
void close_audio_file(FILE *in, audio_file *aufile, wavegain_opt *opt);
static int write_wav_header(audio_file *aufile, wavegain_opt *opt, Int64_t file_size);
static int write_aiff_header(audio_file *aufile);
static int write_audio_8bit(audio_file *aufile, void *sample_buffer, unsigned int samples);
static int write_audio_16bit(audio_file *aufile, void *sample_buffer, unsigned int samples);
static int write_audio_24bit(audio_file *aufile, void *sample_buffer, unsigned int samples);
static int write_audio_32bit(audio_file *aufile, void *sample_buffer, unsigned int samples);
static int write_audio_float(audio_file *aufile, void *sample_buffer, unsigned int samples);
void* output_to_PCM(double **input, void *samplebuffer, int channels, int samples, int format);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_H_INCLUDED */
