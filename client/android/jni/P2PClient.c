#include "JniCallback.h"
#include <p2p_transport.h>
#include <p2p_dispatch.h>
#include <stdlib.h>
#include<pj/config.h>
#include <stdio.h>
#include "log.h"

typedef struct P2PClient {
	p2p_transport *transport;
	jobject java_callback; //java object
	int user_data;
} P2PClient;

#define MAX_ERROR_STRING_LEN 256

P2PClient* malloc_client() {
	P2PClient* client = malloc(sizeof(P2PClient));
	client->transport = 0;
	client->java_callback = 0;
	client->user_data = 0;
	return client;
}

void free_client(P2PClient* client) {
	free(client);
}

static void log_func(const char *data, int len) {
	LOG_PRINT("%s", data);
}

jobject returnP2PResult(JNIEnv* env, int result, int value) {

	jclass p2PResultClass = (*env)->FindClass(env, "com/yhzl/p2p/P2PResult");
	jmethodID constrocMID = (*env)->GetMethodID(env, p2PResultClass, "<init>",
			"(II)V");
	jobject returnObj = (*env)->NewObject(env, p2PResultClass, constrocMID,
			result, value);
	return returnObj;
}

void call_back_on_connection_disconnect(P2PClient* client, ...) {
	va_list vl;
	va_start(vl, client);
	java_callback_void(client->java_callback, "on_connection_disconnect",
			"(IIII)V", vl);
	va_end(vl);
}

static void on_connection_disconnect(p2p_transport *transport,
		int connection_id, void *transport_user_data, void *connect_user_data) {
	P2PClient* client = (P2PClient*) transport_user_data;
	LOG_PRINT("p2p connection is disconnected %d\r\n", connection_id);
	call_back_on_connection_disconnect(client, (int) client, connection_id,
			client->user_data, (int) connect_user_data);
}

void call_back_on_create_complete(P2PClient* client, ...) {
	va_list vl;
	va_start(vl, client);
	java_callback_void(client->java_callback, "on_create_complete", "(III)V",
			vl);
	va_end(vl);
}
static void on_create_complete(p2p_transport *transport, int status,
		void *user_data) {
	P2PClient* client = (P2PClient*) user_data;
	if (status == P2P_SUCCESS) {
		LOG_PRINT("p2p connect server successful\r\n");
	} else {
		char errmsg[MAX_ERROR_STRING_LEN];
		p2p_strerror(status, errmsg, sizeof(errmsg));
		LOG_PRINT("p2p connect server failed: %s\r\n", errmsg);
	}
	call_back_on_create_complete(client, (int) client, status,
			client->user_data);
}

void call_back_on_connect_complete(P2PClient* client, ...) {
	va_list vl;
	va_start(vl, client);
	java_callback_void(client->java_callback, "on_connect_complete", "(IIIII)V",
			vl);
	va_end(vl);
}
static void on_connect_complete(p2p_transport *transport, int connection_id,
		int status, void *transport_user_data, void *connect_user_data) {
	P2PClient* client = (P2PClient*) transport_user_data;
	if (status == P2P_SUCCESS) {
		LOG_PRINT(
				"p2p connect remote user successful, connection id %d\r\n", connection_id);
	} else {
		char errmsg[MAX_ERROR_STRING_LEN];
		p2p_strerror(status, errmsg, sizeof(errmsg));
		LOG_PRINT(
				"p2p connect remote user failed: %s, connection id %d\r\n", errmsg, connection_id);
	}
	call_back_on_connect_complete(client, (int) client, connection_id, status,
			client->user_data, (int) connect_user_data);

}

void call_back_on_accept_remote_connection(P2PClient* client, ...) {
	va_list vl;
	va_start(vl, client);
	java_callback_void(client->java_callback, "on_accept_remote_connection",
			"(III)V", vl);
	va_end(vl);
}
static void on_accept_remote_connection(p2p_transport *transport,
		int connection_id, int conn_flag, void *transport_user_data) {
	P2PClient* client = (P2PClient*) transport_user_data;
	LOG_PRINT("accept remote connection %d", connection_id);
	call_back_on_accept_remote_connection(client, (int) client, connection_id,
			client->user_data);
}

