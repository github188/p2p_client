#ifndef __P2P_JNI_LOG_H__
#define __P2P_JNI_LOG_H__

#include <android/log.h>
#define LOG_TAG "p2p"
#define LOG_PRINT(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)

#endif
