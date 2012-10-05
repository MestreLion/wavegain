#ifndef MISC_H
#define MISC_H

#ifdef _WIN32
typedef signed   __int64    Int64_t;
typedef unsigned __int64    Uint64_t;
#define chdir _chdir
#define strdup _strdup
#define getcwd _getcwd
#define getpid _getpid
#else
typedef signed   long long  Int64_t;
typedef unsigned long long  Uint64_t;
#endif

void file_error(const char* message, ...);
char* last_path(const char* path);
extern void write_log(const char *fmt, ...);

#endif /* MISC_H */
