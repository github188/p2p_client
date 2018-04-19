/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
****************************************************************************/

/****************************************************************************
written by
   Yunhong Gu, last updated 01/27/2011
*****************************************************************************/

#ifndef WIN32
	#include <netdb.h>
	#include <arpa/inet.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <cstring>
	#include <cstdio>
	#include <cerrno>
#else
	#include <winsock2.h>
	#include <ws2tcpip.h>
		#ifdef LEGACY_WIN32
	#include <wspiapi.h>
	#endif
#endif

#include "channel.h"
#include "packet.h"
#include <pjnath/p2p_pool.h>

#include <pjnath/p2p_pool.h>
#ifndef USE_P2P_TCP 

#ifndef WIN32
	#define P2P_RECV_TIMEOUT 10000
#else
	#define P2P_RECV_TIMEOUT 10
#endif


namespace UDT_P2P
{


CChannel::CChannel():
m_iIPversion(AF_INET),
m_iSockAddrSize(sizeof(sockaddr_in)),
m_iSndBufSize(65536),
m_iRcvBufSize(65536)
{
	memset(&m_p2pCb, 0, sizeof(p2p_socket_cb));
#ifndef WIN32
	pthread_cond_init(&m_recvedBufCond, NULL);
	pthread_mutex_init(&m_recvedBufLock, NULL);
#else
	m_recvedBufLock = CreateMutex(NULL, false, NULL);
	m_recvedBufCond = CreateEvent(NULL, false, false, NULL);
	m_sendBufLock = CreateMutex(NULL, false, NULL);
#endif

#ifdef WIN32
	for(int i=0; i<UDT_CHANNEL_SEND_BUFFER_COUNT; i++)
	{
		m_sendBuffers[i].buf = NULL;
		m_sendBuffers[i].len = 0;
	}
	m_sendBufferIndex = 0;
#endif
}

CChannel::CChannel(int version):
m_iSockAddrSize(sizeof(sockaddr_in)),
m_iIPversion(version),
m_iSndBufSize(65536),
m_iRcvBufSize(65536)
{
   memset(&m_p2pCb, 0, sizeof(p2p_socket_cb));

#ifndef WIN32
   pthread_cond_init(&m_recvedBufCond, NULL);
   pthread_mutex_init(&m_recvedBufLock, NULL);
#else
   m_recvedBufLock = CreateMutex(NULL, false, NULL);
   m_recvedBufCond = CreateEvent(NULL, false, false, NULL);
   m_sendBufLock = CreateMutex(NULL, false, NULL);
#endif

#ifdef WIN32
   for(int i=0; i<UDT_CHANNEL_SEND_BUFFER_COUNT; i++)
   {
	   m_sendBuffers[i].buf = NULL;
	   m_sendBuffers[i].len = 0;
   }
   m_sendBufferIndex = 0;
#endif
}

CChannel::~CChannel()
{
#ifndef WIN32
	pthread_cond_destroy(&m_recvedBufCond);
	pthread_mutex_destroy(&m_recvedBufLock);
#else
	CloseHandle(m_recvedBufLock);
	CloseHandle(m_recvedBufCond);
	CloseHandle(m_sendBufLock);
#endif
#ifdef WIN32
	for(int i=0; i<UDT_CHANNEL_SEND_BUFFER_COUNT; i++)
	{
		if(m_sendBuffers[i].buf)
			free(m_sendBuffers[i].buf);
		
	}
#endif
}

void CChannel::open(const sockaddr* addr)
{
	(void)addr;
}

void CChannel::open(UDPSOCKET udpsock)
{
   (void)udpsock;
}

void CChannel::setUDPSockOpt()
{
   
}

void CChannel::close() const
{
   
}

int CChannel::getSndBufSize()
{
   return m_iSndBufSize;
}

int CChannel::getRcvBufSize()
{
   return m_iRcvBufSize;
}

void CChannel::setSndBufSize(int size)
{
   m_iSndBufSize = size;
}

void CChannel::setRcvBufSize(int size)
{
   m_iRcvBufSize = size;
}

void CChannel::getSockAddr(sockaddr* addr) const
{
   if(m_p2pCb.get_sock_addr)
	   (*m_p2pCb.get_sock_addr)(addr, m_p2pCb.user_data);
}

void CChannel::getPeerAddr(sockaddr* addr) const
{
	if(m_p2pCb.get_peer_addr)
		(*m_p2pCb.get_peer_addr)(addr, m_p2pCb.user_data);
}

#ifdef WIN32
UDT_CHANNE_SEND_BUFFER* CChannel::allocSendBuffer(int len)
{
	UDT_CHANNE_SEND_BUFFER* buffer;
	CGuard guard(m_sendBufLock);
	if(m_sendBuffers[m_sendBufferIndex].buf==NULL)
	{
		m_sendBuffers[m_sendBufferIndex].buf = (char *)malloc(len);
		m_sendBuffers[m_sendBufferIndex].len = len;
	}
	else
	{
		if(m_sendBuffers[m_sendBufferIndex].len < len)
		{
			free(m_sendBuffers[m_sendBufferIndex].buf);
			m_sendBuffers[m_sendBufferIndex].buf = (char *)malloc(len);
			m_sendBuffers[m_sendBufferIndex].len = len;
		}
	}
	buffer = &m_sendBuffers[m_sendBufferIndex++];
	if(m_sendBufferIndex >= UDT_CHANNEL_SEND_BUFFER_COUNT)
		m_sendBufferIndex -= UDT_CHANNEL_SEND_BUFFER_COUNT;
	return buffer;
}
#endif

int CChannel::sendto(const sockaddr* addr, CPacket& packet)// const
{
   // convert control information into network order
   if (packet.getFlag())
      for (int i = 0, n = packet.getLength() / 4; i < n; ++ i)
         *((uint32_t *)packet.m_pcData + i) = htonl(*((uint32_t *)packet.m_pcData + i));

   // convert packet header into network order
   uint32_t* p = packet.m_nHeader;
   for (int j = 0; j < 4; ++ j)
   {
      *p = htonl(*p);
      ++ p;
   }

   int res = 0;
   if (m_p2pCb.send_cb)
   {
	   if(packet.ice_socket == 0 ) //if use stun, use src_sock send 
	   {
#ifdef WIN32 //for WSASend, hold the buffer
		   UDT_CHANNE_SEND_BUFFER* b = allocSendBuffer(packet.m_PacketVector[0].iov_len+packet.m_PacketVector[1].iov_len);
		   int buflen = 0;
		   memcpy(b->buf, packet.m_PacketVector[0].iov_base, packet.m_PacketVector[0].iov_len);
		   buflen += packet.m_PacketVector[0].iov_len;
		   memcpy(b->buf + buflen, packet.m_PacketVector[1].iov_base, packet.m_PacketVector[1].iov_len);
		   buflen += packet.m_PacketVector[1].iov_len;
		   res = (*m_p2pCb.send_cb)(addr, b->buf, buflen, m_p2pCb.user_data);
#else
		   int buflen = 0;
		   char send_buffer[65536];
		   memcpy(send_buffer, packet.m_PacketVector[0].iov_base, packet.m_PacketVector[0].iov_len);
		   buflen += packet.m_PacketVector[0].iov_len;
		   memcpy(send_buffer + buflen, packet.m_PacketVector[1].iov_base, packet.m_PacketVector[1].iov_len);
		   buflen += packet.m_PacketVector[1].iov_len;
		   res = (*m_p2pCb.send_cb)(addr, send_buffer, buflen, m_p2pCb.user_data);
#endif
	   }
	   else
	   {
#ifndef WIN32
		   msghdr mh;
		   mh.msg_name = (sockaddr*)addr;
		   mh.msg_namelen = m_iSockAddrSize;
		   mh.msg_iov = (iovec*)packet.m_PacketVector;
		   mh.msg_iovlen = 2;
		   mh.msg_control = NULL;
		   mh.msg_controllen = 0;
		   mh.msg_flags = 0;

		   int res = ::sendmsg(packet.ice_socket, &mh, 0);
#else
		   DWORD size = CPacket::m_iPktHdrSize + packet.getLength();
		   int addrsize = m_iSockAddrSize;
		   int res = ::WSASendTo(packet.ice_socket, (LPWSABUF)packet.m_PacketVector, 2, &size, 0, addr, addrsize, NULL, NULL);
		   res = (0 == res) ? size : -1;
#endif
	   }
   }

   // convert back into local host order
   p = packet.m_nHeader;
   for (int k = 0; k < 4; ++ k)
   {
      *p = ntohl(*p);
       ++ p;
   }

   if (packet.getFlag())
   {
      for (int l = 0, n = packet.getLength() / 4; l < n; ++ l)
         *((uint32_t *)packet.m_pcData + l) = ntohl(*((uint32_t *)packet.m_pcData + l));
   }

   return res;
}

void CChannel::GetPackageFromRecvedBuffer(sockaddr* addr, CPacket& packet, int& res, DWORD& size)
{
#ifdef USE_STREAM_DATA
	PSTREAM_DATA pdata = m_recvedBuf.GetQueueHead();
	memcpy(packet.m_PacketVector[0].iov_base, pdata->DataBuf, CPacket::m_iPktHdrSize);
	/*memcpy(packet.m_PacketVector[1].iov_base,
		pdata->DataBuf + CPacket::m_iPktHdrSize, 
		pdata->DataBufSize - CPacket::m_iPktHdrSize);*/
	packet.m_PacketVector[1].iov_base = pdata->DataBuf + CPacket::m_iPktHdrSize;
	res = 0;
	size = pdata->DataBufSize;
	memcpy(addr, pdata->addr, pdata->addr_len);
	m_recvedBuf.De_StreamQueue();
#else
	UDT_CHANNE_RECV_BUFFER* recvBuf = m_recvedBuf.front();
	char* data = (char*)recvBuf+sizeof(UDT_CHANNE_RECV_BUFFER);
	memcpy(packet.m_PacketVector[0].iov_base, data, CPacket::m_iPktHdrSize);
	memcpy(packet.m_PacketVector[1].iov_base,
		data + CPacket::m_iPktHdrSize, 
		recvBuf->data_len - CPacket::m_iPktHdrSize);
	res = 0;
	size = recvBuf->data_len;
	memcpy(addr, data+recvBuf->data_len, recvBuf->addr_len);
	m_recvedBuf.pop_front();
	p2p_free(recvBuf);
#endif
}

int CChannel::recvfrom(sockaddr* addr, CPacket& packet) 
{
   int res = -1;
   DWORD size = CPacket::m_iPktHdrSize + packet.getLength();
    
   {
	   CGuard guard(m_recvedBufLock);
#ifdef USE_STREAM_DATA
	   if (m_recvedBuf.IsEmpty())
#else
	   if(m_recvedBuf.size() == 0)
#endif
	   {
#ifndef WIN32
		   timeval now;
		   timespec timeout;
		   gettimeofday(&now, 0);
		   if (now.tv_usec < (1000000-P2P_RECV_TIMEOUT))
		   {
			   timeout.tv_sec = now.tv_sec;
			   timeout.tv_nsec = (now.tv_usec + P2P_RECV_TIMEOUT) << 10; //1000 optimize to 1024
		   }
		   else
		   {
			   timeout.tv_sec = now.tv_sec + 1;
			   timeout.tv_nsec = (now.tv_usec + P2P_RECV_TIMEOUT - 1000000) << 10;//1000 optimize to 1024
		   }
		   pthread_cond_timedwait(&m_recvedBufCond, &m_recvedBufLock, &timeout);
#else
		   ReleaseMutex(m_recvedBufLock);
		   WaitForSingleObject(m_recvedBufCond, P2P_RECV_TIMEOUT);
		   WaitForSingleObject(m_recvedBufLock, INFINITE);
#endif

#ifdef USE_STREAM_DATA
		   if (m_recvedBuf.IsEmpty())
#else
		   if(m_recvedBuf.size() == 0)
#endif
			   res = -1;
		   else
			   GetPackageFromRecvedBuffer(addr, packet, res, size);
	   }
	   else
		   GetPackageFromRecvedBuffer(addr, packet, res, size);
   }
   
   res = (0 == res) ? size : -1;
   if (res <= 0)
   {
      packet.setLength(-1);
      return -1;
   }

   packet.setLength(res - CPacket::m_iPktHdrSize);

   // convert back into local host order
   //for (int i = 0; i < 4; ++ i)
   //   packet.m_nHeader[i] = ntohl(packet.m_nHeader[i]);
   uint32_t* p = packet.m_nHeader;
   for (int i = 0; i < 4; ++ i)
   {
      *p = ntohl(*p);
      ++ p;
   }

   if (packet.getFlag())
   {
      for (int j = 0, n = packet.getLength() / 4; j < n; ++ j)
         *((uint32_t *)packet.m_pcData + j) = ntohl(*((uint32_t *)packet.m_pcData + j));
   }

   return packet.getLength();
}

int CChannel::OnP2pDataRecved(const char* buf, int buf_len, sockaddr* addr, unsigned addr_len)
{
	CGuard guard(m_recvedBufLock);
#ifdef USE_STREAM_DATA
	m_recvedBuf.En_StreamQueue(buf, buf_len, 0, addr, addr_len);
#else
	UDT_CHANNE_RECV_BUFFER* recvBuf = (UDT_CHANNE_RECV_BUFFER*)p2p_malloc(sizeof(UDT_CHANNE_RECV_BUFFER)+buf_len+addr_len);
	recvBuf->data_len = buf_len;
	recvBuf->addr_len = addr_len;
	char* data = (char*)recvBuf+sizeof(UDT_CHANNE_RECV_BUFFER);
	memcpy(data, buf, buf_len);
	memcpy(data+buf_len, addr, addr_len);
	m_recvedBuf.push_back(recvBuf);
#endif

#ifndef WIN32
	pthread_cond_signal(&m_recvedBufCond);
#else
	SetEvent(m_recvedBufCond);
#endif
	return buf_len;
}
}
#endif //USE_P2P_TCP