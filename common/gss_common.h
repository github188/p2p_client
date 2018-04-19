#ifndef __GSS_COMMON_H__
#define __GSS_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

#define GSS_UNUSED_ARG(arg)  (void)arg

#ifdef _WIN32
#define usleep(t) Sleep(t/1000)
#define snprintf _snprintf
#endif

#ifdef __cplusplus
}
#endif


#endif
