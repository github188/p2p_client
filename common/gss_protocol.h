#ifndef __GSS_PROTOCOL__H__
#define __GSS_PROTOCOL__H__

typedef enum tcp_client_type{
	tcp_client_none=-1,
	tcp_client_unknown,
	tcp_dev_main,
	tcp_dev_av,
	tcp_dev_push,
	tcp_client_signaling,
	tcp_client_av,
	tcp_client_pull,
}tcp_client_type;

#define GSS_EX_CMD (255)

#define GSS_DEV_LOGIN_CMD (1)

#define GSS_CLIENT_LOGIN_CMD (2)

#define GSS_DEV_AV_LOGIN_CMD (3)

#define GSS_CLIENT_AV_LOGIN_CMD (4)

#define GSS_SIGNALING_DATA (5)

#define GSS_SIGNALING_CONNECTED (6)

#define GSS_SIGNALING_DISCONNECTED (7)

#define GSS_MAIN_DISCONNECTED (8)

#define GSS_CONNECT_RESULT (9)

#define GSS_KICKOUT (10)

#define GSS_HEART_CMD (11)

#define GSS_AV_DATA (12)

#define GSS_AV_DISCONNECTED (13)

#define GSS_DEV_PUSH_LOGIN_CMD (14)

#define GSS_CLIENT_PULL_LOGIN_CMD (15)

#define GSS_PUSH_VIDEO (16)

#define GSS_PUSH_KEY_VIDEO (17)

#define GSS_PUSH_AUDIO (18)

#define GSS_PUSH_DISCONNECTED (19)

#define GSS_PUSH_RTMP (20)

#define GSS_RTMP_CONNECT_RESULT (21)

#define GSS_RTMP_DISCONNECT (22)

#define GSS_DISPATCH (23)

#define GSS_P2P_DISPATCH (24)

#define GSS_DISPATCH_REQUEST (25)

#define GSS_P2P_DISPATCH_REQUEST (26)

#define GSS_DISPATCH_QUERY (27)

#define GSS_P2P_DISPATCH_QUERY (28)

#define GSS_DISPATCH_RESPONSE (29)

#define GSS_DISPATCH_WATCH (30)

#define GSS_SEND_LIMIT (31)

#define GSS_CLEAN_BUFFER (32)

#define GSS_PULL_STATUS_CHANGED (33)

#define GSS_CUSTOM_BASE (128) //for push custom type,to see gss_dev_push_send
#define GSS_CUSTOM_MAX (GSS_CUSTOM_BASE+32)


#define LAST_DATA_SEQ (0xFF)
#pragma pack(1)
typedef struct GSS_DATA_HEADER
{
	/*data length, data header length is not included*/
	unsigned short len; 

	//command 
	unsigned char cmd;

	 /*if data length more than GSS_MAX_DATA_LEN, long data cut to multiple short data
	   data_seq is sequence number, last is LAST_DATA_SEQ
	 */
	unsigned char data_seq;
}GSS_DATA_HEADER;

#define MAX_UID_LEN (64)
typedef struct GSS_LOGIN_CMD
{
	unsigned char type; //tcp_dev_main
	char uid[MAX_UID_LEN];
}GSS_LOGIN_CMD;

typedef struct GSS_SIGNALING_CONNECT_RESULT_CMD
{
	int result;
	unsigned short index;
}GSS_SIGNALING_CONNECT_RESULT_CMD;

typedef struct GSS_DEVICE_AV_LOGIN_CMD
{
	char uid[MAX_UID_LEN];
	unsigned int index;
}GSS_DEVICE_AV_LOGIN_CMD;

#define MAX_RTMP_URL_LEN (512)
typedef struct GSS_PUSH_RMTP_CMD
{
	char url[MAX_RTMP_URL_LEN];
}GSS_PUSH_RMTP_CMD;

#define GSS_MAX_ADDR_LEN (128)
typedef struct GSS_DISPATCH_CMD
{
	unsigned int id;
	char addr[GSS_MAX_ADDR_LEN];
	unsigned int port;
	unsigned int main_count;
	unsigned int signaling_count;
	unsigned int av_count;
	unsigned int push_count;
	unsigned int pull_count;
}GSS_DISPATCH_CMD;

typedef struct GSS_P2P_DISPATCH_CMD
{
	unsigned int id;
	char addr[MAX_UID_LEN];
	unsigned int port;
	unsigned int conn_count;
}GSS_P2P_DISPATCH_CMD;

typedef struct GSS_DISP_REQUEST 
{
	char user[MAX_UID_LEN];
	char password[MAX_UID_LEN];
}GSS_DISP_REQUEST;

typedef struct GSS_DISP_QUERY
{
	char dest_user[MAX_UID_LEN];
}GSS_DISP_QUERY;

typedef struct GSS_DISP_RESPONSE
{
	int result;
	char server[GSS_MAX_ADDR_LEN];
	unsigned short port;
	unsigned int server_id;
}GSS_DISP_RESPONSE;

#pragma pack()

#define GSS_NO_ERROR (0)

#define GSS_BASE_ERROR (480000)
#define GSS_DEVICE_OFFLINE (GSS_BASE_ERROR+1)
#define GSS_TOO_MANY_CONN (GSS_BASE_ERROR+2)
#define GSS_DEV_KICKOUT (GSS_BASE_ERROR+3)
#define GSS_INVALID_CLINET_INDEX (GSS_BASE_ERROR+4)
#define GSS_CLIENT_OFFLINE (GSS_BASE_ERROR+5)
#define GSS_UID_NO_EQ (GSS_BASE_ERROR+6)
#define GSS_FRAME_TOO_LONG (GSS_BASE_ERROR+7)
#define GSS_NO_ONLINE_SERVER (GSS_BASE_ERROR+8)
#define GSS_INVALID_USER (GSS_BASE_ERROR+10)
#define GSS_ERROR_QUERY_DB (GSS_BASE_ERROR+11)
#define GSS_ERROR_CONNECT_DB (GSS_BASE_ERROR+12)

#define GSS_HEART_SPAN (30) //in second

#define DISP_HEART_SPAN (5) //in second

#define GSS_MAX_CMD_LEN (8192) //max gss command length

//max gss command data length,  data header length is not included
#define GSS_MAX_PREFIX_LEN (60)
#define GSS_MAX_DATA_LEN (GSS_MAX_CMD_LEN-sizeof(GSS_DATA_HEADER)-GSS_MAX_PREFIX_LEN)

#define INVALID_CLINET_INDEX ((unsigned short)-1)

#define GSS_MAX_CACHE_LEN (64) //8192*32 = 256k, a package maybe not 8192 not enough, so set 64


#define MAX_AV_FRAME_LEN (1024*1024) //1 M

#define GSS_MAX_DISPATCH_LEN (1024) //max gss dispatch command length

#define GSS_RECVBUF_RATIO (8)


#endif