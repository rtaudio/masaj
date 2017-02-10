#pragma once

#include "net.h"
#include "JsonNode.h"

#include<map>


//#include "concurrentqueue/blockingconcurrentqueue.h"
#include "ThreadPool/ThreadPool.h"

typedef std::map<std::string, std::string> StringMap;


union SocketAddress;

struct mg_mgr;
struct mg_connection;
struct mg_rpc_request;

#include <functional>

class JsonHttpServer
{
public:
	//template <typename ReturnType>
	//using RequestHandler = std::function<ReturnType(const NodeAddr &addr, const JsonObject &request)>;


	class StreamResponse {
		friend class JsonHttpServer;

	private:
		JsonHttpServer *server;
		struct mg_connection *nc;
		bool headersSent;
		StringMap headers;

		int minSendLen;
		std::vector<uint8_t> sendBuffer;
		int curSendBufferBytes;


		StreamResponse(JsonHttpServer *server, struct mg_connection *nc)
			: server(server), nc(nc), headersSent(false) {
			minSendLen = 1200;
			curSendBufferBytes = 0;
			sendBuffer.resize(minSendLen * 2);
		}

		bool sendHeaders();

	public:
		void addHeader(const std::string &name, const std::string &value);
		bool write(const void *buf, int len);	

		void setSendBufferSize(int size) {
			minSendLen = size;
		}

		inline bool write(const std::string &str) {
			return write(str.c_str(), str.size());
		}

		bool isConnected();
	};

	typedef std::function<void(const void *buf, int len)> Writer;

	typedef std::function<void(const SocketAddress &addr, JsonNode &request, JsonNode &response)> RequestHandler;
	typedef std::function<void(const SocketAddress &addr, JsonNode &request, StreamResponse &write)> StreamRequestHandler;

	


	JsonHttpServer(int numWorkerThreads);
	~JsonHttpServer();

	void on(std::string rpcMethod, const RequestHandler &handler);
	void onStream(std::string streamPrefix, const StreamRequestHandler &handler);
    
	bool start(const std::string &bindAddr);
	void update();

	bool hasActiveConnection();

private:


	static void ev_handler(struct mg_connection *nc, int ev, void *ev_data);
	void rpc_dispatch(struct mg_connection *nc, struct http_message *hm);

	struct mg_mgr *m_mgr;
	struct mg_connection *nc;
	int m_mgEv;

	clock_t lastRequestTime;

	struct ConnectionHandler {
		std::future<void> future;

		ConnectionHandler(const ConnectionHandler&) = delete;
		ConnectionHandler& operator=(const ConnectionHandler&) = delete;
		ConnectionHandler(ConnectionHandler&&) noexcept {}
		ConnectionHandler& operator=(ConnectionHandler&&) noexcept {}

		std::atomic<bool> connectionClosed;

		std::string lastRequest;

		void notifyDone() {
			done.notify_all();
		}

		void blockUntilDone() {
			std::unique_lock<std::mutex> lock(mtx);
			done.wait(lock);
		}

		void polled() {
			//waitForPoll.Signal();
		}

		ConnectionHandler() : connectionClosed(false) {			
		}
	private:
		std::condition_variable done;
		std::mutex mtx;
	};

	//moodycamel::ConcurrentQueue<HandlerParamsAndPromise> dispatchQueue;
	ThreadPool threadPool;

	//std::map<mg_connection*, HandlerParamsAndPromise> activeHandlers;

	std::vector<ConnectionHandler*> activeConnectionHandlers;
	

	std::map<std::string, RequestHandler> m_handlers;
	std::map<std::string, StreamRequestHandler> m_streamHandlers;
};

