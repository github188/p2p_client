#ifndef __GSS_MEM_H__
#define __GSS_MEM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>


#define gss_malloc(sz) malloc(sz)
#define gss_calloc(n, sz) calloc((n), (sz))
#define gss_strdup(s) strdup(s)
#define gss_realloc(p, sz) realloc((p), (sz))
#define gss_free(p) free(p)


#ifdef __cplusplus
}
#endif


#endif
