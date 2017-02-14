#pragma once

#include "net.h"
#include "JsonNode.h"

#include<map>


//#include "concurrentqueue/blockingconcurrentqueue.h"
#include "ThreadPool/ThreadPool.h"

#include "../concurrentqueue/concurrentqueue.h"

typedef std::map<std::string, std::string> StringMap;


union SocketAddress;

struct mg_mgr;
struct mg_connection;
struct mg_rpc_request;

#include <functional>

class JsonHttpServer
{
private:
    struct ConnectionHandler;

public:
	//template <typename ReturnType>
	//using RequestHandler = std::function<ReturnType(const NodeAddr &addr, const JsonObject &request)>;



    class StreamRequest {
    public:
        const SocketAddress &addr;
        const JsonNode &data;
        const StringMap &headers;

        std::string getHeader(std::string name) const;

        StreamRequest(   const SocketAddress &addr, const JsonNode &queryVars,const  StringMap &headers)
            : addr(addr), data(queryVars), headers(headers) {}

        StreamRequest( const  ConnectionHandler *ch);
    };

    typedef StreamRequest WsRequest;

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
        inline void setContentType(const std::string &mime) {
            addHeader("Content-Type", mime);
        }

		bool write(const void *buf, int len);	

		void setSendBufferSize(int size) {
			minSendLen = size;
		}

		inline bool write(const std::string &str) {
			return write(str.c_str(), str.size());
		}

		bool isConnected();
	};

    class WsConnection {
public:
        WsConnection(ConnectionHandler *ch) : ch(ch) { }

        // TODO bug: segfault
        inline bool write(std::vector<uint8_t> &&data) {
            if(ch->connectionClosed) return false;
            return ch->sendBuf->enqueue(data); }

        inline bool write(const std::vector<uint8_t> &data) {
            if(ch->connectionClosed) return false;
           return ch->sendBuf->enqueue(data); }

        int tryRead(uint8_t *buf, int maxLen) {
            return ch->recvBuf->try_dequeue_bulk(buf, maxLen);
        }

        inline bool isConnected() const { return !ch->connectionClosed; }

    private:
        ConnectionHandler *ch;
    };

	typedef std::function<void(const void *buf, int len)> Writer;

	typedef std::function<void(const SocketAddress &addr, JsonNode &request, JsonNode &response)> RequestHandler;
    typedef std::function<void(const StreamRequest &request, StreamResponse &response)> StreamRequestHandler;
    typedef std::function<void(const WsRequest &request, WsConnection &conn)> WebSocketHandler;

	


	JsonHttpServer(int numWorkerThreads);
	~JsonHttpServer();

	void on(std::string rpcMethod, const RequestHandler &handler);
    void onStream(std::string streamPrefix, const StreamRequestHandler &handler);
    void onWS(std::string endpointUri, const WebSocketHandler &handler);
    
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
        struct mg_connection *nc;

		ConnectionHandler(const ConnectionHandler&) = delete;
		ConnectionHandler& operator=(const ConnectionHandler&) = delete;
		ConnectionHandler(ConnectionHandler&&) noexcept {}
		ConnectionHandler& operator=(ConnectionHandler&&) noexcept {}

		std::atomic<bool> connectionClosed;

        std::string lastRequest; // TODO remove
        std::string uri;
        StringMap requestHeaders;
        JsonNode getVars;

        moodycamel::ConcurrentQueue<std::vector<uint8_t>> *sendBuf;
        moodycamel::ConcurrentQueue<uint8_t> *recvBuf;


		void notifyDone() {
			done.notify_all();
		}

		void blockUntilDone() {
			std::unique_lock<std::mutex> lock(mtx);
			done.wait(lock);
		}

        void poll();

        void update(struct mg_connection *nc, struct http_message *hm);

        ConnectionHandler() :
            connectionClosed(false), sendBuf(nullptr), recvBuf(nullptr), nc(nullptr) {
        }

        ~ConnectionHandler() {
            if(sendBuf)
                delete sendBuf;
            if(recvBuf)
                delete recvBuf;
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
    std::map<std::string, WebSocketHandler> m_wsHandlers;
};

