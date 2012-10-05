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
#include "config.h"
#endif

#include <ctype.h>
#include "getopt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "config.h"
#include "gain_analysis.h"
#include "i18n.h"
#include "audio.h"
#include "misc.h"
#include "wavegain.h"
#include "main.h"
#include "dither.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef ENABLE_RECURSIVE
#include "recurse.h"
#endif

/*This is an ugly way of doing this - but it works for the moment*/
#ifndef MAX_PATH
#define MAX_PATH 512
#endif

int	      write_to_log = 0;
char          log_file_name[MAX_PATH];
extern double total_samples;
extern double total_files;

/**
 * \brief Allocate a FILE_LIST node.
 *
 * Allocate a FILE_LIST node. filename is set to file while track_peak and
 * track_gain are set to NO_PEAK and NO_GAIN respectively. The rest of the
 * fields are set to zero.
 *
 * \param file  name of file to get a node for.
 * \return  the allocated node or NULL (in which case a message has been
 *          printed).
 */
static FILE_LIST* alloc_node(const char* file)
{
	FILE_LIST* node;

	node = calloc(1, sizeof(*node));

	if (node != NULL) {
		node->filename = strdup(file);

		if (node->filename != NULL) {
			node->track_peak = NO_PEAK;
			node->track_gain = NO_GAIN;
			node->dc_offset[0] = 0.;
			node->dc_offset[1] = 0.;
			node->offset[0] = 0.;
			node->offset[1] = 0.;
			return node;
		}
		free(node);
	}

	fprintf(stderr, _("Out of memory\n"));
	return NULL;
}


/**
 * \brief Allocate a new FILE_LIST node and add it to the end of a list.
 *
 * Allocate a new FILE_LIST node and add it to the end of a list.
 *
 * \param list  pointer to the list (first node) pointer. If the list
 *              pointer contains NULL, it is set to the new node. Otherwise 
 *              the new node is added the last node in the list.
 * \param file  name of the file to get a node for.
 * \return  0 if successful and -1 if the node couldn't be allocated (in
 *          which case a message has been printed).
 */
int add_to_list(FILE_LIST** list, const char* file)
{
	if (*list == NULL) {
		*list = alloc_node(file);

		if (*list == NULL)
			return -1;
	}
	else {
		FILE_LIST* node = *list;

		while (node->next_file != NULL)
			node = node->next_file;

		node->next_file = alloc_node(file);

		if (node->next_file == NULL)
			return -1;
	}

	return 0;
}


/**
 * Free all nodes in a FILE_LIST list.
 *
 * \param list  pointer to first node in list. Can be NULL, in which case 
 *              nothing is done.
 */
void free_list(FILE_LIST* list)
{
	FILE_LIST* next;

	while (list != NULL) {
		next = list->next_file;
		free((void *) list->filename);
		free(list);
		list = next;
	}
}

static char s_floatToAscii[256];
const char* ftos(double f, const char* format)
{
	sprintf(s_floatToAscii, format, f);
	return s_floatToAscii;
}

/**
 * \brief Processs the file in file_list.
 *
 * Process the files in file_list. If settings->album is set, the files are
 * considered to belong to one album.
 *
 * \param file_list  list of files to process.
 * \param settings   settings and global variables.
 * \return  0 if successful and -1 if an error occured (in which case a
 *          message has been printed).
 */