void call_back_on_connection_recv(P2PClient* client, ...) {
	va_list vl;
	va_start(vl, client);
	java_callback_void(client->java_callback, "on_connection_recv", "(IIII[B)V",
			vl);
	va_end(vl);
}
void on_connection_recv(p2p_transport *transport, int connection_id,
		void *transport_user_data, void *connect_user_data, char* data, int len) {
	P2PClient* client = (P2PClient*) transport_user_data;
	jbyteArray jdata;
	JNIEnv* env;
	jboolean attach;
	LOG_PRINT("on_connection_recv %p %d %d\r\n", transport, connection_id, len);

	attach = jni_attach_thread(&env);
	if (attach == JNI_TRUE) {
		jdata = (*env)->NewByteArray(env, len);
		(*env)->SetByteArrayRegion(env, jdata, 0, len, data);

		call_back_on_connection_recv(client, (int) client, connection_id,
				client->user_data, (int) connect_user_data, jdata);
		jni_detach_thread();
	}
}

jint Java_com_yhzl_p2p_P2PClient_init(JNIEnv* env, jobject thiz) {
	//LOG_PRINT("Java_com_yhzl_p2p_P2PClient_init begin, %s\n", PJ_M_NAME);
	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_init begin");
	p2p_init(log_func);
	//p2p_log_set_level(6);
	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_init end\n");
	return 0;
}

void Java_com_yhzl_p2p_P2PClient_uninit(JNIEnv* env, jobject thiz) {
	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_uninit begin\n");
	p2p_uninit();
	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_uninit end\n");
}

jobject Java_com_yhzl_p2p_P2PClient_p2p_1transport_1create(JNIEnv* env,
		jobject thiz, jstring server, int port, jstring user, jstring password,
		int terminal_type, int user_data, int use_tcp_connect_srv,
		jstring proxy_addr, jobject java_cb) {
	P2PClient* client = 0;
	p2p_transport_cb cb;
	int status;
	p2p_transport_cfg cfg;
	const char* utf_server;
	const char* utf_user;
	const char* utf_pwd;
	const char* utf_proxy_addr;

	if (server == 0 || user == 0 || password == 0)
		return returnP2PResult(env, -1, 0);

	memset(&cfg, 0, sizeof(cfg));

	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1transport_1create begin\n");

	client = malloc_client();

	memset(&cb, 0, sizeof(cb));
	cb.on_connect_complete = on_connect_complete;
	cb.on_create_complete = on_create_complete;
	cb.on_connection_disconnect = on_connection_disconnect;
	cb.on_accept_remote_connection = on_accept_remote_connection;
	cb.on_connection_recv = on_connection_recv;
	cfg.cb = &cb;
	cfg.terminal_type = terminal_type;

	utf_server = (*env)->GetStringUTFChars(env, server, 0);
	utf_user = (*env)->GetStringUTFChars(env, user, 0);
	utf_pwd = (*env)->GetStringUTFChars(env, password, 0);
	if (proxy_addr)
		utf_proxy_addr = (*env)->GetStringUTFChars(env, proxy_addr, 0);
	else
		utf_proxy_addr = 0;

	cfg.server = (char*) utf_server;
	cfg.port = (unsigned short) port;
	cfg.user = (char*) utf_user;
	cfg.password = (char*) utf_pwd;
	cfg.user_data = client;
	cfg.use_tcp_connect_srv = use_tcp_connect_srv;
	cfg.proxy_addr = (char*) utf_proxy_addr;
	client->user_data = user_data;

	LOG_PRINT("utf_proxy_addr %s\n", utf_proxy_addr);
	status = p2p_transport_create(&cfg, &client->transport);

	(*env)->ReleaseStringUTFChars(env, server, utf_server);
	(*env)->ReleaseStringUTFChars(env, user, utf_user);
	(*env)->ReleaseStringUTFChars(env, password, utf_pwd);
	if (proxy_addr)
		(*env)->ReleaseStringUTFChars(env, proxy_addr, utf_proxy_addr);

	if (status != P2P_SUCCESS) {
		LOG_PRINT(
				"Java_com_yhzl_p2p_P2PClient_p2p_1transport_1create status %d\n", status);
		free_client(client);
		return returnP2PResult(env, status, 0);
	}

	client->java_callback = (*env)->NewGlobalRef(env, java_cb);

	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1transport_1create end\n");

	return returnP2PResult(env, 0, (int) client);
}

