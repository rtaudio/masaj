#pragma once
#include <string>
#include "net.h"
#include "JsonHttpServer.h"


class JsonHttpClient
{
public:
	JsonHttpClient(int timeoutMs = 4000);
	virtual ~JsonHttpClient();    
	const JsonNode &rpc(const SocketAddress &host, const std::string &method, const JsonNode &params);

private:
	const JsonNode &request(const SocketAddress &host, const std::string &method, const std::string &body);
	JsonNode m_lastData;
	int m_timeoutConnectMs;
	int m_timeoutResponseMs;
	uint64_t m_rpcId;
};