int process_files(FILE_LIST* file_list, SETTINGS* settings, const char* dir)
{
	FILE_LIST* file;
	double     factor_clip,
	           audiophile_gain = 0.,
	           Gain,
	           scale,
	           dB,
	           album_dc_offset[2] = {0., 0.};

	settings->album_peak = NO_PEAK;

	if (file_list == NULL)
		return 0;

	settings->first_file = 1;

	/* Undo previously applied gain */
	if (settings->undo) {
		for (file = file_list; file; file = file->next_file) {
			if (file->filename == '\0')
				continue;
			if (!write_gains(file->filename, 0, 0, 0, 0, 0, settings)) {
				fprintf(stderr, " Error processing GAIN for file - %s\n", file->filename);
				continue;
			}
		}
	}
	else {
		/* Analyze the files */
		for (file = file_list; file; file = file->next_file) {
			int dc_l;
			int dc_r;
			if (file->filename == '\0')
				continue;

			if (!get_gain(file->filename, &file->track_peak, &file->track_gain,
			              file->dc_offset, file->offset, settings)) {
				file->filename = '\0';
				continue;
			}
			dc_l = (int)(file->dc_offset[0] * 32768 * -1);
			dc_r = (int)(file->dc_offset[1] * 32768 * -1);
			if (dc_l < -1 || dc_l > 1 || dc_r < -1 || dc_r > 1)
				settings->need_to_process = 1;
			album_dc_offset[0] += file->offset[0];
			album_dc_offset[1] += file->offset[1];
		}

		album_dc_offset[0] /= total_samples;
		album_dc_offset[1] /= total_samples;

		if (!settings->no_offset && settings->adc) {
			int dc_l = (int)(album_dc_offset[0] * 32768 * -1);
			int dc_r = (int)(album_dc_offset[1] * 32768 * -1);
			fprintf(stderr, " ********************* Album DC Offset | %4d  |  %4d  | ***************\n",
				dc_l, dc_r);
			if(write_to_log) {
				write_log(" ********************* Album DC Offset | %4d  |  %4d  | ***************\n",
					dc_l, dc_r);
			}
		}

		fprintf(stderr, "\n");
		if(write_to_log)
			write_log("\n");

		if (settings->audiophile) {
			Gain = GetAlbumGain() + settings->man_gain;
			scale = pow(10., Gain * 0.05);
			settings->set_album_gain = 0;
			if(settings->clip_prev) {
				factor_clip  = (32767./( settings->album_peak + 1));
				if(scale < factor_clip)
					factor_clip = 1.0;
				else
					factor_clip /= scale;
				scale *= factor_clip;
			}
			if (settings->scale) {
				fprintf(stdout, "%8.6lf", scale);
			}

			if (scale > 0.0)
				dB = 20. * log10(scale);
			else
				dB = 0.0;
			audiophile_gain = dB;
			if (audiophile_gain < 0.1 && audiophile_gain > -0.1 && !settings->need_to_process) {
				fprintf(stderr, " No Album Gain adjustment or DC Offset correction required, exiting.\n");
				if(write_to_log)
					write_log(" No Album Gain adjustment or DC Offset correction required, exiting.\n");
				settings->set_album_gain = 1;
			}
			else {
				fprintf(stderr, " Recommended Album Gain: %+6.2f dB\tScale: %6.4f\n\n", audiophile_gain, scale);
				if(write_to_log)
					write_log(" Recommended Album Gain: %+6.2f dB\tScale: %6.4f\n\n", audiophile_gain, scale);
			}
		}

		if(settings->apply_gain) {	/* Write radio and audiophile gains. */
			total_files = 0.0;
			for (file = file_list; file; file = file->next_file) {
				if (file->filename == '\0')
					continue;
				if (settings->audiophile && settings->set_album_gain == 1)
					break;
				if (settings->radio && (file->track_gain < 0.1 && file->track_gain > -0.1) && !settings->need_to_process) {
					fprintf(stderr, " No Title Gain adjustment or DC Offset correction required for file: %s, skipping.\n", file->filename);
					if(write_to_log)
						write_log(" No Title Gain adjustment or DC Offset correction required for file: %s, skipping.\n", file->filename);
				}
				else if (!write_gains(file->filename, file->track_gain,	audiophile_gain, file->track_peak, 
						      file->dc_offset, album_dc_offset, settings)) {
						fprintf(stderr, " Error processing GAIN for file - %s\n", file->filename);
						continue;
				}
			}
		}
	}

	fprintf(stderr, "\n WaveGain Processing completed normally\n");
	if(write_to_log)
		write_log("\n WaveGain Processing completed normally\n\n");

#ifdef _WIN32
	if (settings->cmd) { 	/* execute user command */
		FILE_LIST* file;
		char buff[MAX_PATH];
		char *b, *p, *q;
		double track_scale = 1.0;

		SetEnvironmentVariable("ALBUM_GAIN", ftos(audiophile_gain, "%.2lf"));
		SetEnvironmentVariable("ALBUM_SCALE", ftos(scale, "%.5lf"));
		SetEnvironmentVariable("ALBUM_NEW_PEAK", ftos(settings->album_peak * scale, "%.0lf"));
		SetEnvironmentVariable("ALBUM_PEAK", ftos(settings->album_peak, "%.0lf"));

		for (file = file_list; file; file = file->next_file) {
			if (file->filename == '\0')
				continue;

			if (dir[0] == '.' && dir[1] == '\\') dir += 2;

			strcpy(buff, file->filename);
			b = (buff[0] == '.' && buff[1] == '\\') ? buff+2 : buff;
			SetEnvironmentVariable("INPUT_FILE", b);

			p = strrchr(b, '\\');
			if (p) {
				p[0] = '\0'; ++p;
				SetEnvironmentVariable("INPUT_FDIR", b);
				SetEnvironmentVariable("INPUT_RDIR", b); // assume dir = "."
			}
			else {
				p = b;
				SetEnvironmentVariable("INPUT_FDIR", ".");
				SetEnvironmentVariable("INPUT_RDIR", dir);
			}

			q = strrchr(p, '.');
			if (q) q[0] = '\0';
			SetEnvironmentVariable("INPUT_NAME", p);

			track_scale = (pow(10., file->track_gain * 0.05));
			SetEnvironmentVariable("TRACK_SCALE", ftos(track_scale, "%.5lf"));
			SetEnvironmentVariable("TRACK_GAIN", ftos(file->track_gain, "%.2lf"));
			SetEnvironmentVariable("TRACK_NEW_PEAK", ftos(file->track_peak, "%.0lf"));
			SetEnvironmentVariable("TRACK_PEAK", ftos(file->track_peak / track_scale, "%.0lf"));

			SetEnvironmentVariable("DC_OFFSET_L", ftos((int)(file->dc_offset[0]*(-32768)), "%.0lf"));
			SetEnvironmentVariable("DC_OFFSET_R", ftos((int)(file->dc_offset[1]*(-32768)), "%.0lf"));

			fprintf(stderr, "\n Executing command on \"%s\":\n\n", file->filename);
			system(settings->cmd);
		}
	}
#endif

	return 0;
}


