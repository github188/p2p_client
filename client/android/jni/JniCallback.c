#include "JniCallback.h"
#include "log.h"
JavaVM *jVM = 0;

//程序启动自动调用一次
jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
	jint res = init_callback_environment(vm);
	return res;
}
//获取java的运行环境
jint init_callback_environment(JavaVM* vm)
{
	JNIEnv* env;
	if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK)
	{
		LOG_PRINT("init_callback_environment: failed to obtain current JNI environment");
		return -1;
	}

	jVM = vm;
	return JNI_VERSION_1_4;
}

//附着java线程
/* attach current thread to jvm */
jboolean jni_attach_thread(JNIEnv** env)
{
	if ((*jVM)->GetEnv(jVM, (void**) env, JNI_VERSION_1_4) != JNI_OK)
	{
		//LOG_PRINT("jni_attach_thread: attaching current thread");

		(*jVM)->AttachCurrentThread(jVM, env, 0);

		if ((*jVM)->GetEnv(jVM, (void**) env, JNI_VERSION_1_4) != JNI_OK)
		{
			LOG_PRINT("jni_attach_thread: failed to obtain current JNI environment");
		}
		//LOG_PRINT("jni_attach_thread: attaching current thread successed");
		return JNI_TRUE;
	}
	return JNI_FALSE;
}

/* attach current thread to JVM */
void jni_detach_thread()
{
	(*jVM)->DetachCurrentThread(jVM);
}

/*C调用java对象的函数
 * callback 函数名称
 * signature 函数标识，就是函数的输入参数类型和返回值类型的简写字符串
 * args 函数参数
 */
void java_callback_void(jobject obj, const char * callback, const char* signature, va_list args)
{
	jclass jObjClass;
	jmethodID jCallback;
	JNIEnv* env;
	jboolean attach;

	//LOG_PRINT("java_callback_void: %s (%s)", callback, signature);
	attach = jni_attach_thread(&env);

	jObjClass = (*env)->GetObjectClass(env, obj);
	if (!jObjClass)
	{
		LOG_PRINT("java_callback_void: failed to get class reference");
		if(attach == JNI_TRUE)
			jni_detach_thread();
		return;
	}
	//LOG_PRINT("java_callback_void: successed to get class reference");

	jCallback = (*env)->GetMethodID(env, jObjClass, callback, signature);
	if (!jCallback)
	{
		LOG_PRINT("java_callback_void: failed to get method id");
		if(attach == JNI_TRUE)
			jni_detach_thread();
		return;
	}

	(*env)->CallVoidMethodV(env, obj, jCallback, args);

	if(attach == JNI_TRUE)
		jni_detach_thread();
}
