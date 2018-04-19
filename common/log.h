#ifndef __LOG_H__
#define __LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include "path.h"

#define LOG_LEVEL_FATAL 7
#define LOG_LEVEL_ERROR 6
#define LOG_LEVEL_WARN  5
#define LOG_LEVEL_PROMPT 4
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 2
#define LOG_LEVEL_TRACE 1


void vlog(const char* file, int line, int level, const char *format, va_list vargs);

void Log(const char* file, int line, int level, const char *format, ...);

#define LOG(level,format,...) Log(__FILE__, __LINE__, level, format, ## __VA_ARGS__)

FILE* log_file_handle();

extern char log_file_name[MAX_PATH];
extern int log_level;

inline void set_log_level(int level)
{
	log_level = level;
}

inline void set_log_file_name(const char* filename)
{
	strcpy(log_file_name, filename);
}

#ifdef __cplusplus
}; //end of extern "C" {
#endif
#endif