/**
 * Print out a list of options and the command line syntax.
 */
static void usage(void)
{
	fprintf(stdout, _("WaveGain v" WAVEGAIN_VERSION " Compiled " __DATE__ ".\n"));
	fprintf(stdout, _("Copyright (c) 2002-2010 John Edwards <john.edwards33@ntlworld.com>\n"));
	fprintf(stdout, _("Additional code by Magnus Holmgren, Gian-Carlo Pascutto, and Tycho\n\n"));
#ifdef _WIN32
	fprintf(stdout, " Usage: wavegain [options] input.wav [...] [-e cmd [args]]\n\n");
#else
	fprintf(stdout, " Usage: wavegain [options] input.wav [...]\n\n");
#endif
	fprintf(stdout, " OPTIONS\n");
	fprintf(stdout, "  -h, --help       Prints this help information.\n");
	fprintf(stdout, "  -a, --album      Use ReplayGain Audiophile/Album gain setting, or\n");
	fprintf(stdout, "  -r, --radio      Use ReplayGain Radio/Single Track gain setting(DEFAULT).\n");
	fprintf(stdout, "  -q, --adc        Apply Album based DC Offset correction.\n");
	fprintf(stdout, "                   DEFAULT is Track based DC Offset correction.\n");
	fprintf(stdout, "  -p, --no_offset  Do NOT apply DC Offset correction.\n");
	fprintf(stdout, "  -c, --calculate  Calculates and prints gain settings, and DC Offsets BUT\n");
	fprintf(stdout, "                   DOES NOT APPLY THEM - This is the DEFAULT.\n");
	fprintf(stdout, "  -x, --scale      Writes scale values to stdout in the format: n.nnnnnn\n");
	fprintf(stdout, "                   In Album mode it only writes the Album Scale value, and\n");
	fprintf(stdout, "                   in Title mode it only writes the Title Scale values.\n");
	fprintf(stdout, "                   ONLY works in Calculation mode.\n");
	fprintf(stdout, "  -y, --apply      Calculates and APPLIES gain settings, and applies\n");
	fprintf(stdout, "                   DC Offset correction.\n");
	fprintf(stdout, "  -w, --write      Writes a 'gain' chunk into the Wave Header.\n");
	fprintf(stdout, "                   Stores the scalefactor applied to the wave data as a\n");
	fprintf(stdout, "                   double floating point number. Only written when gain\n");
	fprintf(stdout, "                   is applied. Presence will result in file being skipped\n");
	fprintf(stdout, "                   if reprocessed.\n");
	fprintf(stdout, "                   (Unless '--force' or '--undo-gain' are specified.)\n");
	fprintf(stdout, "      --force      Forces the reprocessing of a file that contains a 'gain'\n");
	fprintf(stdout, "                   chunk and will result in the new scalefactor overwriting\n");
	fprintf(stdout, "                   the existing value.\n");
	fprintf(stdout, "      --undo-gain  Reads the scalefactor in the 'gain' chunk and uses the\n");
	fprintf(stdout, "                   value to reverse the previously applied gain. This will NOT\n");
	fprintf(stdout, "                   recreate a bit identical version of the original file, but\n");
	fprintf(stdout, "                   it will be rescaled to the original level.\n");
#ifdef ENABLE_RECURSIVE
	fprintf(stdout, "  -z, --recursive  Search for files recursively, each folder as an album\n");
#endif
	fprintf(stdout, "  -l, --log        Write log file.(Default filename = WGLog.txt)\n");
	fprintf(stdout, "  -f, --logfile    Specify log filename. (Assumes -l if present.)\n");
	fprintf(stdout, "  -n, --noclip     NO Clipping Prevention.\n");
	fprintf(stdout, "  -d, --dither X   Dither output, where X =\n");
	fprintf(stdout, "               0   for       dither OFF (default).\n");
	fprintf(stdout, "               1   for       dither without Noise Shaping.\n");
	fprintf(stdout, "               2   for       dither with Light Noise Shaping.\n");
	fprintf(stdout, "               3   for       dither with Medium Noise Shaping.\n");
	fprintf(stdout, "               4   for       dither with Heavy Noise Shaping.\n");
	fprintf(stdout, "  -t, --limiter    Apply 6dB Hard Limiter to output.\n");
	fprintf(stdout, "  -g, --gain X     Apply additional Manual Gain adjustment in decibels, where\n");
	fprintf(stdout, "             X = any floating point number between -20.0 and +12.0.\n");
	fprintf(stdout, "                   Clipping Prevention WILL be applied UNLESS '-n' is used.\n");
	fprintf(stdout, "  -s, --fast       Calculates and prints gain settings - DOES NOT APPLY THEM.\n");
	fprintf(stdout, "                   NOTE: This method does NOT process all samples, it only\n");
	fprintf(stdout, "                         processes 200 x 16k chunks of samples. Results will\n");
	fprintf(stdout, "                         NOT be as accurate as a full analysis but, with most\n");
	fprintf(stdout, "                         material, will be within +/- 0.5db. Files of 8,192,000\n");
	fprintf(stdout, "                         real samples, or less, will be analysed in full.\n");
	fprintf(stdout, "                         DC Offset is neither calculated nor corrected in\n");
	fprintf(stdout, "                         FAST mode.\n");
	fprintf(stdout, "  -o, --stdout     Write output file to stdout.\n");
	fprintf(stdout, " FORMAT OPTIONS (One option ONLY may be used)\n");
	fprintf(stdout, "  -b, --bits X     Set output sample format, where X =\n");
	fprintf(stdout, "             1     for        8 bit unsigned PCM data.\n");
	fprintf(stdout, "             2     for       16 bit signed PCM data.\n");
	fprintf(stdout, "             3     for       24 bit signed PCM data.\n");
	fprintf(stdout, "             4     for       32 bit signed PCM data.\n");
	fprintf(stdout, "             5     for       32 bit floats.\n");
	fprintf(stdout, "             6     for       16 bit 'aiff' format.\n");
	fprintf(stdout, "         NOTE:     By default, the output file will be of the same bitwidth\n");
	fprintf(stdout, "                   and type as the input file.\n");
#ifdef _WIN32
	fprintf(stdout, "  -e, --exec Cmd   Execute a command after wavegain.\n");
	fprintf(stdout, "                   The following environment variables are available:\n");
	fprintf(stdout, "                   INPUT_FILE, INPUT_FDIR, INPUT_RDIR, INPUT_NAME,\n");
	fprintf(stdout, "                   TRACK_GAIN, TRACK_PEAK, TRACK_SCALE, TRACK_NEW_PEAK,\n");
	fprintf(stdout, "                   ALBUM_GAIN, ALBUM_PEAK, ALBUM_SCALE, ALBUM_NEW_PEAK,\n");
	fprintf(stdout, "                   DC_OFFSET_L, DC_OFFSET_R\n");
#endif
	fprintf(stdout, " INPUT FILES\n");
	fprintf(stdout, "  WaveGain input files may be 8, 16, 24 or 32 bit integer, or floating point\n"); 
	fprintf(stdout, "  wave files with 1 or 2 channels and a sample rate of 96000Hz, 88200Hz,\n");
	fprintf(stdout, "  64000Hz, 48000Hz, 44100Hz, 32000Hz, 24000Hz, 22050Hz, 16000Hz, 12000Hz,\n");
	fprintf(stdout, "  11025Hz or 8000Hz.\n");
	fprintf(stdout, "  16 bit integer 'aiff' files are also supported.\n");
	fprintf(stdout, "  Wildcards (?, *) can be used in the filename, or '-' for stdin.\n");

    return;
}


