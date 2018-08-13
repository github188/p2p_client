#pragma once


#include <pjnath/p2p_pool.h>
#include <pjnath/p2p_tcp.h>
#ifndef USE_P2P_TCP 

#include "udt.h"
#include <fstream>
#include <iostream>
#include <cstring>

// the stream data queue max size
#define STREAM_QUEUE_LENGTH		2048

#define STREAM_QUEUE_RESIZE_TIME	60 // 分钟

#define STREAM_QUEUE_MAX_BUFFER_LEN	1000000

typedef struct _STREAM_DATA
// 数据结构
{
	// 缓存数据大小
	int			DataBufSize					;
	// 缓存大小
	int			BufSize						;	
	// 缓存
	// char		DataBuf[STREAM_MAX_LENGTH]	;
	char*		DataBuf						;
	// 用户数据
	unsigned long		UserData			;
	sockaddr*	addr;
	unsigned addr_len;
	_STREAM_DATA()
	{
		DataBufSize = 0;
		BufSize = 0;
		DataBuf = NULL;
		UserData = 0;
	};

}STREAM_DATA, *PSTREAM_DATA;

#ifndef	QUEUE_STATUS_ENUM
enum _QUEUE_STATUS
{
	QUEUE_STATUS_EMPTY = 0,	// 空
	QUEUE_STATUS_FULL = 1,	// 满
	QUEUE_STATUS_DATA = 2,	// 有数据
	QUEUE_STATUS_OVERFLOW = 3, // 溢出
};
#define QUEUE_STATUS_ENUM
#endif

typedef struct _STREAM_DATA_QUEUE
// 视频帧循环队列
{

	// 数据队列
	STREAM_DATA		StreamData[STREAM_QUEUE_LENGTH]	;

	// 队首
	int				Head							;

	// 队尾
	int				Tail							;

	// 队列状态
	int				State							;

	_STREAM_DATA_QUEUE()
	{
		Head	=	0		;

		Tail	=	0		;

		State	=	QUEUE_STATUS_EMPTY		;
	};

}STREAM_DATA_QUEUE, *PSTREAM_DATA_QUEUE;

typedef unsigned long DWORD;

class CStreamData
{
public:
	CStreamData(void);
	~CStreamData(void);
	
	// input stream queue
	int			En_StreamQueue(const char* pBuffer, const int iBufferLen, DWORD dwUserData, sockaddr* addr, unsigned addr_len);
	// output stream queue
	int			De_StreamQueue(void);
	// is empty the stream queue
	bool		IsEmpty(void);
	// get the first data of the queue
	PSTREAM_DATA GetQueueHead(void);
	int			GetCount();
	void		Release();
private:
	// 数据循环队列
	STREAM_DATA_QUEUE	m_StreamDataQueue;

	DWORD				m_dwResizeDT;
	DWORD				m_dwResizeCount;

	//CBaseCS				m_csData;
};

#endif //USE_P2P_TCP
