#include <sstream>
#include <functional>
#include <cctype>

#include "JsonHttpClient.h"

#undef LOG
#include "mongoose/mongoose.h"

#include "SocketAddress.h"


// need to override LOG() macro:
#undef LOG
#define LOG(level) if (level > pclog::Log::ReportingLevel()) ; else pclog::Log(level).get()
#define logDebugHttp logDEBUG
#include "pclog/pclog.h"

JsonHttpClient::JsonHttpClient(int timeoutMs)
{
	m_timeoutConnectMs = timeoutMs;
	m_timeoutResponseMs = timeoutMs;
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
		throw std::runtime_error("RPC response id does not match request!");
	}

	if (rpcResp["result"].isUndefined()) {
		throw std::runtime_error("RPC response without result!");
	}


	if (!rpcResp["result"]["error"].str.empty())
		throw std::runtime_error(rpcResp["result"]["error"].str);

	return rpcResp["result"];
}

#ifdef _WIN32
LARGE_INTEGER timerFreq;
VOID(WINAPI*myGetSystemTime)(LPFILETIME);
#endif

inline static uint64_t getMicroSeconds()
{
#ifndef _WIN32
	uint64_t t;
	struct timeval tv;
	gettimeofday(&tv, 0);
	t = (uint64_t)tv.tv_sec * (uint64_t)1e6 + (uint64_t)tv.tv_usec;
	return t;
#else
	LARGE_INTEGER t1;
	QueryPerformanceCounter(&t1);
	return (uint64_t)(((double)t1.QuadPart) / ((double)timerFreq.QuadPart) * 1000000.0);
#endif
}

// trim from start
static inline std::string &ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
	return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
	return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
	return ltrim(rtrim(s));
}


const JsonNode &JsonHttpClient::request(const SocketAddress &host, const std::string &method, const std::string &body)
{
	//LOG(logDEBUG1) << "http request to " << host << " /" << method << " " << body;
	auto tStart =  getMicroSeconds();

	SOCKET soc = socket(host.getFamily(), SOCK_STREAM, IPPROTO_TCP);
	try {
		int res;
		if (m_timeoutConnectMs > 0) {
			if (!socketSetBlocking(soc, false))
				throw std::runtime_error("Cannot set socket non-blocking!");

			res = connect(soc, (const sockaddr*)(&host), sizeof(host));
			if (res != 0) {
				res = socketConnectTimeout(soc, m_timeoutConnectMs * 1000);
				if (res != 1) {
					//LOG(logDEBUG) << "connect() to " << host << " timed out";
					throw std::runtime_error("Connect timeout (" + std::to_string(m_timeoutConnectMs) + " ms) to " + host.toString());
				}
			}
		}
		else {
			res = connect(soc, (const sockaddr*)(&host), sizeof(host));
		}

        if (res == -1) {
             //LOG(logDebugHttp) << "connect() to " << host << " failed!";
            throw std::runtime_error("Connect failed!");
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
		if (m_timeoutResponseMs > 0)
		{
			toms = m_timeoutResponseMs + rand() % (m_timeoutResponseMs);
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
				throw std::runtime_error("Cannot set recvtimeo to " + std::to_string(toms) + "us! " + lastError()) ;
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

		auto tEnd = getMicroSeconds();

		str = buf.str();
		buf.str(""); buf.clear();
		
		if (str.empty())
		{
			throw std::runtime_error("Empty response!");
		}
		
		LOG(logDEBUG3) << "response after " << ((tEnd - tStart) / 1000) << " ms, timeout was " << toms << " ms";

		auto skipHttpVer = str.find(' '), skipCode = str.find(' ', skipHttpVer + 1), skipStatus = str.find('\n', skipCode + 1);
		if (skipHttpVer == std::string::npos || skipCode == std::string::npos || skipStatus == std::string::npos) {
			throw std::runtime_error("Invalid HTTP response!");
		}
		auto code(std::stoi(str.substr(skipHttpVer + 1, skipCode - skipHttpVer)));
		
		auto status(str.substr(skipCode + 1, skipStatus - skipCode));
		trim(status);

		if (code != 200) {
			throw std::runtime_error("HTTP error: " + status);
		}

		bool winLb = str[skipStatus - 1] == '\r';
		auto skipHeader = str.find(winLb ? "\r\n\r\n" : "\n\n", skipStatus);
		auto body = str.substr(skipHeader + (winLb ? 4 : 2));

		if (!m_lastData.tryParse(body)) {
			LOG(logERROR) << "Failed to parse json " << body;
			throw std::runtime_error("JSON parse error!");
		}

		LOG(logDebugHttp) << "http response " << code << " (" << status << ") body: " << m_lastData;

		return m_lastData;
	}

	catch (const std::exception &ex) {
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