const static struct option long_options[] = {
	{"help",	0, NULL, 'h'},
	{"album",	0, NULL, 'a'},
	{"radio",	0, NULL, 'r'},
	{"adc",		0, NULL, 'q'},
	{"no_offset",	0, NULL, 'p'},
	{"calculate",	0, NULL, 'c'},
	{"scale",	0, NULL, 'x'},
	{"apply",	0, NULL, 'y'},
	{"write",	0, NULL, 'w'},
	{"force",	0, NULL,  0 },
	{"undo-gain",	0, NULL,  0 },
	{"fast",	0, NULL, 's'},
	{"stdout",	0, NULL, 'o'},
#ifdef ENABLE_RECURSIVE
	{"recursive",   0, NULL, 'z'},
#endif
	{"log",		0, NULL, 'l'},
	{"logfile",	1, NULL, 'f'},
	{"noclip",	0, NULL, 'n'},
	{"dither",	1, NULL, 'd'},
	{"limiter",	0, NULL, 't'},
	{"gain",	1, NULL, 'g'},
	{"bits",	1, NULL, 'b'},
#ifdef _WIN32
	{"exec",	1, NULL, 'e'},
#endif
	{NULL,		0, NULL , 0}
};


#ifdef ENABLE_RECURSIVE
#define ARG_STRING "harqpcxywsozlf:nd:tg:b:e:"
#else
#define ARG_STRING "harqpcxywsolf:nd:tg:b:e:"
#endif


