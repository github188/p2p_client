#ifndef __PATH__H__
#define __PATH__H__

#ifdef __cplusplus
extern "C"
{
#endif

const char* get_exe_path();

#ifdef WIN32
#include <windows.h> 
#else
	#define MAX_PATH 1024
#endif

#ifdef __cplusplus
}; //end of extern "C" {
#endif

#endif