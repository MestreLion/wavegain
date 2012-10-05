/*
 * Process files recursively, using an optional pattern
 *
 * This program is distributed under the GNU General Public License, version
 * 2.1. A copy of this license is included with this source.
 *
 * Copyright (C) 2002 Magnus Holmgren
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef ENABLE_RECURSIVE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif
#include "i18n.h"
#include "misc.h"
#include "recurse.h"



#ifdef _WIN32
#define PATH_SEPARATOR "\\"
#define PATH_SEPARATOR_CHAR '\\'
#else
#define PATH_SEPARATOR "/"
#define PATH_SEPARATOR_CHAR '/'
#endif


typedef struct directory
{
	const char* name;       /* Name of the read file (or directory) */
	int read_error;         /* True if an error has occured */

	/* The following stuff is used internally */
#ifdef _WIN32
	WIN32_FIND_DATA find_data;
	HANDLE find_handle;
#else
	DIR* dir;
	struct dirent* entry;
#endif
	const char* full_path;
} DIRECTORY;


/**
 * See if a path refers to a directory.
 *
 * \param path  path to examine.
 * \returns  1 if path is a directory, 0 if it isn't a directory, and -1 for
 * any errors.
 */
static int is_dir(const char* path)
{
	struct stat stat_buf;

	if (stat(path, &stat_buf) != 0) {
		return -1;
	}

	/* Is this the proper way to check for a directory? */
	return (stat_buf.st_mode & S_IFDIR) ? 1 : 0;
}


/**
 * \brief See if a string contains wildcard characters.
 *
 * See if a string contains wildcard characters, as used by match().
 *
 * \param string  text string to examine.
 * \return  1 if the string contains pattern characters, 0 otherwise.
 */
static int contains_pattern(const char* string)
{
	while (*string) {
		switch (*string) {
			case '?':
			case '*':
				return 1;

			case '\\':
				/* Accept a terminating \ as a literal \ */
				if (string[1])
					string++;
				/* Fall through */

			default:
				string++;
				break;
		}
	}

	return 0;
}


/**
 * \brief Compare two characters for equality.
 *
 * Compare two characters for equality. Placed as a separate function since
 * case should be considered on some platforms.
 *
 * \param c1  first character to compare.
 * \param c2  second character to compare.
 * \return  1 if the characters are equal, 0 otherwise.
 */
inline static int equal(const char c1, const char c2)
{
#ifdef _WIN32
	return (toupper(c1) == toupper(c2)) ? 1 : 0;
#else
	return (c1 == c2) ? 1 : 0;
#endif
}


/**
 * \brief Match a text against a pattern.
 *
 * Match a text against a pattern. The pattern may contain the wildcards '*'
 * and '?'. '*' matches zero or more characters while '?' matches exactly one
 * character. Wildcards can be escaped by preceding them with a '\'. To match
 * a '\' character, escape it (i.e., "\\").
 *
 * Using '\' as an escape character makes this function unsuitable to match
 * full pathnames on Win32 (or DOS) platforms. Matching the last part (i.e.,
 * after the last '\' (or '/', depending on platform)) works fine though.
 *
 * This function is case sensitive on some platforms.
 *
 * \param pattern  the pattern strings will be matched against.
 * \param text     string to match against pattern.
 * \return  1 if the text matches the pattern and 0 otherwise.
 */
static int match(const char* pattern, const char* text)
{
	const char* last_pattern = NULL;
	const char* last_text = NULL;

	while (*text) {
		switch (*pattern) {
			case '?':
				/* Just accept any difference */
				++pattern;
				++text;
				break;

			case '*':
				/* Search for the text following the '*' */
				last_pattern = ++pattern;
				last_text = text;
				break;

			case '\\':
				/* Accept a terminating \ as a literal \ */
				if (pattern[1])
					++pattern;
				/* Fall through */

			default:
				if (!equal(*pattern++, *text++)) {
					if (last_pattern != NULL) {
						/* Accept difference and repeat search for the text
						 * following the '*'
						 */
						pattern = last_pattern;
						text = ++last_text;
					}
					else
						return 0;
				}
				break;
		}
	}

	/* Only a match if pattern is exhausted (we know text is) */
	return (*pattern) ? 0 : 1;
}


#ifdef _WIN32
/**
 * \brief Display a file (or other I/O) Win32 error message.
 *
 * Display a file (or other I/O) error message that was caused by a 
 * Win32-specific function call. First a message is formatted
 * and printed, followed by the error text, terminated by a line feed.
 *
 * \param message  message format to display.
 * \param ...      printf-arguments used to format the message.
 */