int main(int argc, char** argv)
{
	SETTINGS settings;
	int      option_index = 1,
	         ret,
	         i,
	         bits;

 	char     CmdDir[MAX_PATH];
 	char     *p;

	memset(&settings, 0, sizeof(settings));
	settings.first_file = 1;
	settings.radio = 1;
	settings.clip_prev = 1;
	settings.outbitwidth = 16;
	settings.format = WAV_NO_FMT;

#ifdef _WIN32
	/* Is this good enough? Or do we need to consider multi-byte codepages as 
	* well? 
	*/
	SetConsoleOutputCP(GetACP());

	GetModuleFileName(NULL, CmdDir, MAX_PATH);
	p = strrchr(CmdDir, '\\') + 1;
	p[0] = '\0';
#endif

	while ((ret = getopt_long(argc, argv, ARG_STRING, long_options, &option_index)) != -1) {
		switch(ret) {
			case 0:
				if (!strcmp(long_options[option_index].name, "force")) {
					settings.force = 1;
				}
				else if (!strcmp(long_options[option_index].name, "undo-gain")) {
					settings.undo = 1;
				}
				else {
					fprintf(stderr, "Internal error parsing command line options\n");
					exit(1);
				}
				break;
			case 'h':
				usage();
				exit(0);
				break;
			case 'a':
				settings.audiophile = 1;
				break;
			case 'r':
				settings.radio = 1;
				break;
			case 'q':
				settings.adc = 1;
				break;
			case 'p':
				settings.no_offset = 1;
				break;
			case 'c':
				settings.apply_gain = 0;
				break;
			case 'x':
				settings.scale = 1;
				break;
			case 'y':
				settings.apply_gain = 1;
				settings.scale = 0;
				break;
			case 'w':
				settings.write_chunk = 1;
				break;
			case 's':
				settings.fast = 1;
				break;
			case 'o':
				settings.std_out = 1;
				break;
#ifdef ENABLE_RECURSIVE
			case 'z':
				settings.recursive = 1;
				break;
#endif
			case 'l':
				write_to_log = 1;
#ifdef _WIN32
				strcpy(log_file_name, CmdDir);
				strcat(log_file_name, LOG_NAME);
#else
				strcpy(log_file_name, LOG_NAME);
#endif
				break;
			case 'f':
				write_to_log = 1;
				strcpy(log_file_name, optarg);
				break;
			case 'n':
				settings.clip_prev = 0;
				break;
			case 'd':
				settings.need_to_process = 1;
				settings.dithering = 1;
   				if(sscanf(optarg, "%d", &settings.shapingtype) != 1) {
	    				fprintf(stderr, "Warning: dither type %s not recognised, using default\n", optarg);
		    			break;
				}
				if (settings.shapingtype == 0)
					settings.dithering = 0;
				else if (settings.shapingtype == 1)
					settings.shapingtype = 0;
				else if (settings.shapingtype == 2)
					settings.shapingtype = 1;
				else if (settings.shapingtype == 3)
					settings.shapingtype = 2;
				else if (settings.shapingtype == 4)
					settings.shapingtype = 3;
				break;
			case 't':
				settings.limiter = 1;
				break;
			case 'g':
   				if(sscanf(optarg, "%lf", &settings.man_gain) != 1) {
	    				fprintf(stderr, "Warning: manual gain %s not recognised, ignoring\n", optarg);
					break;
				}
				if(settings.man_gain < -20.0) {
	    				fprintf(stderr, "Warning: manual gain %s is out of range, "
					                 "applying gain of -20.0dB\n", optarg);
					settings.man_gain = -20.0;
				}
				else if(settings.man_gain > 12.0) {
	    				fprintf(stderr, "Warning: manual gain %s is out of range, "
					                 "applying gain of +12.0dB\n", optarg);
					settings.man_gain = 12.0;
				}
				break;
			case 'b':
				settings.need_to_process = 1;
   				if(sscanf(optarg, "%d", &bits) != 1) {
	    				fprintf(stderr, "Warning: output format %s not recognised, using default\n", optarg);
		    			break;
				}
				if (bits == 1) {
					settings.outbitwidth = 8;
					settings.format = WAV_FMT_8BIT;
		    			break;
				}
				else if (bits == 2) {
					settings.outbitwidth = 16;
					settings.format = WAV_FMT_16BIT;
		    			break;
				}
				else if (bits == 3) {
					settings.outbitwidth = 24;
					settings.format = WAV_FMT_24BIT;
		    			break;
				}
				else if (bits == 4) {
					settings.outbitwidth = 32;
					settings.format = WAV_FMT_32BIT;
		    			break;
				}
				else if (bits == 5) {
					settings.outbitwidth = 32;
					settings.format = WAV_FMT_FLOAT;
		    			break;
				}
				else if (bits == 6) {
					settings.outbitwidth = 16;
					settings.format = WAV_FMT_AIFF;
		    			break;
				}
				else {
	    				fprintf(stderr, "Warning: output format %s not recognised, using default\n", optarg);
					break;
				}
				break;
#ifdef _WIN32
			case 'e': {
				char *p;
				int k;
				char * lpPart[MAX_PATH]={NULL};

				settings.cmd = (char *) malloc(1024*8); /* 8Kb is XP's limit */
				if (settings.cmd == NULL) {
					fprintf(stderr, "Failed to allocate memory for cmd...\n");
					return 1;
				}

				p = settings.cmd;

				k = GetFullPathName(argv[optind - 1], 1024*8, p, lpPart);
				k = GetShortPathName(p, p, MAX_PATH);
				if (k == 0) {
					p += sprintf (p, "%s", argv[optind - 1]);
				} else {
					p += k;
				}

				for (k = optind; k < argc; ++k) {
					p += sprintf(p, " \"%s\"", argv[k]);
				}

				argc = optind;
				break;
			}
#endif
		}
	}
	if (settings.undo == 1) {
		settings.write_chunk = 0;
		settings.no_offset = 1;
	}

	if (optind >= argc) {
		fprintf(stderr, _("No files specified.\n"));
		usage();
		return EXIT_SUCCESS;
	}

	if (write_to_log) {
		write_log("Command line:\n\t");
		for(i = 0; i < argc; i++)
			write_log("%s ",  argv[i]);
		write_log("\n");
	}

	if (!strcmp(argv[optind], "-")) {
		double track_peak, track_gain;
		double dc_offset;
		double offset;
		if (!get_gain("-", &track_peak, &track_gain,
		              &dc_offset, &offset, &settings))
			return -1;
	}
	else {
		for (i = optind; i < argc; ++i) {
#ifdef ENABLE_RECURSIVE
			if (process_argument(argv[i], &settings) < 0) {
				free_list(settings.file_list);
				return EXIT_FAILURE;
			}
#else
			if (add_to_list(&settings.file_list, argv[i]) < 0)
				return EXIT_FAILURE;
#endif
		}

		/* Process files (perhaps remaining) in list */
		ret = process_files(settings.file_list, &settings, ".");
		free_list(settings.file_list);
		settings.file_list = NULL;
		if (settings.cmd) free(settings.cmd);
		settings.cmd = NULL;
	}

	return (ret < 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
