# masaj
This is a C++ wrapper for mongoose.c to create a JSONRPC server that supports async streaming too.


# RPC Server example
```
bool g_isRunning = true;
JsonHttpServer server;

// JSONRPC method "hello":
// $ curl 'http://localhost/' -H 'Content-Type: application/json' '{"id":1,"method":"hello","params":{"name":"foo"}}'
server.on("hello", [](const SocketAddress &addr, const JsonNode &request, JsonNode &response) {	
	auto name = request["name"].str;
	if (name.empty()) {
		response["error"] = "Say your name!";
		return;
	}
	response["name"] = "masaj";
	response["reply"] = "Hi " + name + "!";
	return;
});

if (!server.start("80")) {
	g_isRunning = false;
}

while (g_isRunning) {
	server.update();
}
```


# Stream handler example (GET request)
curl 'http://localhost/stream/test-stream'

```
server.onStream("test-stream", [](const SocketAddress &addr, const JsonNode &request, JsonHttpServer::StreamResponse &response) {
	response.addHeader("Content-Type", "text/html");
	response.setSendBufferSize(0); // instantly flush
	response.write("<html><head><title>textstream</title></head><body><pre>");
	int i = 0;
	while (true) {
		std::string l = "LINE:" + std::to_string(i++) + " @ " + std::to_string(std::clock()) + "\n";
		if (!response.write(l.c_str(), l.length() + 1))
			break; // browser has closed connection
		usleep(1000 * 1000);
	}
});
```

Note that the stream handler runs in a seperate thread.
