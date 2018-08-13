#include "StreamData.h"

#include <pjnath/p2p_pool.h>
#ifndef USE_P2P_TCP 

CStreamData::CStreamData(void)
{
	memset(&m_StreamDataQueue, 0, sizeof(m_StreamDataQueue));

	m_dwResizeCount = 0;
	m_dwResizeDT = 0;
}

CStreamData::~CStreamData(void)
{
	Release();
}

int CStreamData::En_StreamQueue(const char* pBuffer, const int iBufferLen, DWORD dwUserData, sockaddr* addr, unsigned addr_len)
{
	//CAutoLock lk(&m_csData);

	if (pBuffer == NULL || iBufferLen <= 0) return m_StreamDataQueue.State;

	if( m_StreamDataQueue.State != QUEUE_STATUS_FULL )
	{
		PSTREAM_DATA ptmpdata = &m_StreamDataQueue .StreamData [ m_StreamDataQueue.Tail ];
		int idatalen = iBufferLen;

		//DWORD dwCur = timeGetTime();
		//if (m_dwResizeDT == 0)
		//{
		//	m_dwResizeDT = dwCur;
		//}

		//if ((dwCur - m_dwResizeDT) > STREAM_QUEUE_RESIZE_TIME*60*1000)
		//{
		//	// delete buffer
		//	if (ptmpdata->DataBuf != NULL)
		//		delete[] ptmpdata->DataBuf ;
		//	ptmpdata->DataBuf = NULL;
		//	ptmpdata->BufSize = 0;
		//	ptmpdata->DataBufSize = 0;

		//	m_dwResizeCount++;

		//	if (m_dwResizeCount >= STREAM_QUEUE_LENGTH)
		//	{
		//		m_dwResizeCount = 0;
		//		m_dwResizeDT = dwCur;
		//	}
		//}


		if( ptmpdata->BufSize < idatalen )
		{
			// resize
			char* ptmp = new char[idatalen+addr_len];
			if (ptmpdata->DataBuf != NULL)
				delete[] ptmpdata->DataBuf ;
			ptmpdata->DataBuf = ptmp;
			ptmpdata->BufSize = idatalen;
			ptmpdata->DataBufSize = 0;
		}

		// save stream data
		ptmpdata->DataBufSize = iBufferLen;
		memcpy(ptmpdata->DataBuf, pBuffer, iBufferLen);
		ptmpdata->UserData = dwUserData;
		ptmpdata->addr = (sockaddr*)(ptmpdata->DataBuf + iBufferLen);
		memcpy(ptmpdata->addr, addr, addr_len);
		ptmpdata->addr_len = addr_len;
		// add tail
		m_StreamDataQueue .Tail++ ;
		// 溢出循环判断
		if( m_StreamDataQueue .Tail >= STREAM_QUEUE_LENGTH )
			m_StreamDataQueue .Tail -= STREAM_QUEUE_LENGTH;

		// 队列状态判断
		//int i = m_StreamDataQueue.Tail + 1;
		int i = m_StreamDataQueue.Tail;
		if (i >= STREAM_QUEUE_LENGTH) i -= STREAM_QUEUE_LENGTH;
		if (i == m_StreamDataQueue.Head)
		{
			m_StreamDataQueue .State = QUEUE_STATUS_FULL;
			//TRACE("stream data 队满\r\n");
		}
		else m_StreamDataQueue.State = QUEUE_STATUS_DATA;
	}

	return m_StreamDataQueue.State;
}

int CStreamData::De_StreamQueue(void)
{
	//CAutoLock lk(&m_csData);

	if( m_StreamDataQueue.State == QUEUE_STATUS_EMPTY )
		// 队空
		return -1;

	//if (IsEmpty())	return -1;
	
	m_StreamDataQueue.StreamData [ m_StreamDataQueue .Head ].DataBufSize = 0;

	m_StreamDataQueue.Head++;

	// 溢出循环判断
	if( m_StreamDataQueue .Head >= STREAM_QUEUE_LENGTH )
		m_StreamDataQueue .Head -= STREAM_QUEUE_LENGTH;

	// 队空判断
	if(m_StreamDataQueue.Head == m_StreamDataQueue .Tail)
		// 队列空
		m_StreamDataQueue .State = QUEUE_STATUS_EMPTY;
	else m_StreamDataQueue.State = QUEUE_STATUS_DATA;

	return 0;
}

bool CStreamData::IsEmpty(void)
{
	//CAutoLock lk(&m_csData);

	bool bRet  = true;

	if ( m_StreamDataQueue.State != QUEUE_STATUS_EMPTY  )
		// 队列不为空
	{
		bRet = false;
	}

	return bRet;
}

PSTREAM_DATA CStreamData::GetQueueHead(void)
{
	//CAutoLock lk(&m_csData);

	PSTREAM_DATA pQueueHead = NULL;

	if( !IsEmpty() )
		pQueueHead = &m_StreamDataQueue .StreamData[ m_StreamDataQueue .Head ];

	return pQueueHead;
}


int	CStreamData::GetCount()
{
	//CAutoLock lk(&m_csData);

	int iRet = 0;

	if(IsEmpty())
		return iRet;

	if(m_StreamDataQueue.State == QUEUE_STATUS_FULL)
		iRet = STREAM_QUEUE_LENGTH;
	else
	{
		iRet = m_StreamDataQueue.Tail - m_StreamDataQueue.Head;
		if(iRet < 0) iRet += STREAM_QUEUE_LENGTH;
	}

	return iRet;
}

void CStreamData::Release()
{
	//CAutoLock lk(&m_csData);

	for( int i = 0; i < STREAM_QUEUE_LENGTH; i++ )
	{
		if (m_StreamDataQueue.StreamData[i].DataBuf !=NULL)
			delete[] m_StreamDataQueue.StreamData[i].DataBuf ;
		m_StreamDataQueue.StreamData[i].DataBuf = NULL;
		m_StreamDataQueue.StreamData[i].BufSize = 0;
		m_StreamDataQueue.StreamData[i].DataBufSize = 0;
	}
	m_StreamDataQueue.State = QUEUE_STATUS_EMPTY;
	m_StreamDataQueue.Head = 0;
	m_StreamDataQueue.Tail = 0;

	m_dwResizeCount = 0;
	m_dwResizeDT = 0;
}
#endif //USE_P2P_TCP