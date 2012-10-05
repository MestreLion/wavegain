#ifndef MISC_H
#define MISC_H

#if  defined _WIN32 && !defined __MINGW32__                 // Microsoft and Intel call it __int64
typedef signed   __int64    Int64_t;
typedef unsigned __int64    Uint64_t;
#else
typedef signed   long long  Int64_t;
typedef unsigned long long  Uint64_t;
#endif

void file_error(const char* message, ...);
char* last_path(const char* path);
extern void log_error(const char *fmt, ...);

#endif /* MISC_H */
