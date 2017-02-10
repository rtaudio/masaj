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