static void file_error_win(const char *message, ...)
{
	char *error;
	va_list args;

	va_start(args, message);
	vfprintf(stderr, message, args);
	va_end(args);

	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &error, 0,
		NULL);

	fprintf(stderr, error);
	fprintf(stderr, "\n");
	LocalFree(error);
}


/**
 * Open the current directory for scanning.
 *
 * \param full_path  the directory name to dislpay in case of errors.
 * \return  directory structure, where the name field contains the name of
 *          the first entry in the directory. If an error occured, NULL is 
 *          returned (and a message has been printed).
 */
static DIRECTORY *open_dir(const char *full_path)
{
	DIRECTORY *result;

	result = (DIRECTORY *) calloc(1, sizeof(DIRECTORY));

	if (result != NULL) {
		result->find_handle = FindFirstFile("*", &result->find_data);

		if (result->find_handle != INVALID_HANDLE_VALUE) {
			result->full_path = full_path;
			result->name = result->find_data.cFileName;
			return result;
		}

		/* Could get "no more files" here, but not likely, due to "." and ".." */

		free(result);
		file_error_win(_("Couldn't scan directory '%s': "), full_path);
		return NULL;
	}

	fprintf(stderr, "Out of memory\n");
	return NULL;
}


/**
 * Get the next file or folder in the directory. 
 *
 * \param directory  pointer returned by open_dir. If the call is successful,
 *                   the name field is updated with the new name.
 * \return  0 for success and -1 for failure (in which case a message has been
 *          printed).
 */
static int read_dir(DIRECTORY *directory)
{
	if (FindNextFile(directory->find_handle, &directory->find_data)) {
		/* Probably not needed, but... */
		directory->name = directory->find_data.cFileName;
		return 0;
	}
	else {
		if (GetLastError() != ERROR_NO_MORE_FILES) {
			file_error_win(_("Couldn't scan directory '%s': "), directory->full_path);
			directory->read_error = 1;
		}
	}

	return -1;
}


/**
 * \brief  Close the scanning of a directory.
 * 
 * Close the scanning of a directory. No more calls to read_dir() can be made
 * after this call. 
 *
 * \param  directory  directory to stop scanning.
 * \return  0 for success and -1 for failure (in which case a message has been
 *          printed). 
 */
static int close_dir(DIRECTORY *directory)
{
	int result = -1;

	if (directory != NULL) {
		if (FindClose(directory->find_handle) == 0) {
			/* What could cause this? Do we need to take some action here? */
			file_error_win(_("Couldn't close directory '%s': "), directory->full_path);
		}
		else
			result = 0;
		free(directory);
	}

	return result;
}

#else /* WIN32 */

/**
 * Open the current directory for scanning.
 *
 * \param full_path  the directory name to dislpay in case of errors.
 * \return  directory structure, where the name field contains the name of
 *          the first entry in the directory. If an error occured, NULL is 
 *          returned (and a message has been printed).
 */
static DIRECTORY *open_dir(const char *full_path)
{
	DIRECTORY *result;

	result = (DIRECTORY *) calloc(1, sizeof(DIRECTORY));

	if (result != NULL) {
		result->full_path = full_path;
		result->dir = opendir(".");

		if (result->dir != NULL) {
			result->entry = readdir(result->dir);

			if (result->entry != NULL) {
				result->name = result->entry->d_name;
				return result;
			}
		}

		/* Could get "no more files" here, but not likely, due to "." and ".." */

		free(result);
		file_error(_("Couldn't scan directory '%s': "), result->full_path);
		return NULL;
	}

	fprintf(stderr, _("Out of memory\n"));
	return NULL;
}


/**
 * Get the next file or folder in the directory. 
 *
 * \param directory  pointer returned by open_dir. If the call is successful,
 *                   the name field is updated with the new name.
 * \return  0 for success and -1 for failure (in which case a message has been
 *          printed). 
 */
static int read_dir(DIRECTORY *directory)
{
	directory->entry = readdir(directory->dir);

	if (directory->entry != NULL) {
		directory->name = directory->entry->d_name;
		return 0;
	}

	/* I think this is the right way to check for errors... */
	directory->read_error = (errno != 0);

	if (directory->read_error)
	        file_error(_("Couldn't scan directory '%s': "), directory->full_path);
	
	return -1;
}


/**
 * \brief  Close the scanning of a directory.
 * 
 * Close the scanning of a directory. No more calls to read_dir() can be made
 * after this call. 
 *
 * \param  directory  directory to stop scanning.
 * \return  0 for success and -1 for failure (in which case a message has been
 *          printed). 
 */
static int close_dir(DIRECTORY *directory)
{
	int result = -1;

	if (directory != NULL) {
		if (closedir(directory->dir) != 0)
			file_error(_("Couldn't close directory '%s': "), directory->full_path);
		else
			result = 0;

		free(directory);
	}

	return result;
}
#endif /* WIN32 */


