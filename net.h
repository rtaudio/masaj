#pragma once




#ifdef _WIN32
	#define NOMINMAX
	#include<winsock2.h>
	//#include<ws2def.h>
	#include <ws2tcpip.h>
	#include <Windows.h>
	#include <Iphlpapi.h>
	#include <Assert.h>
	#include <strsafe.h>
	#include<codecvt>
	#pragma comment(lib, "iphlpapi.lib")


	//#define in_port_t u_short
	//#define inet_aton(s,b) InetPton(AF_INET,L##s,b)
	inline int close(SOCKET s) { return closesocket(s); }
#else
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <locale.h>

	#include<unistd.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netinet/ip.h>
	#include <netdb.h>
	#include <arpa/inet.h>
	#include <fcntl.h>
        #ifndef SOCKET
            #define SOCKET int
        #endif
#endif

#include "pclog/pclog.h"


inline int socketConnectTimeout(SOCKET &soc, uint64_t toUs)
{
	fd_set myset;
	struct timeval tv;
	int res;

	tv.tv_sec = (long)(toUs / 1000000UL);
	tv.tv_usec = (long)(toUs - tv.tv_sec);


	FD_ZERO(&myset);
	FD_SET(soc, &myset);

	res = select(soc + 1/* this is ignored on windows*/, NULL, &myset, NULL, &tv);

	if (res == -1) {
		//LOG(logERROR) << "error while select connect: " << lastError();
		return -1;
	}

	if (res == 0) {
		//LOG(logDEBUG1) << "timeout during connect select " << toUs;
		return 0;
	}

	socklen_t olen = sizeof(int);
	int o = -1;
	if (getsockopt(soc, SOL_SOCKET, SO_ERROR, (char*)(&o), &olen) < 0) {
		//LOG(logERROR) << "Error in getsockopt() " << lastError();
		return -1;
	}

	// some error occured
	if (o != 0) {
		return -1;
	}

	return res;
}

inline bool socketSetBlocking(SOCKET &soc, bool block) {
#ifndef _WIN32
	const int flags = fcntl(soc, F_GETFL, 0);
	int curNonBlock = (flags & O_NONBLOCK);
	if (curNonBlock == !block) {
#ifdef LOG
		LOG(logDEBUG) << "Kernel blocking mode of socket already set to " << block;
#endif
		return true;
	}

	if (-1 == fcntl(soc, F_SETFL, block ? flags ^ O_NONBLOCK : flags | O_NONBLOCK)) {
#ifdef LOG
		LOG(logERROR) << "Cannot set socket kernel blocking to " << block << "!";
#endif
		return false;
	}
#else
	u_long iMode = block ? 0 : 1;
	if (ioctlsocket(soc, FIONBIO, &iMode) != NO_ERROR)
	{
#ifdef LOG
		LOG(logERROR) << "Cannot set socket kernel blocking to " << block << "!";
#endif
		return false;
	}
#endif
	return true;
}


inline std::string lastError(int err = 0)
{
#if _WIN32
	err = err ? err : WSAGetLastError();
#else
        char buf[300];
	err = err ? err : errno;
        if(strerror_r(err, buf, sizeof(buf)) == 0)
            return std::string(buf);
        return "[strerror_s failed!]";
#endif

	switch (err ? err : errno) {
	case EADDRINUSE: return("EADDRINUSE\n");
	case EADDRNOTAVAIL: return("EADDRNOTAVAIL\n");
	case EBADF: return("EBADF\n");
	case ECONNABORTED: return("ECONNABORTED\n");
	case EINVAL: return("EINVAL\n");
		//case EIO: return("EIO\n"); break;
	case ENOBUFS: return("ENOBUFS\n");
	case ENOPROTOOPT: return("ENOPROTOOPT\n");
	case ENOTCONN: return("ENOTCONN\n");
	case ENOTSOCK: return("ENOTSOCK\n");
	case EPERM: return("EPERM\n");

#ifdef _WIN32
	case WSAEADDRNOTAVAIL: return "WSAEADDRNOTAVAIL"; break;
	case WSAEWOULDBLOCK:
		return "WSAEWOULDBLOCK";
#endif

#ifdef ETOOMANYREFS
	case ETOOMANYREFS: return("ETOOMANYREFS\n"); break;
#endif
#ifdef EUNATCH
	case EUNATCH: return("EUNATCH\n"); break;
#endif
	default:
#ifdef _WIN32
		LPVOID lpMsgBuf;
		DWORD dw = GetLastError();

		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |	FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL
		);


		std::string msg((LPTSTR)lpMsgBuf);
		LocalFree(lpMsgBuf);

		msg += " (" + std::to_string(dw) + ")";

		return msg;
#endif


		return("UNKNOWN\n"); break;
	}
}

inline bool socketAsyncContinue()
{
	return !(errno != EINTR && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK
#ifdef _WIN32
		&& WSAGetLastError() != WSAEINTR &&
		WSAGetLastError() != WSAEWOULDBLOCK
#endif
		);
}