#include <string>
#include <regex>
#include <sstream>
#include <algorithm>

#include "net.h"
#include "SocketAddress.h"

#ifndef _WIN32
#include <netinet/tcp.h>
#endif

#include "JsonHttpServer.h"
#include "pclog/pclog.h"

// VS
#pragma warning( disable : 4996 )


#undef LOG
#include "mongoose/mongoose.h"
#undef LOG // need to override LOG() macro:
#define LOG(level) if (level > pclog::Log::ReportingLevel()) ; else pclog::Log(level).get()
#define logDebugHttp logDEBUG

#include "frozen/frozen.h"



JsonHttpServer::JsonHttpServer(int numWorkerThreads)
	:threadPool(numWorkerThreads)
{
#ifndef _WIN32
    // StreamResponse::write / send() can signal SIGPIPE, just ignore it
    signal(SIGPIPE, SIG_IGN);
#endif

	m_mgEv = 0;
	m_mgr = new mg_mgr();
	mg_mgr_init(m_mgr, this);
	lastRequestTime = std::clock();
}


JsonHttpServer::~JsonHttpServer()
{
	mg_mgr_free(m_mgr);
	delete m_mgr;
}

void JsonHttpServer::update()
{
	clock_t end = clock();
	// poll untill all events processed
	do {
		m_mgEv = 0;
		mg_mgr_poll(m_mgr, 1);
	} while (m_mgEv != 0);
	
	// go into "sleep" mode after 2 seconds
	auto now = clock();
	double secondsSinceLastRequest = double(now - lastRequestTime) / CLOCKS_PER_SEC;
	if (secondsSinceLastRequest > 2) {
		mg_mgr_poll(m_mgr, 400);
	}

    // here we pump WebSocket data from handling threads to the client
    for (ConnectionHandler *ch : activeConnectionHandlers) {
       ch->poll();
    }
}

bool JsonHttpServer::hasActiveConnection() {
	return (m_mgr->active_connections && m_mgr->active_connections->next);
}

void JsonHttpServer::on(std::string rpcMethod, const RequestHandler &handler)
{
	m_handlers.insert(std::pair<std::string, RequestHandler>(rpcMethod, handler));
}

void JsonHttpServer::onStream(std::string streamPrefix, const StreamRequestHandler &handler)
{
	m_streamHandlers.insert(std::pair<std::string, StreamRequestHandler>(streamPrefix, handler));
}

void JsonHttpServer::onWS(std::string endpointUri, const WebSocketHandler &handler)
{
    m_wsHandlers.insert(std::pair<std::string, WebSocketHandler>("/" + endpointUri, handler));
}



bool JsonHttpServer::start(const std::string &bindAddr)
{

	nc = mg_bind(m_mgr, bindAddr.c_str(), ev_handler);
	if (!nc) {
        LOG(logERROR) << "Failed to bind mongoose web server to " << bindAddr.c_str();
		return false;
	}
	mg_set_protocol_http_websocket(nc);

    LOG(logDebugHttp) << "started mongoose on " << bindAddr.c_str();

	return true;
}


static int is_equal(const struct mg_str *s1, const struct mg_str *s2) {
	return s1->len == s2->len && memcmp(s1->p, s2->p, s2->len) == 0;
}

static int starts_with(const struct mg_str *s1, const struct mg_str *s2) {

	return s1->len >= s2->len && memcmp(s1->p, s2->p, s2->len) == 0;
}


JsonNode & rpcErrorReponse(JsonNode &response, const std::string &message, int code=1) {	
	response["error"]["code"] = code;
	response["error"]["message"] = message;
	response["id"] = message;
	return response;
}


