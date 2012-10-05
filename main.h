#ifndef MAIN_H
#define MAIN_H

/* Some version numbers */
#define WAVEGAIN_VERSION "1.3.1"

#define BUFFER_LEN  16384
#define TEMP_NAME "wavegain.tmp"
#define LOG_NAME "WGLog.txt"

#define NO_GAIN -10000.f

#ifdef _WIN32
#include <windows.h>
#endif

/** Information about a file to process */
typedef struct file_list
{
    struct file_list* next_file;
    const char* filename;
    double track_gain;
    double track_peak;
    double dc_offset[2];
    double offset[2];
} FILE_LIST;


/** Settings and misc other global data */
typedef struct settings
{
    FILE_LIST* file_list;         /**< Files to process (possibly as an album) */
#ifdef ENABLE_RECURSIVE
    char* pattern;                /**< Pattern to match file names against */
    int recursive;
#endif
    int first_file;               /**< About to process first file in directory */
    double man_gain;              /**< Apply Manual Gain entered by user */
    double album_peak;            /**< Will end up storing the highest value of the tracks analyzed */
    int audiophile;               /**< Calculate Album gain */
    int scale;                    /**< write Scale values to stdout */
    int apply_gain;               /**< Apply the calculated gain - album or track */
    int write_chunk;              /**< Write a 'gain' chunk containing the scalefactor applied to the wave data */
    int force;                    /**< Force a file to be re-replaygained when 'gain' chunk present */
    int undo;                     /**< Read the value in the 'gain' chunk and re-scale the data */
    int set_album_gain;           /**< Don't apply the calculated album gain if set */
    int fast;                     /**< Use the fast routines for RG analysis */
    int std_out;                  /**< Write output file to stdout */
    int radio;                    /**< Calculate Title gain  */
    int adc;                      /**< Apply Album based DC Offset correction (default is Track based)  */
    int no_offset;                /**< Do NOT apply any DC Offset  */
    int write_log;                /**< Write a log of gain calculations, etc */
    char *log_data;               /**< Pointer to data to be written to log file */
    int clip_prev;                /**< Whether, or not, to apply clipping prevention */
    int dithering;                /**< Apply dithering to output */
    int shapingtype;              /**< Noise shaping to use in dithering */
    int limiter;                  /**< Apply Hard limiter */
    unsigned int outbitwidth;     /**< bitwidth of desired output */
    unsigned int format;          /**< format of desired output */
    int need_to_process;          /**< need to process even if peak unchanged */
    char* cmd;
} SETTINGS;


extern int  add_to_list(FILE_LIST** list, const char* file);
extern void free_list(FILE_LIST* list);
extern int  process_files(FILE_LIST* file_list, SETTINGS* settings, const char* dir);

#endif /* MAIN_H */
