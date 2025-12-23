#include <sourcemod>
#include <socket>

public Plugin myinfo = {
	name = "Socket HTTP GET Example",
	author = "ProjectSky",
	description = "Simple HTTP GET request helper",
	version = "1.0.0",
	url = "https://github.com/ProjectSky/sm-ext-socket"
}

// HTTP request context
enum struct HttpRequest {
	Socket socket;
	char host[128];
	int port;
	char path[256];
	char response[8192];
	int responseLen;
}

ArrayList g_Requests;

public void OnPluginStart() {
	g_Requests = new ArrayList(sizeof(HttpRequest));
	RegConsoleCmd("sm_httpget", Command_HttpGet, "Make HTTP GET request");
}

// Usage: sm_httpget <host> [port] [path]
Action Command_HttpGet(int client, int args) {
	if (args < 1) {
		ReplyToCommand(client, "Usage: sm_httpget <host> [port] [path]");
		return Plugin_Handled;
	}

	HttpRequest req;
	GetCmdArg(1, req.host, sizeof(req.host));
	req.port = 80;

	if (args >= 2) {
		char portStr[16];
		GetCmdArg(2, portStr, sizeof(portStr));
		req.port = StringToInt(portStr);
		if (req.port <= 0) req.port = 80;
	}

	if (args >= 3) {
		GetCmdArg(3, req.path, sizeof(req.path));
	} else {
		strcopy(req.path, sizeof(req.path), "/");
	}

	req.socket = new Socket();
	req.socket.SetOption(SocketConnectTimeout, 10000);  // 10 second timeout
	req.socket.SetConnectCallback(OnHttpConnect);
	req.socket.SetReceiveCallback(OnHttpReceive);
	req.socket.SetDisconnectCallback(OnHttpDisconnect);
	req.socket.SetErrorCallback(OnHttpError);

	int index = g_Requests.PushArray(req);
	req.socket.Connect(req.host, req.port);

	PrintToServer("[HTTP] GET http://%s:%d%s (request #%d)", req.host, req.port, req.path, index);

	return Plugin_Handled;
}

static void OnHttpConnect(Socket socket, any data) {
	int index = FindRequestBySocket(socket);
	if (index == -1) return;

	HttpRequest req;
	g_Requests.GetArray(index, req);

	char request[512];
	FormatEx(request, sizeof(request),
		"GET %s HTTP/1.1\r\n"
		... "Host: %s\r\n"
		... "User-Agent: SourceMod-Socket/1.0\r\n"
		... "Accept: */*\r\n"
		... "Connection: close\r\n"
		... "\r\n",
		req.path, req.host);

	socket.Send(request);
}

static void OnHttpReceive(Socket socket, const char[] buffer, const int size, const char[] senderIP, int senderPort, any data) {
	int index = FindRequestBySocket(socket);
	if (index == -1) return;

	HttpRequest req;
	g_Requests.GetArray(index, req);

	// Append to response buffer
	int remaining = sizeof(req.response) - req.responseLen - 1;
	if (remaining > 0) {
		int copyLen = size < remaining ? size : remaining;
		for (int i = 0; i < copyLen; i++) {
			req.response[req.responseLen + i] = buffer[i];
		}
		req.responseLen += copyLen;
		req.response[req.responseLen] = '\0';
	}

	g_Requests.SetArray(index, req);
}

static void OnHttpDisconnect(Socket socket, any data) {
	int index = FindRequestBySocket(socket);
	if (index == -1) return;

	HttpRequest req;
	g_Requests.GetArray(index, req);

	PrintToServer("[HTTP] Response from %s (%d bytes):", req.host, req.responseLen);

	// Find and print status line
	int headerEnd = StrContains(req.response, "\r\n");
	if (headerEnd != -1) {
		char statusLine[128];
		strcopy(statusLine, headerEnd + 1 < sizeof(statusLine) ? headerEnd + 1 : sizeof(statusLine), req.response);
		PrintToServer("Status: %s", statusLine);
	}

	// Print body preview (skip headers)
	int bodyStart = StrContains(req.response, "\r\n\r\n");
	if (bodyStart != -1) {
		char body[512];
		strcopy(body, sizeof(body), req.response[bodyStart + 4]);
		PrintToServer("Body preview: %.200s...", body);
	}

	CleanupRequest(index);
}

static void OnHttpError(Socket socket, const int errorType, const int errorNum, any data) {
	int index = FindRequestBySocket(socket);
	if (index == -1) return;

	HttpRequest req;
	g_Requests.GetArray(index, req);

	PrintToServer("[HTTP] Error for %s:%d: type=%d, errno=%d", req.host, req.port, errorType, errorNum);
	CleanupRequest(index);
}

int FindRequestBySocket(Socket socket) {
	HttpRequest req;
	for (int i = 0; i < g_Requests.Length; i++) {
		g_Requests.GetArray(i, req);
		if (req.socket == socket) {
			return i;
		}
	}
	return -1;
}

void CleanupRequest(int index) {
	HttpRequest req;
	g_Requests.GetArray(index, req);
	delete req.socket;
	g_Requests.Erase(index);
}

public void OnPluginEnd() {
	HttpRequest req;
	for (int i = 0; i < g_Requests.Length; i++) {
		g_Requests.GetArray(i, req);
		delete req.socket;
	}
	delete g_Requests;
}