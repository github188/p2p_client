#pragma once
#ifdef __cplusplus
extern "C" {
#endif

extern int opterr;        /* if error message should be printed */
extern int optind;        /* index into parent argv vector */
extern int optopt;            /* character checked for validity */
extern int optreset;        /* reset getopt */
extern char    *optarg;        /* argument associated with option */
int getopt(int argc, char * const argv[], const char *optstring);

#ifdef __cplusplus
}    
#endif