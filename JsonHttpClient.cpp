#include <sstream>

#include "JsonHttpClient.h"

#undef LOG
#include "mongoose/mongoose.h"

// need to override LOG() macro:
#undef LOG
#define LOG(level) if (level > pclog::Log::ReportingLevel()) ; else pclog::Log(level).get()
#define logDebugHttp logDEBUG


JsonHttpClient::JsonHttpClient()
{
	m_connectTimeout = UP::HttpConnectTimeout;
	m_rpcId = 1;
}


JsonHttpClient::~JsonHttpClient()
{
}



const JsonNode &JsonHttpClient::rpc(const SocketAddress &host, const std::string &method, const JsonNode &params)
{
	JsonNode rpc;
	rpc["id"] = ++m_rpcId;
	rpc["method"] = method;
	rpc["params"] = params;
	std::stringstream ss;
	ss << rpc;
	const JsonNode &rpcResp(request(host, method, ss.str()));

	if (rpcResp["id"].num != m_rpcId) {
		LOG(logDEBUG) << "response with invalid id: " << rpcResp;
		throw Exception("RPC response id does not match request!");
	}

	if (rpcResp["result"].isUndefined()) {
		throw Exception("RPC response without result!");
	}


	if (!rpcResp["result"]["error"].str.empty())
		throw JsonHttpClient::Exception(rpcResp["result"]["error"].str);

	return rpcResp["result"];
}



const JsonNode &JsonHttpClient::request(const SocketAddress &host, const std::string &method, const std::string &body)
{
	LOG(logDEBUG1) << "http request to " << host << " /" << method << " " << body;
	auto tStart =  UP::getMicroSeconds();

	SOCKET soc = socket(host.getFamily(), SOCK_STREAM, IPPROTO_TCP);
	try {
		int res;
		if (m_connectTimeout > 0) {
			if (!socketSetBlocking(soc, false))
				throw Exception("Cannot set socket non-blocking!");

			res = connect(soc, (const sockaddr*)(&host), sizeof(host));
			if (res != 0) {
				res = socketConnectTimeout(soc, UP::HttpConnectTimeout * 1000);
				if (res != 1) {
					LOG(logDEBUG) << "connect() to " << host << " timed out";
					throw Exception("Connect timeout (" + std::to_string(UP::HttpConnectTimeout) + " ms)!");
				}
			}
		}
		else {
			res = connect(soc, (const sockaddr*)(&host), sizeof(host));
		}

        if (res == -1) {
             LOG(logDebugHttp) << "connect() to " << host << " failed!";
            throw Exception("Connect failed!");
        }

		std::string path("/x-" + method);

		std::ostringstream buf;
		buf << "POST " << path << " HTTP/1.1\n";
		buf << "Content-Type: application/json\n";
		buf << "Content-Length: " << std::to_string(body.length()) << "\n\n";
		buf << body;
		std::string str = buf.str();
        send(soc, str.data(), str.size(), 0);
		buf.str(""); buf.clear();

		char chunk[1024*16+1];
		int rlen;

		
		int toms = 0;
		// set socket back to blocking if necessary
		if (UP::HttpResponseTimeoutMs > 0)
		{
			toms = UP::HttpResponseTimeoutMs + rand() % (UP::HttpResponseTimeoutMs);
			socketSetBlocking(soc, true);

			// set timeout
#ifndef _WIN32
			// 0 means no timeout
			struct timeval tv;
			tv.tv_sec = toms/1000UL;
			tv.tv_usec = (toms*1000UL) % 1000000UL;
#else
			int tv = toms;
#endif
			if (setsockopt(soc, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
				LOG(logERROR) << "set SO_RCVTIMEO fro " << soc << " failed: " << lastError();
				throw Exception("Cannot set recvtimeo to " + std::to_string(toms) + "us! " + lastError()) ;
			}
		}
		

		while (true) {
			rlen = recv(soc, chunk, sizeof(chunk) - 1, 0);
			if (rlen == 0)
				break;
			
			if (rlen <= 0 && !socketAsyncContinue()) {
				LOG(logERROR) << "error during receive!";
				break;
			}
			
			chunk[rlen] = '\0';
			buf << chunk;
		}
		close(soc);

		auto tEnd = UP::getMicroSeconds();

		str = buf.str();
		buf.str(""); buf.clear();
		
		if (str.empty())
		{
			throw Exception("Empty response!");
		}
		
		LOG(logDEBUG3) << "response after " << ((tEnd - tStart) / 1000) << " ms, timeout was " << toms << " ms";

		auto skipHttpVer = str.find(' '), skipCode = str.find(' ', skipHttpVer + 1), skipStatus = str.find('\n', skipCode + 1);
		if (skipHttpVer == std::string::npos || skipCode == std::string::npos || skipStatus == std::string::npos) {
			throw Exception("Invalid HTTP response!");
		}
		auto code(std::stoi(str.substr(skipHttpVer + 1, skipCode - skipHttpVer)));
		
		auto status(str.substr(skipCode + 1, skipStatus - skipCode));
		trim(status);

		if (code != 200) {
			throw Exception("HTTP error: " + status);
		}

		bool winLb = str[skipStatus - 1] == '\r';
		auto skipHeader = str.find(winLb ? "\r\n\r\n" : "\n\n", skipStatus);
		auto body = str.substr(skipHeader + (winLb ? 4 : 2));

		if (!m_lastData.tryParse(body)) {
			LOG(logERROR) << "Failed to parse json " << body;
			throw Exception("JSON parse error!");
		}

		LOG(logDebugHttp) << "http response " << code << " (" << status << ") body: " << m_lastData;

		return m_lastData;
	}

	catch (const Exception &ex) {
		close(soc);
		throw ex;
	}
}


#if 0
const JsonHttpClient::Response &JsonHttpClient::request(const NodeAddr &host, std::string path, const StringMap &data)
{
	char res[1024];
	const int m = sizeof(res) - 1;
	int n = 0;

	n += json_emit(&res[n], m, "{");
	for (auto it = data.begin(); it != data.end(); it++) {
		n += json_emit(&res[n], m, "s:s,", it->first.c_str(), it->second.c_str());
	}
	n += json_emit(&res[n - 1], m, "}"); // n-1 to remove last ','

	return request(host, path, std::string(res));
}
#endif