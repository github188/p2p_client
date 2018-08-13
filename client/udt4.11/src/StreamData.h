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

#define STREAM_QUEUE_RESIZE_TIME	60 // ����

#define STREAM_QUEUE_MAX_BUFFER_LEN	1000000

typedef struct _STREAM_DATA
// ���ݽṹ
{
	// �������ݴ�С
	int			DataBufSize					;
	// �����С
	int			BufSize						;	
	// ����
	// char		DataBuf[STREAM_MAX_LENGTH]	;
	char*		DataBuf						;
	// �û�����
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
	QUEUE_STATUS_EMPTY = 0,	// ��
	QUEUE_STATUS_FULL = 1,	// ��
	QUEUE_STATUS_DATA = 2,	// ������
	QUEUE_STATUS_OVERFLOW = 3, // ���
};
#define QUEUE_STATUS_ENUM
#endif

typedef struct _STREAM_DATA_QUEUE
// ��Ƶ֡ѭ������
{

	// ���ݶ���
	STREAM_DATA		StreamData[STREAM_QUEUE_LENGTH]	;

	// ����
	int				Head							;

	// ��β
	int				Tail							;

	// ����״̬
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
	// ����ѭ������
	STREAM_DATA_QUEUE	m_StreamDataQueue;

	DWORD				m_dwResizeDT;
	DWORD				m_dwResizeCount;

	//CBaseCS				m_csData;
};

#endif //USE_P2P_TCP
