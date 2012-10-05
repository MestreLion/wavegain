/* WaveGain - Filename: AUDIO.C
 *
 * Function: Essentially provides all the wave input and output routines.
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
 * Portions, (c) Michael Smith <msmith@labyrinth.net.au>
 * Portions from Vorbize, (c) Kenneth Arnold <kcarnold@yahoo.com>
 * and libvorbis examples, (c) Monty <monty@xiph.org>
 *
 * AIFF/AIFC support from OggSquish, (c) 1994-1996 Monty <xiphmont@xiph.org>
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <math.h>

#ifdef _WIN32
#include <io.h>
#else
# ifndef __MACOSX__
#  include <sys/io.h>
# endif
#endif

#include <fcntl.h>

#ifndef __MACOSX__
#include <malloc.h>
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "audio.h"
#include "i18n.h"
#include "misc.h"

/* Macros to read header data */
#define READ_U32_LE(buf) \
	(((buf)[3]<<24)|((buf)[2]<<16)|((buf)[1]<<8)|((buf)[0]&0xff))

#define READ_U16_LE(buf) \
	(((buf)[1]<<8)|((buf)[0]&0xff))

#define READ_U32_BE(buf) \
	(((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|((buf)[3]&0xff))

#define READ_U16_BE(buf) \
	(((buf)[0]<<8)|((buf)[1]&0xff))

#ifdef __MACOSX__
	#define READ_D64	read_d64_be
	#define WRITE_D64	write_d64_be
#else
	#define READ_D64	read_d64_le
	#define WRITE_D64	write_d64_le
#endif

static unsigned char pcm_guid[16] =
{
	/* (00000001-0000-0010-8000-00aa00389b71) */

	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};

static unsigned char ieee_float_guid[16] =
{
	/* (00000003-0000-0010-8000-00aa00389b71) */

	0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};

/* Define the supported formats here */
input_format formats[] = {
	{wav_id, 12, wav_open, wav_close, "wav", N_("WAV file reader")},
	{aiff_id, 12, aiff_open, wav_close, "aiff", N_("AIFF/AIFC file reader")},
	{NULL, 0, NULL, NULL, NULL, NULL}
};

#if (defined (WIN32) || defined (_WIN32))
extern int __cdecl _fseeki64(FILE *, Int64_t, int);
extern Int64_t __cdecl _ftelli64(FILE *);

	#define FSEEK64		_fseeki64
	#define FTELL64		_ftelli64
#else
	#define FSEEK64		fseeko
	#define FTELL64		ftello
#endif

/* Global */
Int64_t current_pos_t;

#if (defined (WIN32) || defined (_WIN32))
__inline long int lrint(double flt)
{
	int intgr;

	_asm {
		fld flt
		fistp intgr
	}

	return intgr;
}
#elif (defined (__MACOSX__))
#define lrint	double2int
inline static long
double2int (register double in)
{	int res [2] ;

	__asm__ __volatile__
	(	"fctiw	%1, %1\n\t"
		"stfd	%1, %0"
		: "=m" (res)	/* Output */
		: "f" (in)		/* Input */
		: "memory"
		) ;

	return res [1] ;
}
#else
#define	lrint	double2int
static inline long double2int (double in)
{	long retval ;

	__asm__ __volatile__
	(	"fistpl %0"
		: "=m" (retval)
		: "t" (in)
		: "st"
		) ;

	return retval ;
}
#endif

double read_d64_be(unsigned char *cptr)
{
	int	exponent, negative;
	double	dvalue;

	negative = (cptr [0] & 0x80) ? 1 : 0;
	exponent = ((cptr [0] & 0x7F) << 4) | ((cptr [1] >> 4) & 0xF);

	/* Might not have a 64 bit long, so load the mantissa into a double. */
	dvalue = (((cptr [1] & 0xF) << 24) | (cptr [2] << 16) | (cptr [3] << 8) | cptr [4]);
	dvalue += ((cptr [5] << 16) | (cptr [6] << 8) | cptr [7]) / ((double) 0x1000000);

	if (exponent == 0 && dvalue == 0.0)
		return 0.0;

	dvalue += 0x10000000;

	exponent = exponent - 0x3FF;

	dvalue = dvalue / ((double) 0x10000000);

	if (negative)
		dvalue *= -1;

	if (exponent > 0)
		dvalue *= (1 << exponent);
	else if (exponent < 0)
		dvalue /= (1 << abs (exponent));

	return dvalue;
}

double read_d64_le(unsigned char *cptr)
{
	int	exponent, negative;
	double	dvalue;

	negative = (cptr [7] & 0x80) ? 1 : 0;
	exponent = ((cptr [7] & 0x7F) << 4) | ((cptr [6] >> 4) & 0xF);

	/* Might not have a 64 bit long, so load the mantissa into a double. */
	dvalue = (((cptr [6] & 0xF) << 24) | (cptr [5] << 16) | (cptr [4] << 8) | cptr [3]);
	dvalue += ((cptr [2] << 16) | (cptr [1] << 8) | cptr [0]) / ((double) 0x1000000);

	if (exponent == 0 && dvalue == 0.0)
		return 0.0;

	dvalue += 0x10000000;

	exponent = exponent - 0x3FF;

	dvalue = dvalue / ((double) 0x10000000);

	if (negative)
		dvalue *= -1;

	if (exponent > 0)
		dvalue *= (1 << exponent);
	else if (exponent < 0)
		dvalue /= (1 << abs (exponent));

	return dvalue;
}

void write_d64_be(unsigned char *out, double in)
{
	int	exponent, mantissa;

	memset (out, 0, sizeof (double));

	if (in == 0.0)
		return;

	if (in < 0.0) {
		in *= -1.0;
		out [0] |= 0x80;
	}

	in = frexp (in, &exponent);

	exponent += 1022;

	out [0] |= (exponent >> 4) & 0x7F;
	out [1] |= (exponent << 4) & 0xF0;

	in *= 0x20000000;
	mantissa = lrint (floor (in));

	out [1] |= (mantissa >> 24) & 0xF;
	out [2] = (mantissa >> 16) & 0xFF;
	out [3] = (mantissa >> 8) & 0xFF;
	out [4] = mantissa & 0xFF;

	in = fmod (in, 1.0);
	in *= 0x1000000;
	mantissa = lrint (floor (in));

	out [5] = (mantissa >> 16) & 0xFF;
	out [6] = (mantissa >> 8) & 0xFF;
	out [7] = mantissa & 0xFF;

	return;
}

void write_d64_le(unsigned char *out, double in)
{
	int	exponent, mantissa;

	memset (out, 0, sizeof (double));

	if (in == 0.0)
		return;

	if (in < 0.0) {
		in *= -1.0;
		out [7] |= 0x80;
	}

	in = frexp (in, &exponent);

	exponent += 1022;

	out [7] |= (exponent >> 4) & 0x7F;
	out [6] |= (exponent << 4) & 0xF0;

	in *= 0x20000000;
	mantissa = lrint (floor (in));

	out [6] |= (mantissa >> 24) & 0xF;
	out [5] = (mantissa >> 16) & 0xFF;
	out [4] = (mantissa >> 8) & 0xFF;
	out [3] = mantissa & 0xFF;

	in = fmod (in, 1.0);
	in *= 0x1000000;
	mantissa = lrint (floor (in));

	out [2] = (mantissa >> 16) & 0xFF;
	out [1] = (mantissa >> 8) & 0xFF;
	out [0] = mantissa & 0xFF;

	return;
}

input_format *open_audio_file(FILE *in, wavegain_opt *opt)
{
	int j = 0;
	unsigned char *buf = NULL;
	int buf_size=0, buf_filled = 0;
	int size, ret;

	while (formats[j].id_func) {
		size = formats[j].id_data_len;
		if (size >= buf_size) {
			buf = realloc(buf, size);
			buf_size = size;
		}

		if (size > buf_filled) {
			ret = fread(buf+buf_filled, 1, buf_size-buf_filled, in);
			buf_filled += ret;

			if (buf_filled < size) {
				/* File truncated */
				j++;
				continue;
			}
		}

		if (formats[j].id_func(buf, buf_filled)) {
			/* ok, we now have something that can handle the file */
			if (formats[j].open_func(in, opt, buf, buf_filled)) {
				free(buf);
				return &formats[j];
			}
		}
		j++;
	}

	free(buf);

	return NULL;
}

static int seek_forward(FILE *in, Int64_t length)
{
	if (FSEEK64(in, length, SEEK_CUR)) {
		/* Failed. Do it the hard way. */
		unsigned char buf[1024];
		int seek_needed = length, seeked;
		while (seek_needed > 0) {
			seeked = fread(buf, 1, seek_needed > 1024 ? 1024:seek_needed, in);
			if (!seeked)
				return 0; /* Couldn't read more, can't read file */
			else
				seek_needed -= seeked;
		}
	}
	return 1;
}


static int find_wav_chunk(FILE *in, char *type, Int64_t *len)
{
	unsigned char buf[8];

	while (1) {
		if (fread(buf,1,8,in) < 8) {
			/* Suck down a chunk specifier */
			if (memcmp(type, "gain", 4))
				fprintf(stderr, "Warning: Unexpected EOF in reading WAV header (1)\n");
			return 0; /* EOF before reaching the appropriate chunk */
		}

		if (memcmp(buf, type, 4)) {
			*len = READ_U32_LE(buf+4);
			if (!seek_forward(in, *len))
				return 0;

			buf[4] = 0;
		}
		else {
			*len = READ_U32_LE(buf+4);
			return 1;
		}
	}
}

static int find_gain_chunk(FILE *in, Int64_t *len)
{
	unsigned char buf[8];

	if (fread(buf,1,8,in) < 8) {
		/* Suck down a chunk specifier */
			fprintf(stderr, "Warning: Unexpected EOF in reading WAV header (3)\n");
		return 0; /* EOF before reaching the appropriate chunk */
	}

	if (!memcmp(buf, "gain", 4)) {
		*len = READ_U32_LE(buf+4);
		return 1;
	}
	else {
		return 0;
	}
}

static int find_aiff_chunk(FILE *in, char *type, unsigned int *len)
{
	unsigned char buf[8];

	while (1) {
		if (fread(buf,1,8,in) <8) {
			fprintf(stderr, "Warning: Unexpected EOF in AIFF chunk\n");
			return 0;
		}

		*len = READ_U32_BE(buf+4);

		if (memcmp(buf,type,4)) {
			if ((*len) & 0x1)
				(*len)++;

			if (!seek_forward(in, *len))
				return 0;
		}
		else
			return 1;
	}
}



double read_IEEE80(unsigned char *buf)
{
	int s = buf[0] & 0xff;
	int e = ((buf[0] & 0x7f) <<8) | (buf[1] & 0xff);
	double f = ((unsigned long)(buf[2] & 0xff) << 24)|
		((buf[3] & 0xff) << 16)|
		((buf[4] & 0xff) << 8) |
		 (buf[5] & 0xff);

	if (e == 32767) {
		if (buf[2] & 0x80)
			return HUGE_VAL; /* Really NaN, but this won't happen in reality */
		else {
			if (s)
				return -HUGE_VAL;
			else
				return HUGE_VAL;
		}
	}

	f = ldexp(f, 32);
	f += ((buf[6] & 0xff) << 24)|
		((buf[7] & 0xff) << 16)|
		((buf[8] & 0xff) << 8) |
		 (buf[9] & 0xff);

	return ldexp(f, e-16446);
}

/* AIFF/AIFC support adapted from the old OggSQUISH application */
int aiff_id(unsigned char *buf, int len)
{
	if (len < 12) return 0; /* Truncated file, probably */

	if (memcmp(buf, "FORM", 4))
		return 0;

	if (memcmp(buf + 8, "AIF",3))
		return 0;

	if (buf[11] != 'C' && buf[11] != 'F')
		return 0;

	return 1;
}

int aiff_open(FILE *in, wavegain_opt *opt, unsigned char *buf, int buflen)
{
	int aifc; /* AIFC or AIFF? */
	unsigned int len;
	unsigned char *buffer;
	unsigned char buf2[8];
	aiff_fmt format;
	aifffile *aiff = malloc(sizeof(aifffile));

	if (buf[11] == 'C')
		aifc = 1;
	else {
		aifc = 0;
		opt->format = WAV_FMT_AIFF;
	}

	if (!find_aiff_chunk(in, "COMM", &len)) {
		fprintf(stderr, "Warning: No common chunk found in AIFF file\n");
		return 0; /* EOF before COMM chunk */
	}

	if (len < 18) {
		fprintf(stderr, "Warning: Truncated common chunk in AIFF header\n");
		return 0; /* Weird common chunk */
	}

	buffer = alloca(len);

	if (fread(buffer, 1, len, in) < len) {
		fprintf(stderr, "Warning: Unexpected EOF in reading AIFF header\n");
		return 0;
	}

	format.channels = READ_U16_BE(buffer);
	format.totalframes = READ_U32_BE(buffer + 2);
	format.samplesize = READ_U16_BE(buffer + 6);
	format.rate = (int)read_IEEE80(buffer + 8);

	aiff->bigendian = BIG;
	opt->endianness = BIG;

	if (aifc) {
		if (len < 22) {
			fprintf(stderr, "Warning: AIFF-C header truncated.\n");
			return 0;
		}

		if (!memcmp(buffer + 18, "NONE", 4)) {
			aiff->bigendian = BIG;
			opt->endianness = BIG;
		}
		else if (!memcmp(buffer + 18, "sowt", 4)) {
			aiff->bigendian = LITTLE;
			opt->endianness = LITTLE;
		}
		else {
			fprintf(stderr, "Warning: Can't handle compressed AIFF-C (%c%c%c%c)\n", *(buffer+18), *(buffer+19), *(buffer+20), *(buffer+21));
			return 0; /* Compressed. Can't handle */
		}
	}

	if (!find_aiff_chunk(in, "SSND", &len)) {
		fprintf(stderr, "Warning: No SSND chunk found in AIFF file\n");
		return 0; /* No SSND chunk -> no actual audio */
	}

	if (len < 8) {
		fprintf(stderr, "Warning: Corrupted SSND chunk in AIFF header\n");
		return 0; 
	}

	if (fread(buf2, 1, 8, in) < 8) {
		fprintf(stderr, "Warning: Unexpected EOF reading AIFF header\n");
		return 0;
	}

	format.offset = READ_U32_BE(buf2);
	format.blocksize = READ_U32_BE(buf2+4);

	if ( format.blocksize == 0 && (format.samplesize == 16 || format.samplesize == 8)) {
		/* From here on, this is very similar to the wav code. Oh well. */
		
		opt->rate = format.rate;
		opt->channels = format.channels;
		opt->read_samples = wav_read; /* Similar enough, so we use the same */
		opt->total_samples_per_channel = format.totalframes;
		opt->samplesize = format.samplesize;
		if (aifc && format.samplesize == 8)
			opt->format = WAV_FMT_AIFC8;
		else if (aifc && format.samplesize == 16)
			opt->format = WAV_FMT_AIFC16;
		opt->header_size = 0;
		opt->header = NULL;
		opt->rate = format.rate;
		aiff->f = in;
		aiff->samplesread = 0;
		aiff->channels = format.channels;
		aiff->samplesize = format.samplesize;
		aiff->totalsamples = format.totalframes;

		opt->readdata = (void *)aiff;

		seek_forward(in, format.offset); /* Swallow some data */
		return 1;
	}
	else {
		fprintf(stderr, "Warning: WaveGain does not support this type of AIFF/AIFC file\n"
				" Must be 8 or 16 bit PCM.\n");
		return 0;
	}
}

int wav_id(unsigned char *buf, int len)
{
	unsigned int flen;
	
	if (len < 12) return 0; /* Something screwed up */

	if (memcmp(buf, "RIFF", 4))
		return 0; /* Not wave */

	flen = READ_U32_LE(buf + 4); /* We don't use this */

	if (memcmp(buf + 8, "WAVE",4))
		return 0; /* RIFF, but not wave */

	return 1;
}

int wav_open(FILE *in, wavegain_opt *opt, unsigned char *oldbuf, int buflen)
{
	unsigned char buf[81];
	Int64_t len;
	Int64_t current_pos;
	int samplesize;
	wav_fmt format;
	wavfile *wav = malloc(sizeof(wavfile));

	/* Ok. At this point, we know we have a WAV file. Now we have to detect
	 * whether we support the subtype, and we have to find the actual data
	 * We don't (for the wav reader) need to use the buffer we used to id this
	 * as a wav file (oldbuf)
	 */

	if (!find_wav_chunk(in, "fmt ", &len)) {
		fprintf(stderr, "Warning: Failed to find fmt chunk in reading WAV header\n");
		return 0; /* EOF */
	}

	if (len < 16) {
		fprintf(stderr, "Warning: Unrecognised format chunk in WAV header\n");
		return 0; /* Weird format chunk */
	}

	/* A common error is to have a format chunk that is not 16 or 18 bytes
	 * in size.  This is incorrect, but not fatal, so we only warn about 
	 * it instead of refusing to work with the file.  Please, if you
	 * have a program that's creating format chunks of sizes other than
	 * 16, 18 or 40 bytes in size, report a bug to the author.
	 * (40 bytes accommodates WAVEFORMATEXTENSIBLE conforming files.)
	 */
	if (len != 16 && len != 18 && len != 40)
		fprintf(stderr, "Warning: INVALID format chunk in wav header.\n"
				" Trying to read anyway (may not work)...\n");

	/* Deal with stupid broken apps. Don't use these programs.
	 */
	
	if (fread(buf,1,len,in) < len) {
		fprintf(stderr, "Warning: Unexpected EOF in reading WAV header\n");
		return 0;
	}

	format.format =      READ_U16_LE(buf); 
	format.channels =    READ_U16_LE(buf+2); 
	format.samplerate =  READ_U32_LE(buf+4);
	format.bytespersec = READ_U32_LE(buf+8);
	format.align =       READ_U16_LE(buf+12);
	format.samplesize =  READ_U16_LE(buf+14);

	if (!opt->std_in) {
		current_pos = FTELL64(in);
		if (!find_gain_chunk(in, &len))
			FSEEK64(in, current_pos, SEEK_SET);
		else {
			char buf_double[8];
			opt->gain_chunk = 1;
			fread(buf_double, 1, 8, in);
			opt->gain_scale = READ_D64(buf_double);
		}
	}

	if (!find_wav_chunk(in, "data", &len)) {
		fprintf(stderr, "Warning: Failed to find data chunk in reading WAV header\n");
		return 0; /* EOF */
	}

	if (opt->apply_gain) {
		current_pos = FTELL64(in);
		current_pos_t = current_pos + len;
		FSEEK64(in, 0, SEEK_SET);
		if ((opt->header = malloc(sizeof(char) * current_pos)) == NULL)
			fprintf(stderr, "Error: unable to allocate memory for header\n");
		else {
			opt->header_size = current_pos;
			fread(opt->header, 1, opt->header_size, in);
		}
		FSEEK64(in, current_pos, SEEK_SET);
	}

	if(format.format == WAVE_FORMAT_PCM) {
		samplesize = format.samplesize/8;
		opt->read_samples = wav_read;
		/* works with current enum */
		opt->format = samplesize;
	}
	else if(format.format == WAVE_FORMAT_IEEE_FLOAT) {
		samplesize = 4;
		opt->read_samples = wav_ieee_read;
		opt->endianness = LITTLE;
		opt->format = WAV_FMT_FLOAT;
	}
	else if (format.format == WAVE_FORMAT_EXTENSIBLE) {
		format.channel_mask = READ_U32_LE(buf+20);
		if (format.channel_mask > 3) {
			fprintf(stderr, "ERROR: Wav file is unsupported type (must be standard 1 or 2 channel PCM\n"
					" or type 3 floating point PCM)(2)\n");
			return 0;
		}	
		if (memcmp(buf+24, pcm_guid, 16) == 0) {
			samplesize = format.samplesize/8;
			opt->read_samples = wav_read;
			/* works with current enum */
			opt->format = samplesize;
		}
		else if (memcmp(buf+24, ieee_float_guid, 16) == 0) {
			samplesize = 4;
			opt->read_samples = wav_ieee_read;
			opt->endianness = LITTLE;
			opt->format = WAV_FMT_FLOAT;
		}
		else {
			fprintf(stderr, "ERROR: Wav file is unsupported type (must be standard PCM\n"
					" or type 3 floating point PCM)(2)\n");
			return 0;
		}
	}
	else {
		fprintf(stderr, "ERROR: Wav file is unsupported type (must be standard PCM\n"
				" or type 3 floating point PCM\n");
		return 0;
	}

	opt->samplesize = format.samplesize;

	if(format.align != format.channels * samplesize) {
		/* This is incorrect according to the spec. Warn loudly, then ignore
		 * this value.
		 */
		fprintf(stderr, _("Warning: WAV 'block alignment' value is incorrect, ignoring.\n" 
		    "The software that created this file is incorrect.\n"));
	}
	if ( format.align == format.channels * samplesize &&
			format.samplesize == samplesize * 8 && 
			(format.samplesize == 32 || format.samplesize == 24 ||
			 format.samplesize == 16 || format.samplesize == 8)) {
		/* OK, good - we have the one supported format,
		   now we want to find the size of the file */
		opt->rate = format.samplerate;
		opt->channels = format.channels;

		wav->f = in;
		wav->samplesread = 0;
		wav->bigendian = 0;
		opt->endianness = LITTLE;
		wav->channels = format.channels; /* This is in several places. The price
							of trying to abstract stuff. */
		wav->samplesize = format.samplesize;

		if (len)
			opt->total_samples_per_channel = len/(format.channels*samplesize);
		else {
			Int64_t pos;
			pos = FTELL64(in);
			if (FSEEK64(in, 0, SEEK_END) == -1)
				opt->total_samples_per_channel = 0; /* Give up */
			else {
				opt->total_samples_per_channel = (FTELL64(in) - pos)/(format.channels * samplesize);
				FSEEK64(in, pos, SEEK_SET);
			}
		}
		wav->totalsamples = opt->total_samples_per_channel;

		opt->readdata = (void *)wav;
		return 1;
	}
	else {
		fprintf(stderr, "ERROR: Wav file is unsupported subformat (must be 8, 16, 24 or 32 bit PCM\n"
				"or floating point PCM)\n");
		return 0;
	}
}

long wav_read(void *in, double **buffer, int samples, int fast, int chunk)
{
	wavfile *f = (wavfile *)in;
	int sampbyte = f->samplesize / 8;
	signed char *buf = alloca(samples*sampbyte*f->channels);
	long bytes_read;
	int i, j;
	long realsamples;

	if (fast) {
		chunk /= (sampbyte * f->channels);
		chunk *= (sampbyte * f->channels);
		FSEEK64(f->f, chunk, SEEK_SET);
	}

	bytes_read = fread(buf, 1, samples * sampbyte * f->channels, f->f);

	if (f->totalsamples && f->samplesread + bytes_read / (sampbyte * f->channels) > f->totalsamples) {
		bytes_read = sampbyte * f->channels * (f->totalsamples - f->samplesread);
	}

	realsamples = bytes_read / (sampbyte * f->channels);
	f->samplesread += realsamples;
		
	if (f->samplesize == 8) {
		unsigned char *bufu = (unsigned char *)buf;
		for (i = 0; i < realsamples; i++) {
			for (j = 0; j < f->channels; j++)
				buffer[j][i] = ((int)(bufu[i * f->channels + j]) - 128) / 128.0;
		}
	}
	else if (f->samplesize==16) {
#ifdef __MACOSX__
		if (f->bigendian != machine_endianness) {
#else
		if (f->bigendian == machine_endianness) {
#endif
			for (i = 0; i < realsamples; i++) {
				for (j = 0; j < f->channels; j++)
					buffer[j][i] = ((buf[i * 2 * f->channels + 2 * j + 1] <<8) |
							        (buf[i * 2 * f->channels + 2 * j] & 0xff)) / 32768.0;
			}
		}
		else {
			for (i = 0; i < realsamples; i++) {
				for (j = 0; j < f->channels; j++)
					buffer[j][i]=((buf[i * 2 * f->channels + 2 * j] << 8) |
							      (buf[i * 2 * f->channels + 2 * j + 1] & 0xff)) / 32768.0;
			}
		}
	}
	else if (f->samplesize == 24) {
#ifdef __MACOSX__
		if (f->bigendian != machine_endianness) {
#else
		if (f->bigendian == machine_endianness) {
#endif
			for (i = 0; i < realsamples; i++) {
				for (j = 0; j < f->channels; j++) 
					buffer[j][i] = ((buf[i * 3 * f->channels + 3 * j + 2] << 16) |
						(((unsigned char *)buf)[i * 3 * f->channels + 3 * j + 1] << 8) |
						(((unsigned char *)buf)[i * 3 * f->channels + 3 * j] & 0xff)) 
						/ 8388608.0;
			}
		}
		else {
			fprintf(stderr, "Big endian 24 bit PCM data is not currently "
					"supported, aborting.\n");
			return 0;
		}
	}
	else if (f->samplesize == 32) {
#ifdef __MACOSX__
		if (f->bigendian != machine_endianness) {
#else
		if (f->bigendian == machine_endianness) {
#endif
			for (i = 0; i < realsamples; i++) {
				for (j = 0; j < f->channels; j++)
					buffer[j][i] = ((buf[i * 4 * f->channels + 4 * j + 3] << 24) |
						(((unsigned char *)buf)[i * 4 * f->channels + 4 * j + 2] << 16) |
						(((unsigned char *)buf)[i * 4 * f->channels + 4 * j + 1] << 8) |
						(((unsigned char *)buf)[i * 4 * f->channels + 4 * j] & 0xff)) 
						/ 2147483648.0;
			}
		}
		else {
			fprintf(stderr, "Big endian 32 bit PCM data is not currently "
					"supported, aborting.\n");
			return 0;
		}
	}
	else {
		fprintf(stderr, "Internal error: attempt to read unsupported "
				"bitdepth %d\n", f->samplesize);
		return 0;
	}

	return realsamples;
}

long wav_ieee_read(void *in, double **buffer, int samples, int fast, int chunk)
{
	wavfile *f = (wavfile *)in;
	float *buf = alloca(samples * 4 * f->channels); /* de-interleave buffer */
	long bytes_read;
	int i,j;
	long realsamples;

	if (fast) {
		chunk /= (sizeof(float) * f->channels);
		chunk *= (sizeof(float) * f->channels);
		FSEEK64(f->f, chunk, SEEK_SET);
	}

	bytes_read = fread(buf, 1, samples * 4 * f->channels, f->f);

	if (f->totalsamples && f->samplesread + bytes_read / (4 * f->channels) > f->totalsamples)
		bytes_read = 4 * f->channels * (f->totalsamples - f->samplesread);
	realsamples = bytes_read / (4 * f->channels);
	f->samplesread += realsamples;

	for (i = 0; i < realsamples; i++)
		for (j = 0; j < f->channels; j++)
			buffer[j][i] = buf[i * f->channels + j];

	return realsamples;
}


void wav_close(void *info)
{
	wavfile *f = (wavfile *)info;

	free(f);
}

int raw_open(FILE *in, wavegain_opt *opt)
{
	wav_fmt format; /* fake wave header ;) */
	wavfile *wav = malloc(sizeof(wavfile));

	/* construct fake wav header ;) */
	format.format =      2; 
	format.channels =    opt->channels;
	format.samplerate =  opt->rate;
	format.samplesize =  opt->samplesize;
	format.bytespersec = opt->channels * opt->rate * opt->samplesize / 8;
	format.align =       format.bytespersec;
	wav->f =             in;
	wav->samplesread =   0;
	wav->bigendian =     opt->endianness;
	wav->channels =      format.channels;
	wav->samplesize =    opt->samplesize;
	wav->totalsamples =  0;

	opt->read_samples = wav_read;
	opt->readdata = (void *)wav;
	opt->total_samples_per_channel = 0; /* raw mode, don't bother */
	return 1;
}

/*
 * W A V E   O U T P U T
 */

audio_file *open_output_audio_file(char *infile, wavegain_opt *opt)
{
	audio_file *aufile = malloc(sizeof(audio_file));

	aufile->outputFormat = opt->format;

	aufile->samplerate = opt->rate;
	aufile->channels = opt->channels;
	aufile->samples = 0;
	aufile->endianness = opt->endianness;
	aufile->bits_per_sample = opt->samplesize;

	if (opt->std_out) {
		aufile->sndfile = stdout;
#ifdef _WIN32
		_setmode( _fileno(stdout), _O_BINARY );
#endif
	}
	else
		aufile->sndfile = fopen(infile, "wb");

	if (aufile->sndfile == NULL) {
		if (aufile)
			free(aufile);
		return NULL;
	}

	switch (aufile->outputFormat) {
		case WAV_FMT_AIFF:
			write_aiff_header(aufile);
			break;
		case WAV_FMT_8BIT:
		case WAV_FMT_16BIT:
		case WAV_FMT_24BIT:
		case WAV_FMT_32BIT:
		case WAV_FMT_FLOAT: {
			unsigned int file_size = 0xffffffff;
			write_wav_header(aufile, opt, file_size);
			}
			break;
	}

	return aufile;
}

int write_audio_file(audio_file *aufile, void *sample_buffer, int samples)
{
	switch (aufile->outputFormat) {
		case WAV_FMT_8BIT:
			return write_audio_8bit(aufile, sample_buffer, samples);
		case WAV_FMT_16BIT:
		case WAV_FMT_AIFF:
			return write_audio_16bit(aufile, sample_buffer, samples);
		case WAV_FMT_24BIT:
			return write_audio_24bit(aufile, sample_buffer, samples);
		case WAV_FMT_32BIT:
			return write_audio_32bit(aufile, sample_buffer, samples);
		case WAV_FMT_FLOAT:
			return write_audio_float(aufile, sample_buffer, samples);
		default:
			return 0;
	}

	return 0;
}

void close_audio_file( FILE *in, audio_file *aufile, wavegain_opt *opt)
{
	unsigned char *ch;
	Int64_t pos;

	if (!opt->std_out) {

		switch (aufile->outputFormat) {
			case WAV_FMT_AIFF:
				FSEEK64(aufile->sndfile, 0, SEEK_SET);
				write_aiff_header(aufile);
				break;
			case WAV_FMT_8BIT:
			case WAV_FMT_16BIT:
			case WAV_FMT_24BIT:
			case WAV_FMT_32BIT:
			case WAV_FMT_FLOAT: {
				FSEEK64(in, 0, SEEK_END);
				pos = FTELL64 (in);
				if ((pos - current_pos_t) > 0) {
					FSEEK64 (in, current_pos_t, SEEK_SET);
					ch = malloc (sizeof(char) * (pos - current_pos_t));

					fread (ch, 1, pos - current_pos_t, in);
					fwrite (ch, pos - current_pos_t, 1, aufile->sndfile);

					if (ch)
						free (ch);
				}
				FSEEK64(aufile->sndfile, 0, SEEK_END);
				pos = FTELL64 (aufile->sndfile);
				FSEEK64(aufile->sndfile, 0, SEEK_SET);
				write_wav_header(aufile, opt, pos - 8);
				break;
			}
		}
	}

	if(opt->header)
		free(opt->header);
	if(opt)
		free(opt);

	fclose(aufile->sndfile);

	if (aufile)
		free(aufile);
}

#define WRITE_U32(buf, x) *(buf)     = (unsigned char)((x)&0xff);\
                          *((buf)+1) = (unsigned char)(((x)>>8)&0xff);\
                          *((buf)+2) = (unsigned char)(((x)>>16)&0xff);\
                          *((buf)+3) = (unsigned char)(((x)>>24)&0xff);

#define WRITE_U16(buf, x) *(buf)     = (unsigned char)((x)&0xff);\
                          *((buf)+1) = (unsigned char)(((x)>>8)&0xff);

static int write_wav_header(audio_file *aufile, wavegain_opt *opt, Int64_t file_size)
{
	unsigned short channels    = opt->channels;
	unsigned long samplerate   = opt->rate;
	unsigned long bytespersec  = opt->channels * opt->rate * opt->samplesize / 8;
	unsigned short align       = opt->channels * opt->samplesize / 8;
	unsigned short samplesize  = opt->samplesize;
	unsigned long size         = file_size;	
	unsigned long data_size    = aufile->samples * (opt->samplesize / 8) < 0xffffffff ?
					aufile->samples * (opt->samplesize / 8) : 0xffffffff;	

	unsigned long sz_fmt;
	unsigned char *p = opt->header;
	unsigned char *q;
	unsigned char *r;
//	unsigned char buf[8];
	unsigned long chunks_to_process;
	unsigned long no_of_chunks_processed = 0;

	if ((opt->force || opt->undo) && opt->gain_chunk)
		chunks_to_process = 3;
	else
		chunks_to_process = 2;

	for (;;) {
		if (!memcmp(p, "RIFF", 4)) {
			p += 4;
			WRITE_U32(p, size);
			p += 4;
			no_of_chunks_processed++;
		}
		if (!memcmp(p, "fmt ", 4)) {
			unsigned long fmt_length = 0;
			p += 4;
			sz_fmt = READ_U32_LE(p);
			p += 4;
			if (aufile->outputFormat == WAV_FMT_FLOAT) {
				WRITE_U16(p, 3);
			}
			else {
				WRITE_U16(p, 1);
			}
			p += 2;
			fmt_length += 2;
			WRITE_U16(p, channels);
			p += 2;
			fmt_length += 2;
			WRITE_U32(p, samplerate);
			p += 4;
			fmt_length += 4;
			WRITE_U32(p, bytespersec);
			p += 4;
			fmt_length += 4;
			WRITE_U16(p, align);
			p += 2;
			fmt_length += 2;
			WRITE_U16(p, samplesize);
			p += 2;
			fmt_length += 2;
			p += sz_fmt - fmt_length;
			no_of_chunks_processed++;
		}
		if (chunks_to_process == 3) {
			if (!memcmp(p, "gain", 4)) {
				p += 8;
				WRITE_D64(p, opt->gain_scale);
				p += 8;
				no_of_chunks_processed++;
			}
		}
		if (no_of_chunks_processed == chunks_to_process)
			break;
		else
			p++;
	}

	if (opt->write_chunk == 1 && !opt->gain_chunk) {
		if ((q = malloc(sizeof(char) * (opt->header_size + 16))) == NULL)
			fprintf(stderr, "Error: unable to allocate memory for header\n");
		else {
			r = q;
			memcpy(r, opt->header, p - opt->header);
			r += (p - opt->header);
			memcpy(r, "gain", 4);
			r += 4;
			WRITE_U32(r, 8);
			r += 4;
//			buf = (unsigned char *)&opt->gain_scale;
			WRITE_D64(r, opt->gain_scale);
			r += 8;
		}

		memcpy(r, p, (opt->header_size - (p - opt->header)));

		r = q + opt->header_size + 16 - 8;
		if (!memcmp(r, "data", 4)) {
			r += 4;
			WRITE_U32(r, data_size);
			r += 4;
			fwrite(q, opt->header_size + 16, 1, aufile->sndfile);
		}
		else
			fprintf(stderr, "Error: unable to write header\n");
		if(q) free(q);
	}
	else {
		p = opt->header + opt->header_size - 8;
		if (!memcmp(p, "data", 4)) {
			p += 4;
			WRITE_U32(p, data_size);
			p += 4;
			fwrite(opt->header, opt->header_size, 1, aufile->sndfile);
		}
	}

	return 1;
}

/*
 *  Write a 80 bit IEEE854 big endian number as 10 octets. Destination is passed as pointer,
 *  End of destination (p+10) is returned.
 */

static unsigned char* Convert_to_80bit_BE_IEEE854_Float(unsigned char* p, long double val )
{
    unsigned long  word32 = 0x401E;

    if (val > 0.L)
        while (val < (long double)0x80000000)                    // scales value in the range 2^31...2^32
            word32--, val *= 2.L;                              // so you have the exponent

    *p++   = (unsigned char)(word32 >>  8);
    *p++   = (unsigned char)(word32 >>  0);                          // write exponent, sign is assumed as '+'
    word32 = (unsigned long) val;
    *p++   = (unsigned char)(word32 >> 24);
    *p++   = (unsigned char)(word32 >> 16);
    *p++   = (unsigned char)(word32 >>  8);
    *p++   = (unsigned char)(word32 >>  0);                          // write the upper 32 bit of the mantissa
    word32 = (unsigned long) ((val - word32) * 4294967296.L);
    *p++   = (unsigned char)(word32 >> 24);
    *p++   = (unsigned char)(word32 >> 16);
    *p++   = (unsigned char)(word32 >>  8);
    *p++   = (unsigned char)(word32 >>  0);                          // write the lower 32 bit of the mantissa

    return p;
}

static int write_aiff_header(audio_file *aufile)
{
	unsigned char   header[54],
	                        *p = header;
	unsigned int         bytes = (aufile->bits_per_sample + 7) / 8;
	unsigned long    data_size = aufile->samples * bytes;
	unsigned long       word32;

	// FORM chunk
	*p++ = 'F';
	*p++ = 'O';
	*p++ = 'R';
	*p++ = 'M';

	word32 = data_size + 0x2E;  // size of the AIFF chunk
	*p++ = (unsigned char)(word32 >> 24);
	*p++ = (unsigned char)(word32 >> 16);
	*p++ = (unsigned char)(word32 >>  8);
	*p++ = (unsigned char)(word32 >>  0);

	*p++ = 'A';
	*p++ = 'I';
	*p++ = 'F';
	*p++ = 'F';
	// end of FORM chunk

	// COMM chunk
	*p++ = 'C';
	*p++ = 'O';
	*p++ = 'M';
	*p++ = 'M';

	word32 = 0x12;                             // size of this chunk
	*p++ = (unsigned char)(word32 >> 24);
	*p++ = (unsigned char)(word32 >> 16);
	*p++ = (unsigned char)(word32 >>  8);
	*p++ = (unsigned char)(word32 >>  0);

	word32 = aufile->channels;                 // channels
	*p++ = (unsigned char)(word32 >>  8);
	*p++ = (unsigned char)(word32 >>  0);

	word32 = aufile->samples / aufile->channels;       // no. of sample frames
	*p++ = (unsigned char)(word32 >> 24);
	*p++ = (unsigned char)(word32 >> 16);
	*p++ = (unsigned char)(word32 >>  8);
	*p++ = (unsigned char)(word32 >>  0);

	word32 = aufile->bits_per_sample;           // bits
	*p++ = (unsigned char)(word32 >>  8);
	*p++ = (unsigned char)(word32 >>  0);

	p = Convert_to_80bit_BE_IEEE854_Float (p, (long double)aufile->samplerate);  // sample frequency as big endian 80 bit IEEE854 float
	// End of COMM chunk

	// SSND chunk
	*p++ = 'S';
	*p++ = 'S';
	*p++ = 'N';
	*p++ = 'D';

	word32 = data_size + 0x08;  // chunk length
	*p++ = (unsigned char)(word32 >> 24);
	*p++ = (unsigned char)(word32 >> 16);
	*p++ = (unsigned char)(word32 >>  8);
	*p++ = (unsigned char)(word32 >>  0);

	*p++ = 0;                                  // offset
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;

	*p++ = 0;                                  // block size
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;

	return fwrite(header, sizeof(header), 1, aufile->sndfile);
}

static int write_audio_8bit(audio_file *aufile, void *sample_buffer, unsigned int samples)
{
	int              ret;
	unsigned int     i;
	unsigned char    *sample_buffer8 = (unsigned char*)sample_buffer;
	unsigned char    *data = malloc(samples*aufile->bits_per_sample*sizeof(char)/8);

	aufile->samples += samples;

	for (i = 0; i < samples; i++)
		data[i] = (sample_buffer8[i]+128) & 0xFF;

	ret = fwrite(data, samples*aufile->bits_per_sample/8, 1, aufile->sndfile);

	if (data)
		free(data);

	return ret;
}

static int write_audio_16bit(audio_file *aufile, void *sample_buffer, unsigned int samples)
{
	int          ret;
	unsigned int i;
	short        *sample_buffer16 = (short*)sample_buffer;
	char         *data = malloc(samples*aufile->bits_per_sample*sizeof(char)/8);

	aufile->samples += samples;

#ifdef __MACOSX__
		if (aufile->endianness != machine_endianness) {
#else
		if (aufile->endianness == machine_endianness) {
#endif
		for (i = 0; i < samples; i++) {
			data[i*2] = (char)(sample_buffer16[i] & 0xFF);
			data[i*2+1] = (char)((sample_buffer16[i] >> 8) & 0xFF);
		}
	}
	else {
		for (i = 0; i < samples; i++) {
			data[i*2+1] = (char)(sample_buffer16[i] & 0xFF);
			data[i*2] = (char)((sample_buffer16[i] >> 8) & 0xFF);
		}
	}

	ret = fwrite(data, samples*aufile->bits_per_sample/8, 1, aufile->sndfile);

	if (data)
		free(data);

	return ret;
}

static int write_audio_24bit(audio_file *aufile, void *sample_buffer, unsigned int samples)
{
	int          ret;
	unsigned int i;
	long         *sample_buffer24 = (long*)sample_buffer;
	char         *data = malloc(samples*aufile->bits_per_sample*sizeof(char)/8);

	aufile->samples += samples;

	for (i = 0; i < samples; i++) {
		data[i*3] = (char)(sample_buffer24[i] & 0xFF);
		data[i*3+1] = (char)((sample_buffer24[i] >> 8) & 0xFF);
		data[i*3+2] = (char)((sample_buffer24[i] >> 16) & 0xFF);
	}

	ret = fwrite(data, samples*aufile->bits_per_sample/8, 1, aufile->sndfile);

	if (data)
		free(data);

	return ret;
}

static int write_audio_32bit(audio_file *aufile, void *sample_buffer, unsigned int samples)
{
	int          ret;
	unsigned int i;
	long         *sample_buffer32 = (long*)sample_buffer;
	char         *data = malloc(samples*aufile->bits_per_sample*sizeof(char)/8);

	aufile->samples += samples;

	for (i = 0; i < samples; i++) {
		data[i*4] = (char)(sample_buffer32[i] & 0xFF);
		data[i*4+1] = (char)((sample_buffer32[i] >> 8) & 0xFF);
		data[i*4+2] = (char)((sample_buffer32[i] >> 16) & 0xFF);
		data[i*4+3] = (char)((sample_buffer32[i] >> 24) & 0xFF);
	}

	ret = fwrite(data, samples*aufile->bits_per_sample/8, 1, aufile->sndfile);

	if (data)
		free(data);

	return ret;
}

static int write_audio_float(audio_file *aufile, void *sample_buffer, unsigned int samples)
{
	int           ret;
	unsigned int  i;
	float         *sample_buffer_f = (float*)sample_buffer;
	unsigned char *data = malloc(samples*aufile->bits_per_sample*sizeof(char)/8);

	aufile->samples += samples;

	for (i = 0; i < samples; i++) {
		int   exponent,
		      mantissa,
		      negative = 0;
		float in = sample_buffer_f[i];

		data[i*4] = 0;
		data[i*4+1] = 0;
		data[i*4+2] = 0;
		data[i*4+3] = 0;
		if (in == 0.f)
			continue;

		if (in < 0.0) {
			in *= -1.0;
			negative = 1;
		}
		in = (float)frexp(in, &exponent);
		exponent += 126;
		in *= (float)0x1000000;
		mantissa = (((int)in) & 0x7FFFFF);

		if (negative)
			data[i*4+3] |= 0x80;

		if (exponent & 0x01)
			data[i*4+2] |= 0x80;

		data[i*4] = mantissa & 0xFF;
		data[i*4+1] = (mantissa >> 8) & 0xFF;
		data[i*4+2] |= (mantissa >> 16) & 0x7F;
		data[i*4+3] |= (exponent >> 1) & 0x7F;
	}

	ret = fwrite(data, samples*aufile->bits_per_sample/8, 1, aufile->sndfile);

	if (data)
		free(data);

	return ret;
}

void* output_to_PCM(double **input, void *sample_buffer, int channels,
                    int samples, int format)
{
	unsigned char   ch;
	int             i;

	char             *char_sample_buffer = (char*)sample_buffer;
	short           *short_sample_buffer = (short*)sample_buffer;
	int               *int_sample_buffer = (int*)sample_buffer;
	float           *float_sample_buffer = (float*)sample_buffer;

	/*
	 * Copy output to a standard PCM buffer
	 */
	switch (format) {
	case WAV_FMT_8BIT:
		for (ch = 0; ch < channels; ch++) {
			for (i = 0; i < samples; i++) {
				char_sample_buffer[(i*channels)+ch] = (char)input[ch][i];
			}
		}
		break;
	case WAV_FMT_AIFF:
	case WAV_FMT_16BIT:
		for (ch = 0; ch < channels; ch++) {
			for (i = 0; i < samples; i++) {
				short_sample_buffer[(i*channels)+ch] = (short)input[ch][i];
			}
		}
		break;
	case WAV_FMT_24BIT:
	case WAV_FMT_32BIT:
		for (ch = 0; ch < channels; ch++) {
			for (i = 0; i < samples; i++) {
				int_sample_buffer[(i*channels)+ch] = (int)input[ch][i];
			}
		}
		break;
	case WAV_FMT_FLOAT:
		for (ch = 0; ch < channels; ch++) {
			for (i = 0; i < samples; i++) {
				float_sample_buffer[(i*channels)+ch] = (float)input[ch][i];
			}
		}
		break;
	}

	return sample_buffer;
}

/*
 * end of audio.c
 */
