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
	#include<unistd.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netinet/ip.h>
	#include <netdb.h>
	#include <arpa/inet.h>
	#include <fcntl.h>
	#include <string.h>
#endif


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
		LOG(logDEBUG) << "Kernel blocking mode of socket already set to " << block;
		return true;
	}

	if (-1 == fcntl(soc, F_SETFL, block ? flags ^ O_NONBLOCK : flags | O_NONBLOCK)) {
		LOG(logERROR) << "Cannot set socket kernel blocking to " << block << "!";
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
	err = err ? err : errno;
	return std::string(strerror(err));
#endif

	switch (err ? err : errno) {
	case EADDRINUSE: return("EADDRINUSE\n"); break;
	case EADDRNOTAVAIL: return("EADDRNOTAVAIL\n"); break;
	case EBADF: return("EBADF\n"); break;
	case ECONNABORTED: return("ECONNABORTED\n"); break;
	case EINVAL: return("EINVAL\n"); break;
		//case EIO: return("EIO\n"); break;
	case ENOBUFS: return("ENOBUFS\n"); break;
	case ENOPROTOOPT: return("ENOPROTOOPT\n"); break;
	case ENOTCONN: return("ENOTCONN\n"); break;
	case ENOTSOCK: return("ENOTSOCK\n"); break;
	case EPERM: return("EPERM\n"); break;

#ifdef _WIN32
	case WSAEADDRNOTAVAIL: return "WSAEADDRNOTAVAIL"; break;
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
		LPVOID lpDisplayBuf;
		DWORD dw = GetLastError();

		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dw,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&lpMsgBuf,
			0, NULL);

		// Display the error message and exit the process

		lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
			(lstrlen((LPCTSTR)lpMsgBuf) + 40) * sizeof(TCHAR));
		StringCchPrintf((LPTSTR)lpDisplayBuf,
			LocalSize(lpDisplayBuf) / sizeof(TCHAR),
			TEXT("%s failed with error %d: %s"),
			"[]", dw, lpMsgBuf);

		std::wstring  msg((wchar_t*)lpDisplayBuf);
		LocalFree(lpMsgBuf);
		LocalFree(lpDisplayBuf);

		//setup converter
		typedef std::codecvt_utf8<wchar_t> convert_type;
		std::wstring_convert<convert_type, wchar_t> converter;
		return converter.to_bytes(msg);
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