#ifndef _CONFIG_H_
#define _CONFIG_H_

/* config.h You may need to edit this to match your system.  */

/* Enable recursive processing and pattern matching */
#define ENABLE_RECURSIVE

/* Define if you have the <dirent.h> header file, and it defines `DIR'. */
#undef HAVE_DIRENT_H

/* Define if you don't have `vprintf' but do have `_doprnt.' */
#undef HAVE_DOPRNT

/* Define if you have the <errno.h> header file. */
#define HAVE_ERRNO_H

/* Define if you have the `floor' function. */
#define HAVE_FLOOR

/* Define if you have the `getcwd' function. */
#define HAVE_GETCWD

/* Define if you have the <inttypes.h> header file. */
#undef HAVE_INTTYPES_H

/* Define if your system has a working `malloc' function. */
#define HAVE_MALLOC

/* Define if you have the `memmove' function. */
#define HAVE_MEMMOVE

/* Define if you have the <memory.h> header file. */
#define HAVE_MEMORY_H

/* Define if you have the `memset' function. */
#define HAVE_MEMSET

/* Define if you have the <ndir.h> header file, and it defines `DIR'. */
#undef HAVE_NDIR_H

/* Define if you have the `pow' function. */
#define HAVE_POW

/* Define if `stat' has the bug that it succeeds when given the zero-length
   file name argument. */
#undef HAVE_STAT_EMPTY_STRING_BUG

/* Define if you have the <stdint.h> header file. */
#undef HAVE_STDINT_H

/* Define if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H

/* Define if you have the `strchr' function. */
#define HAVE_STRCHR

/* Define if you have the `strdup' function. */
#define HAVE_STRDUP

/* Define if you have the `strerror' function. */
#define HAVE_STRERROR

/* Define if you have the <strings.h> header file. */
#undef HAVE_STRINGS_H

/* Define if you have the <string.h> header file. */
#define HAVE_STRING_H

/* Define if you have the <sys/dir.h> header file, and it defines `DIR'. */
#undef HAVE_SYS_DIR_H

/* Define if you have the <sys/ndir.h> header file, and it defines `DIR'. */
#undef HAVE_SYS_NDIR_H

/* Define if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H

/* Define if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H

/* Define if you have the <unistd.h> header file. */
#undef HAVE_UNISTD_H

/* Define if you have the <utime.h> header file. */
#define HAVE_UTIME_H

/* Define if you have the `vprintf' function. */
#define HAVE_VPRINTF

/* Define if `lstat' dereferences a symlink specified with a trailing slash. */
#undef LSTAT_FOLLOWS_SLASHED_SYMLINK

/* Define if you have the ANSI C header files. */
#define STDC_HEADERS

/* Define to empty if `const' does not conform to ANSI C. */
//#undef const

/* Define as `__inline' if that's what the C compiler calls it, or to nothing
   if it is not supported. */
#define inline __inline

/* Define to `unsigned' if <sys/types.h> does not define. */
//#undef size_t

/* Define according to endianness of CPU - PC = LITTLE, APPLE/MAC = BIG, etc. */
#define LITTLE                  0
#define BIG                     1
#ifdef __APPLE__
# define machine_endianness      BIG
#else
# define machine_endianness      LITTLE
#endif

#endif /* _CONFIG_H_ */

