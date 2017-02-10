#pragma once

#include <algorithm>
#include "pclog/pclog.h"

union SocketAddress {
    struct sockaddr sa;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
    // union! no more fields!

	SocketAddress(const sockaddr_storage &ss)
	{
		memcpy(&sa, &ss, sizeof(sin6));
	}

	SocketAddress(const sockaddr *ss, int len=sizeof(sockaddr))
	{
		memset(&sa, 0, sizeof(sin6));
		memcpy(&sa, ss, std::min((int)sizeof(sin6), len));
	}

	SocketAddress()
	{
		memset(&sa, 0, sizeof(sin6));
	}

    inline void setPort(int port) {
        if(sa.sa_family == AF_INET6)
            sin6.sin6_port = htons(port);
        else
            sin.sin_port = htons(port);
    }

	inline int getFamily() const { return sa.sa_family; }
	inline int getPort() const {
		return ntohs((sa.sa_family == AF_INET6) ? sin6.sin6_port : sin.sin_port);
	}

    inline std::string toString(bool hostOnly=false) const {
        char host[64], serv[32];

        int r = getnameinfo((const sockaddr*)&sin6, sizeof(sin6), host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
        if(r != 0) {
		// TODO bug: this prints weird chars
			LOG(logERROR) << "getnameinfo() failed! " << gai_strerror(r);
			LOG(logERROR) << lastError();
            return "";
        }

		if (hostOnly)
			return std::string(host);

        if(sin6.sin6_family == AF_INET6) {
            return "["+std::string(host)+"]:" + std::string(serv);
        } else {
            return std::string(host) + ":" + std::string(serv);
        }
    }


	//inline friend std::ostream & operator<<(std::ostream &os, const SocketAddress& s) { return os << s.toString(); }
};

inline std::ostream & operator<<(std::ostream &os, const SocketAddress& s) { return os << s.toString(); }

inline bool operator==(const SocketAddress& s1, const SocketAddress& s2) {
	return s1.getFamily() == s2.getFamily()
		&& memcmp(&s1, &s2, (s1.getFamily() == AF_INET6) ? sizeof(s1.sin6) : sizeof(s1.sin)) == 0;
}

inline bool operator!=(const SocketAddress& s1, const SocketAddress& s2) {
	return !(s1 == s2);
}