void Java_com_yhzl_p2p_P2PClient_p2p_1transport_1destroy(JNIEnv* env,
		jobject thiz, int transport) {

	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1transport_1destroy begin\n");
	P2PClient* client = (P2PClient*) transport;
	if (client && client->transport) {
		p2p_transport_destroy(client->transport);
		(*env)->DeleteGlobalRef(env, client->java_callback);
		free_client(client);
	}
	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1transport_1destroy end\n");
}

jobject Java_com_yhzl_p2p_P2PClient_p2p_1transport_1connect(JNIEnv* env,
		jobject thiz, int transport, jstring user, int user_data) {

	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1transport_1connect begin\n");

	P2PClient* client = (P2PClient*) transport;
	if (client && client->transport && user) {
		int conn_id = 0;
		const char const * utf_user = (*env)->GetStringUTFChars(env, user, 0);
		int status = p2p_transport_connect(client->transport, (char*) utf_user,
				(void*) user_data, 0, &conn_id);
		(*env)->ReleaseStringUTFChars(env, user, utf_user);
		LOG_PRINT(
				"Java_com_yhzl_p2p_P2PClient_p2p_1transport_1connect %d %d\n", status, conn_id);
		return returnP2PResult(env, status, conn_id);
	}

	LOG_PRINT(
			"Java_com_yhzl_p2p_P2PClient_p2p_1transport_1connect invalid transport\n");
	return returnP2PResult(env, -1, 0);
}

void Java_com_yhzl_p2p_P2PClient_p2p_1transport_1disconnect(JNIEnv* env,
		jobject thiz, int transport, int conn_id) {
	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1transport_1disconnect begin\n");

	P2PClient* client = (P2PClient*) transport;
	if (client && client->transport) {
		p2p_transport_disconnect(client->transport, conn_id);
	}

	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1transport_1disconnect end\n");
}

jobject Java_com_yhzl_p2p_P2PClient_p2p_1create_1tcp_1proxy(JNIEnv* env,
		jobject thiz, int transport, int conn_id, int remote_listen_port) {
	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1create_1tcp_1proxy begin\n");

	P2PClient* client = (P2PClient*) transport;
	if (client && client->transport) {
		unsigned short local_port = 0;
		int status = p2p_create_tcp_proxy(client->transport, conn_id,
				remote_listen_port, &local_port);
		LOG_PRINT(
				"Java_com_yhzl_p2p_P2PClient_p2p_1create_1tcp_1proxy %d %d\n", status, local_port);
		return returnP2PResult(env, status, local_port);
	}

	LOG_PRINT(
			"Java_com_yhzl_p2p_P2PClient_p2p_1create_1tcp_1proxy invalid transport\n");
	return returnP2PResult(env, -1, 0);
}

void Java_com_yhzl_p2p_P2PClient_p2p_1destroy_1tcp_1proxy(JNIEnv* env,
		jobject thiz, int transport, int conn_id, int local_proxy_port) {
	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1destroy_1tcp_1proxy begin\n");

	P2PClient* client = (P2PClient*) transport;
	if (client && client->transport) {
		p2p_destroy_tcp_proxy(client->transport, conn_id, local_proxy_port);
	}

	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1destroy_1tcp_1proxy end\n");
}

jstring Java_com_yhzl_p2p_P2PClient_p2p_1strerror(JNIEnv* env, jobject thiz,
		int error) {
	char errmsg[MAX_ERROR_STRING_LEN] = { 0 };
	p2p_strerror(error, errmsg, sizeof(errmsg));
	return (*env)->NewStringUTF(env, errmsg);
}

