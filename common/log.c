#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "tm.h"

#ifdef WIN32
#include <malloc.h>
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include "path.h" /*for MAX_PATH*/
#endif

#define LOG_FILE_LINE

#define LOG_FILE_SIZE (1024*1024*10)
#define LOG_FILE_COUNT (10)
#define MAX_LOG_LEN (4096)

static FILE* log_file = NULL;
char log_file_name[MAX_PATH]={0};
int log_level=LOG_LEVEL_INFO;

FILE* log_file_handle()
{
	return log_file;
}


//from 1 to LOG_FILE_COUNT,loop write log
void log_to_file(const char* text)
{
	static int file_count = LOG_FILE_COUNT;
	long nCurrentSize;
	
	if(log_file_name[0] == '\0')
		return;

	if(!log_file)
		log_file = fopen(log_file_name, "a");
	if(!log_file)
		return;

	fprintf( log_file, "%s\n", text );
	fflush( log_file );

	nCurrentSize = ftell( log_file );
	if( nCurrentSize > LOG_FILE_SIZE)
	{
		char name1[256];
		char name2[256];
		int i;

		fclose( log_file );
		log_file = NULL;

		// delete oldest log file
		sprintf(name1, "%s.%d", log_file_name , file_count-1);
#ifdef WIN32
		_unlink(name1);
#else
		unlink(name1);
#endif
		//rename log file
		for (i=file_count-1; i>=2; i--)
		{
			sprintf(name1, "%s.%d", log_file_name, i);
			sprintf(name2, "%s.%d", log_file_name, i-1);
			rename(name2, name1);
		}
		if ( file_count > 1 )
		{
			sprintf(name1, "%s.1", log_file_name);
			rename(log_file_name, name1);
		}
		else
		{
#ifdef WIN32
			_unlink(log_file_name);
#else
			unlink(log_file_name);
#endif
		}

		log_file = fopen(log_file_name, "w");
	}
}

void set_log_colour(int level)
{
#ifdef WIN32
	WORD attrs = 0;
	switch( level )
	{
	case LOG_LEVEL_FATAL:
	case LOG_LEVEL_ERROR:
		attrs |= (FOREGROUND_RED | FOREGROUND_INTENSITY);
		break;
	case LOG_LEVEL_WARN:
		attrs |= (FOREGROUND_GREEN | FOREGROUND_INTENSITY);
		break;
	case LOG_LEVEL_PROMPT:
		attrs |= (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		break;
	case LOG_LEVEL_INFO:
	case LOG_LEVEL_DEBUG:
	case LOG_LEVEL_TRACE:
		attrs |= (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
		break;
	default:
		attrs |= (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
		break;
	}

	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attrs);

#else

#define CONSOLE_NONE                 "\e[0m"
#define CONSOLE_BLACK                "\e[0;30m"
#define CONSOLE_L_BLACK              "\e[1;30m"
#define CONSOLE_RED                  "\e[0;31m"
#define CONSOLE_L_RED                "\e[1;31m"
#define CONSOLE_GREEN                "\e[0;32m"
#define CONSOLE_L_GREEN              "\e[1;32m"
#define CONSOLE_BROWN                "\e[0;33m"
#define CONSOLE_YELLOW               "\e[1;33m"
#define CONSOLE_BLUE                 "\e[0;34m"
#define CONSOLE_L_BLUE               "\e[1;34m"
#define CONSOLE_PURPLE               "\e[0;35m"
#define CONSOLE_L_PURPLE             "\e[1;35m"
#define CONSOLE_CYAN                 "\e[0;36m"
#define CONSOLE_L_CYAN               "\e[1;36m"
#define CONSOLE_GRAY                 "\e[0;37m"
#define CONSOLE_WHITE                "\e[1;37m"

#define CONSOLE_BOLD                 "\e[1m"
#define CONSOLE_UNDERLINE            "\e[4m"
#define CONSOLE_BLINK                "\e[5m"
#define CONSOLE_REVERSE              "\e[7m"
#define CONSOLE_HIDE                 "\e[8m"
#define CONSOLE_CLEAR                "\e[2J"
#define CONSOLE_CLRLINE              "\r\e[K" //or "\e[1K\r"


	switch( level )
	{
	case LOG_LEVEL_FATAL:
	case LOG_LEVEL_ERROR:
		fputs(CONSOLE_L_RED, stdout);
		break;
	case LOG_LEVEL_WARN:
		fputs(CONSOLE_L_GREEN, stdout);
		break;
	case LOG_LEVEL_PROMPT:
		fputs(CONSOLE_WHITE, stdout);
		break;
	case LOG_LEVEL_INFO:
	case LOG_LEVEL_DEBUG:
	case LOG_LEVEL_TRACE:
		fputs(CONSOLE_NONE, stdout);
		break;
	default:
		fputs(CONSOLE_NONE, stdout);
		break;
	}
	
#endif
};

void vlog(const char* file, int line, int level, const char *format, va_list vargs)
{
	char logtext[MAX_LOG_LEN-128];
	char text[MAX_LOG_LEN];
	char level_text[32];
	struct tm *today;
	time_t ltime;
	unsigned int tid;
	int ms;

	if(level < log_level)
		return;

	vsnprintf(logtext, sizeof(logtext)-1, format, vargs);

	time( &ltime );
	today = localtime( &ltime );

	ms = now_ms_time()%1000;

	switch( level )
	{
	case LOG_LEVEL_FATAL:
		strcpy( level_text,"FATAL");
		break;
	case LOG_LEVEL_ERROR:
		strcpy( level_text,"ERROR");
		break;
	case LOG_LEVEL_WARN:
		strcpy( level_text,"WARN");
		break;
	case LOG_LEVEL_PROMPT:
		strcpy( level_text,"PROMPT");
		break;
	case LOG_LEVEL_INFO:
		strcpy( level_text,"INFO");
		break;
	case LOG_LEVEL_DEBUG:
		strcpy( level_text,"DEBUG");
		break;
	case LOG_LEVEL_TRACE:
		strcpy( level_text,"TRACE");
		break;
	}

	set_log_colour(level);

#ifdef WIN32
	tid = GetCurrentThreadId();
#else
	tid = syscall(SYS_gettid);
#endif

#ifdef LOG_FILE_LINE
	sprintf(text, "%02d-%02d-%02d %02d:%02d:%02d:%d:[%s,%d]:%d:%s:%s", 
		today->tm_year + 1900 - 2000,
		today->tm_mon + 1,
		today->tm_mday,
		today->tm_hour,
		today->tm_min,
		today->tm_sec,
		ms,
		file,
		line,
		tid,
		level_text,
		logtext );
#else
	sprintf(text, "%02d-%02d-%02d %02d:%02d:%02d:%d:%s:%s", 
		today->tm_year + 1900 - 2000,
		today->tm_mon + 1,
		today->tm_mday,
		today->tm_hour,
		today->tm_min,
		today->tm_sec,
		tid,
		level_text,
		logtext );
#endif

	printf("%s\r\n", text);

	set_log_colour(0);

	log_to_file(text);
}

void Log(const char* file, int line, int level, const char *format, ...)
{
	va_list vargs;

	if(level < log_level)
		return;

	va_start (vargs, format);
	
	vlog(file, line, level, format, vargs);

	va_end (vargs); 
}