/**
 * \brief Process all files in a directory.
 *
 * Process all files in a directory. If settings->album is set, assume the 
 * files make up one album. If settings->recursive is set, process all 
 * subdirectories as well. 
 *
 * \param current   "display name" (i.e., not neccessarily the full name) of
 *                  the current path, used to display the name of the 
 *                  directory being processed. path will be appended to
 *                  current and passed on recursively, if needed.
 * \param path      name of folder to process.
 * \param settings  settings and global variables.
 * \return  0 if successful and -1 if an error occured (in which case a
 *          message has been printed).
 */
static int process_directory(const char* current, const char* path, SETTINGS* settings)
{
	char* full_path;
	char* old_path;
	int result = -1;

	old_path = getcwd(NULL, 1024);

	if (old_path == NULL) {
		file_error(_("Couldn't get name of current directory: "));
		return result;
	}

	full_path = malloc(strlen(current) + strlen(path) + 2);

	if (full_path == NULL) {
		free(old_path);
		fprintf(stderr, _("Out of memory"));
		return result;
	}

	strcpy(full_path, current);

	if (strlen(full_path) > 0)
		strcat(full_path, PATH_SEPARATOR);

	strcat(full_path, path);

	if (chdir(path) == 0) {
		DIRECTORY* directory;
		FILE_LIST* file_list = NULL;

		directory = open_dir(".");

		if (directory != NULL) {
			result = 0;

			do {
				int dir;

				/* Skip "special" directories */
				if (!strcmp(directory->name, ".") || !strcmp(directory->name, ".."))
					continue;

				dir = is_dir(directory->name);

				if (dir > 0) {
					if (settings->recursive)
						result = process_directory(full_path, directory->name, settings);
				}
				else if (!dir) {
					if(match(settings->pattern, directory->name) && (add_to_list(&file_list, directory->name) < 0))
						result = -1;
				}
				else {
					file_error(_("1-Couldn't find '%s': "), path);
					result = -1;
				}
			} while ((result == 0) && (read_dir(directory) == 0));

			if (directory->read_error)
				result = -1;

			close_dir(directory);
		}

		if ((result == 0) && (file_list != NULL)) {
			fprintf(stderr, _("\nProcessing directory '%s':\n"), full_path);

			result = process_files(file_list, settings, full_path);
		}

		free_list(file_list);

		if ((result == 0) && (chdir(old_path) != 0)) {
			file_error(_("Couldn't go back to folder '%s': "), old_path);
			result = 0;
		}
	}
	else
		file_error(_("Couldn't go to folder '%s': "), full_path);

	free(old_path);
	free(full_path);
	return result;
}


/**
 * \brief Process an argument.
 *
 * Process an argument. Check for wildcard at end of path, then process the 
 * file or folder specified.
 *
 * \param path      path argument to process.
 * \param settings  settings and global variables.
 * \return  0 if successful and -1 if an error occured (in which case a
 *          message has been printed).
 */
int process_argument(const char* path, SETTINGS* settings)
{
	char* buffer = strdup(path);
	char* my_path;
	int my_path_len;
	int dir;
	int result = -1;

	if (buffer == NULL) {
		fprintf(stderr, _("Out of memory\n"));
		return result;
	}

	my_path = buffer;
	/* Check for wildcards */
	settings->pattern = last_path(my_path);

	if (contains_pattern(settings->pattern)) {
		/* Strip last part of path */
		if (settings->pattern > my_path) {
			/* Not using [-1] to avoid compiler warning */
			settings->pattern--;
			*settings->pattern++ = '\0';
		}
		else
			my_path = "";
	}
	else
		settings->pattern = NULL;

	my_path_len = strlen(my_path);

	if (my_path_len == 0)
		my_path = ".";
	else if (my_path[my_path_len - 1] == PATH_SEPARATOR_CHAR)
		/* On Win32, "path" and "path\." are okay, but not "path\"... */
		my_path[my_path_len - 1] = '\0';

	dir = is_dir(my_path);

	if (dir > 0) {
		/* Finish off any files before processing folder */
		if (process_files(settings->file_list, settings, ".") == 0) {
			if (settings->pattern == NULL)
				settings->pattern = "*";

			result = process_directory("", my_path, settings);
		}

		free_list(settings->file_list);
		settings->file_list = NULL;
	}
	else if (dir == 0) {
		if (settings->pattern)
			/* A pattern was specified, but a file was found as "folder part" */
			fprintf(stderr, _("'%s' is a file, not a folder\n"), my_path);
		else
			result = add_to_list(&settings->file_list, my_path);
	}
	else
		file_error(_("Couldn't find '%s': "), my_path);

	free(buffer);
	return result;
}

#endif /* ENABLE_RECURSIVE */