jobject Java_com_yhzl_p2p_P2PClient_p2p_1transport_1send(JNIEnv* env,
		jobject thiz, int transport, int conn_id, jbyteArray jbuffer,
		int jbuffer_len) {
	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1transport_1send begin\n");

	P2PClient* client = (P2PClient*) transport;
	if (client && client->transport) {
		int error = 0;
		char *buf = (char*) (*env)->GetByteArrayElements(env, jbuffer, 0);
		int sended = p2p_transport_send(client->transport, conn_id, buf,
				jbuffer_len, P2P_SEND_BLOCK, &error);

		(*env)->ReleaseByteArrayElements(env, jbuffer, buf, 0);
		LOG_PRINT(
				"Java_com_yhzl_p2p_P2PClient_p2p_1transport_1send %d %d\n", sended, error);
		return returnP2PResult(env, sended, error);
	}
	LOG_PRINT(
			"Java_com_yhzl_p2p_P2PClient_p2p_1transport_1send invalid transport\n");
	return returnP2PResult(env, -1, 0);
}

typedef struct java_nat_type_detector {
	jobject jobj;
} java_nat_type_detector;

void call_back_nat_type(jobject obj, ...) {
	va_list vl;
	va_start(vl, obj);
	java_callback_void(obj, "on_detect_nat", "(II)V", vl);
	va_end(vl);
}

void on_java_nat_type(int status, int nat_type, void* user_data) {
	JNIEnv* env;
	jboolean attach;
	java_nat_type_detector* d = user_data;
	LOG_PRINT("on_java_nat_type %d %d\r\n", status, nat_type);

	attach = jni_attach_thread(&env);
	if (attach == JNI_TRUE) {
		call_back_nat_type(d->jobj, status, nat_type);
		(*env)->DeleteGlobalRef(env, d->jobj);
		jni_detach_thread();
	}
	free(d);
}

int Java_com_yhzl_p2p_P2PClient_p2p_1nat_1type_1detect(JNIEnv* env,
		jobject thiz, jstring server, int port, jobject java_cb) {
	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1nat_1type_1detect begin\n");

	java_nat_type_detector* d = malloc(sizeof(java_nat_type_detector));
	const char const * utf_server = (*env)->GetStringUTFChars(env, server, 0);

	d->jobj = (*env)->NewGlobalRef(env, java_cb);
	int status = p2p_nat_type_detect((char*) utf_server, port, on_java_nat_type,
			d);

	(*env)->ReleaseStringUTFChars(env, server, utf_server);

	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1nat_1type_1detect end\n");
	return status;
}

jstring Java_com_yhzl_p2p_P2PClient_p2p_1get_1conn_1remote_1addr(JNIEnv* env,
		jobject thiz, int transport, int conn_id) {
	char addr[256] = { 0 };
	char ret[256] = { 0 };
	int len = sizeof(addr);
	P2PClient* client = (P2PClient*) transport;
	p2p_addr_type addr_type = P2P_ADDR_TYPE_HOST;
	if (client && client->transport) {
		if (p2p_get_conn_remote_addr(client->transport, conn_id, addr, &len,
				&addr_type) == P2P_SUCCESS)
			sprintf(ret, "%s:%d", addr, addr_type);
	}
	return (*env)->NewStringUTF(env, ret);
}

jstring Java_com_yhzl_p2p_P2PClient_p2p_1get_1conn_1local_1addr(JNIEnv* env,
		jobject thiz, int transport, int conn_id) {
	char addr[256] = { 0 };
	char ret[256] = { 0 };
	int len = sizeof(addr);
	P2PClient* client = (P2PClient*) transport;
	p2p_addr_type addr_type = P2P_ADDR_TYPE_HOST;
	if (client && client->transport) {
		if (p2p_get_conn_local_addr(client->transport, conn_id, addr, &len,
				&addr_type) == P2P_SUCCESS)
			sprintf(ret, "%s:%d", addr, addr_type);
	}
	return (*env)->NewStringUTF(env, ret);
}

int Java_com_yhzl_p2p_P2PClient_p2p_1conn_1set_1buf_1size(JNIEnv* env,
		jobject thiz, int transport, int conn_id, int opt, int size) {
	P2PClient* client = (P2PClient*) transport;
	return p2p_set_conn_opt(client->transport, conn_id, opt, &size, sizeof(int));
}

typedef struct P2PDispatchClient {
	jobject java_callback; //java object
	int user_data;
	void* dispatcher;
} P2PDispatchClient;

P2PDispatchClient* malloc_dispatch_client() {
	P2PDispatchClient* client = malloc(sizeof(P2PDispatchClient));
	client->java_callback = 0;
	client->user_data = 0;
	client->dispatcher = 0;
	return client;
}

