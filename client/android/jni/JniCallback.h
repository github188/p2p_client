#ifndef __P2P_JNI_CALLBACK_H__
#define __P2P_JNI_CALLBACK_H__

#include <jni.h>
//C����java����ĺ���
void java_callback_void(jobject obj, const char * callback, const char* signature, va_list args);
jboolean jni_attach_thread(JNIEnv** env);
void jni_detach_thread();

#endif
