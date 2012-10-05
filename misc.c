/*
 * Misc utility functions
 *
 * This program is distributed under the GNU General Public License, version 
 * 2.1. A copy of this license is included with this source.
 *
 * Copyright (C) 2002 Gian-Carlo Pascutto and Magnus Holmgren
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "i18n.h"
#include "misc.h"

#ifndef _WIN32
#include <errno.h>
#include <ctype.h>
#endif

extern char log_file_name[];

/**
 * \brief Display a file (or other I/O) error message.
 *
 * Display a file (or other I/O) error message. First a message is formatted
 * and printed, followed by the error text, terminated by a line feed.
 *
 * \param message  message format to display.
 * \param ...      printf-arguments used to format the message.
 */
void file_error(const char* message, ...)
{
	int err_num = errno;
	va_list args;

	va_start(args, message);
	vfprintf(stderr, message, args);
	va_end(args);

	fprintf(stderr, strerror(err_num));
	fprintf(stderr, "\n");
}


/**
 * \brief Get the last component in a path. 
 *
 * Get the last component in a path. If no directory separator is found, 
 * return the path as is.
 *
 * \param path  path to get last component of.
 * \return  the last path component, or path.
 */    
char* last_path(const char* path)
{
	int i;

	for (i = strlen(path) - 1; i >= 0; i--) {
#ifdef _WIN32
		if ((path[i] == '\\') || (path[i] == ':')) {
#else
		if (path[i] == '/') {
#endif
			return (char*) &path[i + 1];
		}
	}

	return (char*) path;
}

void write_log(const char *fmt, ...)
{
	va_list ap;
	FILE *fp;
	char msgbuf[1024];
	char *bufp = msgbuf;

	/*
	 * A really rough sanity check to protect against blatant buffer overrun
	 */
	if (strlen(fmt) > 750)
		sprintf(msgbuf, "%s %s", "<buffer overflow> ", fmt);
	else {
		va_start(ap, fmt);
		vsprintf(bufp, fmt, ap);
		va_end(ap);
	}
	if ((fp = fopen(log_file_name, "a")) == (FILE *)NULL)
		return;

	fprintf(fp, "%s", msgbuf);
	fflush(fp);
	fclose(fp);

	va_end(ap);
}