void free_dispatch_client(P2PDispatchClient* client) {
	if(client->dispatcher)
		destroy_p2p_dispatch_requester(client->dispatcher);
	free(client);
}

void call_back_ds_callback(P2PDispatchClient* client, ...) {
	va_list vl;
	va_start(vl, client);
	java_callback_void(client->java_callback, "on_dispatch_result",
			"(IILjava/lang/String;II)V", vl);
	va_end(vl);
}
void ds_callback(void* dispatcher, int status, void* user_data, char* server, unsigned short port,
		unsigned int server_id) {
	P2PDispatchClient* client = (P2PDispatchClient*) user_data;
	JNIEnv* env;
	jboolean attach;

	LOG_PRINT("ds_callback %d %s %d %d\r\n", status, server, port, server_id);

	attach = jni_attach_thread(&env);
	if (attach == JNI_TRUE) {
		jstring strResult;
		if (server)
			strResult = (*env)->NewStringUTF(env, server);
		else
			strResult = (*env)->NewStringUTF(env, "");
		call_back_ds_callback(client, status, client->user_data, strResult,
				port, server_id);
		(*env)->DeleteLocalRef(env, strResult);
		jni_detach_thread();
	}
	free_dispatch_client(client);
}

int Java_com_yhzl_p2p_P2PClient_p2p_1request_1dispatch_1server(JNIEnv* env,
		jobject thiz, jstring user, jstring password, jstring ds_addr,
		int user_data, jobject cb) {
	const char* utf_server;
	const char* utf_user;
	const char* utf_pwd;
	int result = 0;
	P2PDispatchClient* client;

	LOG_PRINT(
			"Java_com_yhzl_p2p_P2PClient_p2p_1request_1dispatch_1server begin\n");

	if (ds_addr == 0 || user == 0 || password == 0)
		return -1;

	client = malloc_dispatch_client();
	client->user_data = user_data;
	client->java_callback = (*env)->NewGlobalRef(env, cb);

	utf_server = (*env)->GetStringUTFChars(env, ds_addr, 0);
	utf_user = (*env)->GetStringUTFChars(env, user, 0);
	utf_pwd = (*env)->GetStringUTFChars(env, password, 0);

	result = p2p_request_dispatch_server((char*) utf_user, (char*) utf_pwd,
			(char*) utf_server, client, ds_callback, &client->dispatcher);

	(*env)->ReleaseStringUTFChars(env, ds_addr, utf_server);
	(*env)->ReleaseStringUTFChars(env, user, utf_user);
	(*env)->ReleaseStringUTFChars(env, password, utf_pwd);
	if (result != 0)
		free_dispatch_client(client);

	LOG_PRINT(
			"Java_com_yhzl_p2p_P2PClient_p2p_1request_1dispatch_1server end\n");
	return result;
}

int Java_com_yhzl_p2p_P2PClient_p2p_1query_1dispatch_1server(JNIEnv* env,
		jobject thiz, jstring dest_user, jstring ds_addr, int user_data,
		jobject cb) {
	const char* utf_server;
	const char* utf_dest_user;
	int result = 0;
	P2PDispatchClient* client;

	LOG_PRINT(
			"Java_com_yhzl_p2p_P2PClient_p2p_1query_1dispatch_1server begin\n");

	if (ds_addr == 0 || dest_user == 0)
		return -1;

	client = malloc_dispatch_client();
	client->user_data = user_data;
	client->java_callback = (*env)->NewGlobalRef(env, cb);

	utf_server = (*env)->GetStringUTFChars(env, ds_addr, 0);
	utf_dest_user = (*env)->GetStringUTFChars(env, dest_user, 0);

	result = p2p_query_dispatch_server((char*) utf_dest_user,
			(char*) utf_server, client, ds_callback, &client->dispatcher);

	(*env)->ReleaseStringUTFChars(env, ds_addr, utf_server);
	(*env)->ReleaseStringUTFChars(env, dest_user, utf_dest_user);

	if (result != 0)
		free_dispatch_client(client);

	LOG_PRINT("Java_com_yhzl_p2p_P2PClient_p2p_1query_1dispatch_1server end\n");
	return result;
}
