#include "path.h"

#ifndef WIN32
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#endif

const char* get_exe_path()
{
	static char full_path[MAX_PATH]={0};
	size_t full_path_length = 0;
	size_t last;

#ifdef WIN32
#elif FREEBSD
	int mib[4];
#elif LINUX
	char temp[MAX_PATH];
#endif

	if(full_path[0] != '\0')
		return full_path;
#ifdef WIN32
	if ( !GetModuleFileNameA(NULL, full_path, MAX_PATH) )
		return "";
#elif FREEBSD
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PATHNAME;
	mib[3] = -1;
	size_t len = sizeof(full_path);
	sysctl(mib, 4, full_path, &len, NULL, 0);

#elif LINUX
	snprintf(temp, sizeof(temp),"/proc/%d/exe", getpid());
	realpath(temp, full_path);
#endif

	full_path_length = strlen(full_path);
	for(last=full_path_length-1; last>=0; last--)
	{
#ifdef WIN32
		if(full_path[last] == '\\') //reverse lookup'\'
#else
		if(full_path[last] == '/') //reverse lookup '/'
#endif
		{
			full_path[last] = '\0' ; //cut file name
			break;
		}
	}
	return full_path;
}