void rpcReponse(struct mg_connection *nc, const JsonNode &response) {	

	std::stringstream ss;
	ss << response;
	std::string respBody(ss.str());

	auto cs = respBody.c_str();

	// generate response	
	mg_printf(nc, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Type: application/json\r\n\r\n%s", (int)strlen(cs), cs);
	// HEAP corruption here! TODO!!
	//nc->flags |= MG_F_SEND_AND_CLOSE & 0;
}

void  JsonHttpServer::rpc_dispatch(struct mg_connection *nc, struct http_message *hm) {
	auto nodeAddr = (SocketAddress*)&nc->sa;
	

	JsonNode response;
	response["id"] = 0;
	response["jsonrpc"] = "2.0";

	//rpcReponse(nc, rpcErrorReponse(response, "Server Error"));
	//return;

	if (nodeAddr->getFamily() == 0) {
		LOG(logERROR) << "Invalid/no client address!";
		rpcReponse(nc, rpcErrorReponse(response, "Server Error"));
		return;
	}
	
	JsonNode requestNode;
	if (!requestNode.tryParse(hm->body.p, hm->body.len)) {
		LOG(logERROR) << "Invalic JSON in RPC request :" << std::string(hm->body.p, hm->body.len);
		rpcReponse(nc, rpcErrorReponse(response, "Invalid JSON"));
		return;
	}

	auto id = requestNode["id"], params = requestNode["params"];
	auto method = requestNode["method"].str;

	response["id"] = id;

	if (id.isEmpty() || std::isnan(id.num) || id.num < 0 || method.empty()) {
		LOG(logERROR) << "Invalic RPC request!";
		rpcReponse(nc, rpcErrorReponse(response, "Invalid RPC request"));
		return;
	}


	// find & execute handlers
	auto hit(m_handlers.find(method));
	if (hit == m_handlers.end() || !hit->second) {
		LOG(logERROR) << "No handler for " << method;
		rpcReponse(nc, rpcErrorReponse(response, "No handler for " + method));
		return;
	}

	//LOG(logDEBUG1) << "rpc_dispatch: " << method;
	try {
		auto f = hit->second;
		f(*nodeAddr, requestNode["params"], response["result"]);
	}
	catch (const std::exception &ex) {
		LOG(logERROR) << "error in RPC handler " << method << ": " << ex.what();
		rpcReponse(nc, rpcErrorReponse(response, ex.what()));
		return;
	}

	//LOG(logDEBUG2) << "sending response: " << response;


	rpcReponse(nc, response);
	
}

std::string urlDecode(std::string &SRC) {
	std::string ret;
	char ch;
	int ii;
	for (size_t i = 0; i<SRC.length(); i++) {
		if (int(SRC[i]) == 37) {
			sscanf(SRC.substr(i + 1, 2).c_str(), "%x", &ii);
			ch = static_cast<char>(ii);
			ret += ch;
			i = i + 2;
		}
		else {
			ret += SRC[i];
		}
	}
	return (ret);
}


std::map<std::string, std::string> parseQuery(const std::string& query)
{
	std::map<std::string, std::string> data;
	std::regex pattern("([\\w+%]+)=([^&]*)");
	auto words_begin = std::sregex_iterator(query.begin(), query.end(), pattern);
	auto words_end = std::sregex_iterator();

	for (std::sregex_iterator i = words_begin; i != words_end; i++)
	{
		std::string key = (*i)[1].str();
		std::string value = (*i)[2].str();
		data[urlDecode(key)] = urlDecode(value);
	}

	return data;
}


void JsonHttpServer::StreamResponse::addHeader(const std::string &name, const std::string &value) {
	if (headersSent)
		throw std::runtime_error("Cannot add header, headers already sent!");
	headers[name] = value;
}


bool JsonHttpServer::StreamResponse::write(const void *buf, int len) {
	ConnectionHandler *ch = (ConnectionHandler *)(nc->user_data);

	if (len < 0)
		return false;

	

	if (nc->flags != 0) {
		return false;
	}

	if (ch && ch->connClosed) {
		return false;
	}


	if (!headersSent) {
		if (!sendHeaders())
			return false;
	}

	int res;

	// if len is too small always add to buffer
	if (len < minSendLen) {
		memcpy(&sendBuffer[curSendBufferBytes], buf, len);
		curSendBufferBytes += len;
		if (curSendBufferBytes >= minSendLen) {
			res = send(nc->sock, (const char *)sendBuffer.data(), curSendBufferBytes, 0);
			if (res != curSendBufferBytes) {
				return false;
			}
			curSendBufferBytes = 0;
		}
	}
	else
	{
		// instant flush
		if (curSendBufferBytes > 0) {
			res = send(nc->sock, (const char *)sendBuffer.data(), curSendBufferBytes, 0);
			if (res != curSendBufferBytes) {
				return false;
			}
			curSendBufferBytes = 0;
		}
		res = send(nc->sock, (const char *)buf, len, 0);
		if (res != len) {
			return false;
		}
	}




	//send(nc->sock, (const char *)buf, len, 0);
	//mg_send(nc, buf, len);

	return true;
}

bool  JsonHttpServer::StreamResponse::sendHeaders() {

#if _WIN32
	DWORD  flagYes = 1;
#else
	int flagYes = 1;
#endif

	int result = setsockopt(nc->sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flagYes, sizeof(flagYes));
	if (result < 0) {
		LOG(logERROR) << "failed to enable TCP_NODELAY! " << lastError();
	}

#if _WIN32
	int send_buffer = 0;
	int send_buffer_sizeof = sizeof(int);
	result = setsockopt(nc->sock, SOL_SOCKET, SO_SNDBUF, (char*)&send_buffer, send_buffer_sizeof);
	if (result < 0) {
		LOG(logERROR) << "failed to set send buffer size! " << lastError();
	}
#endif

	addHeader("Accept-Ranges", "none");
	addHeader("Connection", "close");
	addHeader("Access-Control-Allow-Origin", "*");
	addHeader("Cache-Control", "no-cache, no-store");
	addHeader("Pragma", "no-cache");

	std::string headerLines("HTTP/1.1 200 OK\r\n");

    // todo BUG: SEVFAULT HERE IN ITERATOR
    for (auto h : headers) {
		headerLines += h.first + ": " + h.second + "\r\n";
	}

	headerLines += "\r\n";
	headersSent = true;
	return write(headerLines);	
}




StringMap parseHeaders(const struct http_message *hm) {
    StringMap headers;
    for (int i = 0; i < MG_MAX_HTTP_HEADERS && hm->header_names[i].len > 0; i++) {
      auto hn = std::string(hm->header_names[i].p, hm->header_names[i].len);
      std::transform(hn.begin(), hn.end(), hn.begin(), ::tolower);
      headers[hn] = std::string(hm->header_values[i].p, hm->header_values[i].len);
    }
    return headers;
}


static int is_websocket(const struct mg_connection *nc) {
  return nc->flags & MG_F_IS_WEBSOCKET;
}

void JsonHttpServer::ev_handler(struct mg_connection *nc, int ev, void *ev_data)
{
    static const struct mg_str s_get_method = MG_MK_STR("GET");
    static const struct mg_str s_head_method = MG_MK_STR("HEAD");
	static const struct mg_str s_post_method = MG_MK_STR("POST");
	static const struct mg_str s_stream_query_prefix = MG_MK_STR("/streamd/");
	static struct mg_serve_http_opts s_http_server_opts;

	JsonHttpServer *jhs = static_cast<JsonHttpServer*>(nc->mgr->user_data);
	struct http_message *hm = (struct http_message *) ev_data;
	ConnectionHandler *ch = (ConnectionHandler *)nc->user_data;
	auto nodeAddr = (SocketAddress*)&nc->sa;

	if (!jhs) {
		LOG(logERROR) << "Nullpointer to server object in ev_handler!";
		return;
	}

	if (ev != 0) {
		jhs->m_mgEv = ev;
		jhs->lastRequestTime = std::clock();
	}

	switch (ev) {
	case MG_EV_HTTP_REQUEST:

        LOG(logDEBUG) << "HTTP_REQUEST " << nc << " on socket " << nc->sock << ": " << std::string(hm->message.p, hm->method.len + hm->uri.len + hm->proto.len);

		if (ch != nullptr) {
            LOG(logWARNING) << "HTTP_REQUEST for connection that is already handled, waiting...";
			ch->blockUntilDone();
		}

		// for any GET request server static content
        if (is_equal(&hm->method, &s_get_method) || is_equal(&hm->method, &s_head_method))
		{
			// async stream handling
			if (starts_with(&hm->uri, &s_stream_query_prefix))
			{		
				auto streamName = std::string(&hm->uri.p[s_stream_query_prefix.len], &hm->uri.p[hm->uri.len]);
				LOG(logDebugHttp) << "incoming http stream request " << streamName;


				auto streamHandler = jhs->m_streamHandlers.find(streamName);
				if (streamHandler != jhs->m_streamHandlers.end()) {

					if (ch == nullptr) {
						nc->user_data = ch = new ConnectionHandler();
						jhs->activeConnectionHandlers.push_back(ch);
					}

                    ch->update(nc, hm);

					auto f = streamHandler->second;

                    {
                    jhs->threadPool.enqueue([jhs, nc, f]() {
                        auto ch = (ConnectionHandler*)nc->user_data;
						try {                            

                            StreamRequest request(ch);
                            StreamResponse response(jhs, nc);
                            f(request, response);
						}
                        catch (const std::exception &ex) {
							LOG(logERROR) << "Streaming error:" << ex.what();
						}
						if(!ch->connClosed)
							nc->flags |= MG_F_CLOSE_IMMEDIATELY; // stream connections always close
						ch->notifyDone();
					});
                }
                    LOG(logDEBUG) << "HTTP Streaming task queued!";
					return; // end handle streams
				}
			}

			s_http_server_opts.document_root = "webroot";
			mg_serve_http(nc, hm, s_http_server_opts);

			return; // end handle GET
		} 
		else if (is_equal(&hm->method, &s_post_method))
		{
			jhs->rpc_dispatch(nc, hm);
			return;
		}

		// Not Implemented other than POST and GET method
		mg_http_send_error(nc, 501, NULL);
		return; // ignore any other requests

    case MG_EV_WEBSOCKET_HANDSHAKE_REQUEST: {
        if (ch != nullptr) {
            LOG(logWARNING) << "HTTP_REQUEST for connection that is already handled, waiting...";
            ch->blockUntilDone();
        }

        if (ch == nullptr) {
            nc->user_data = ch = new ConnectionHandler();
            jhs->activeConnectionHandlers.push_back(ch);
        }
        ch->update(nc, hm);
    }break;




	case MG_EV_CLOSE:
		LOG(logDEBUG) << "MG_EV_CLOSE " << nc;
		//printf("MG_EV_CLOSE (%p)\n", nc);

        if (is_websocket(nc)) {
            LOG(logDEBUG)    << "Websocket closed!";
        }

		if (ch) {
			// notify thread that we are closing down & join
			ch->connClosed = true; // this shuts down the StreamWriter
			LOG(logDEBUG) << "Waiting for connection handler to stop...";
			LOG(logDEBUG) << "\tHTTP message was:" << ch->lastRequest.substr(0, 30);
            ch->blockUntilDone();
			LOG(logDEBUG) << "\tDONE WAITING!";

			// remove handler
			for (auto it = jhs->activeConnectionHandlers.begin(); it != jhs->activeConnectionHandlers.end(); it++) {
				if ((*it) == ch) {
					jhs->activeConnectionHandlers.erase(it);
					break;
				}
			}

			nc->user_data = nullptr;
			delete ch;
		}
		return;


     {
        if (ch != nullptr) {
            LOG(logWARNING) << "MG_EV_HTTP_REQUEST for connection that is already handled, waiting...";
            ch->blockUntilDone();
        }

        LOG(logDEBUG) << "MG_EV_WEBSOCKET_HANDSHAKE_REQUEST";

    }break;

    case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {

        auto wsEndpoint = ch->uri;
        LOG(logDebugHttp) << "incoming websocket request " << wsEndpoint;

        auto handler = jhs->m_wsHandlers.find(wsEndpoint);
        if (handler != jhs->m_wsHandlers.end()) {
            auto f = handler->second;

            {
            jhs->threadPool.enqueue([jhs, nc, f]() {
                auto ch = (ConnectionHandler*)nc->user_data;
                ch->sendBuf = new moodycamel::ConcurrentQueue<std::vector<uint8_t>>;
                ch->recvBuf = new moodycamel::ConcurrentQueue<uint8_t>;

                try {
                    WsConnection conn(ch);
                    WsRequest request(ch);
                    f(request, conn);
                } catch (const std::exception &ex) {
                    LOG(logERROR) << "Streaming error:" << ex.what();
                }
                if(!ch->connClosed)
                    nc->flags |= MG_F_CLOSE_IMMEDIATELY; // stream connections always close
                ch->notifyDone();
            });
        }
            LOG(logDEBUG) << "HTTP Streaming task queued!";
            return; // end handle streams
        } else {
            nc->flags |= MG_F_CLOSE_IMMEDIATELY;
        }

          break;
        }
        case MG_EV_WEBSOCKET_FRAME: {
          struct websocket_message *wm = (struct websocket_message *) ev_data;
          if(ch) {
              if(!ch->recvBuf) {
                  LOG(logERROR) << "MG_EV_WEBSOCKET_FRAME for connection with handler which has no buffers!";
                  return;
              }
              ch->recvBuf->enqueue_bulk((uint8_t*)wm->data, wm->size);
          }
          break;
        }
    }
}


bool JsonHttpServer::StreamResponse::isConnected() {
	ConnectionHandler *ch = (ConnectionHandler *)(nc->user_data);
	return  (ch && !ch->connClosed);
}


 std::string JsonHttpServer::StreamRequest::getHeader(std::string name) const
 {
         std::transform(name.begin(), name.end(), name.begin(), ::tolower);
         auto it = headers.find(name);
         if(it == headers.end()) {
             return "";
         }
         return it->second;
 }


 void JsonHttpServer::ConnectionHandler::poll() {

     // here we pump our send buffer
     if(sendBuf) {
        std::vector<uint8_t> frame;
        while(sendBuf->try_dequeue(frame))
        {
             mg_send_websocket_frame(this->nc, WEBSOCKET_OP_BINARY, frame.data(), frame.size());
        }
     }

 }

void JsonHttpServer::ConnectionHandler::update(struct mg_connection *nc, struct http_message *hm) {
     this->nc = nc;
     connClosed = false;
     lastRequest = std::string(hm->message.p, hm->message.len);
     uri = std::string(hm->uri.p, hm->uri.len);
     getVars = JsonNode(parseQuery(std::string(hm->query_string.p, hm->query_string.len)));
     requestHeaders = parseHeaders(hm);
 }


JsonHttpServer::StreamRequest::StreamRequest( const  ConnectionHandler *ch)
    : addr(*(SocketAddress*)&ch->nc->sa), data(ch->getVars), headers(ch->requestHeaders) {}
