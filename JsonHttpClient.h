#pragma once

#include "UlltraProto.h"
#include "networking.h"


#include <string>

#include "JsonHttpServer.h"


class JsonHttpClient
{
public:
	struct Exception {
		int statusCode;
		std::string statusString;
		Exception(const std::string &msg, int code=-1) {
			statusString = msg;
		}
	};
	JsonHttpClient();
	virtual ~JsonHttpClient();    
	const JsonNode &rpc(const SocketAddress &host, const std::string &method, const JsonNode &params);

private:
	const JsonNode &request(const SocketAddress &host, const std::string &method, const std::string &body);
	JsonNode m_lastData;
	int m_connectTimeout;
	uint64_t m_rpcId;
};

