#include <sourcemod>
#include <socket>

public Plugin myinfo = {
	name = "Socket TCP Client Example",
	author = "ProjectSky",
	description = "Demonstrates TCP client connection",
	version = "1.0.0",
	url = "https://github.com/ProjectSky/sm-ext-socket"
}

Socket g_Socket;

public void OnPluginStart() {
	RegConsoleCmd("sm_tcptest", Command_TcpTest, "Test TCP connection");
}

Action Command_TcpTest(int client, int args) {
	if (g_Socket != null) {
		delete g_Socket;
	}

	g_Socket = new Socket();
	g_Socket.SetConnectCallback(OnSocketConnect);
	g_Socket.SetReceiveCallback(OnSocketReceive);
	g_Socket.SetDisconnectCallback(OnSocketDisconnect);
	g_Socket.SetErrorCallback(OnSocketError);

	// Connect to example.com on port 80
	g_Socket.Connect("example.com", 80);
	PrintToServer("[TCP] Connecting to example.com:80...");

	return Plugin_Handled;
}

static void OnSocketConnect(Socket socket, any data) {
	PrintToServer("[TCP] Connected!");

	// Send HTTP GET request
	char request[256];
	FormatEx(request, sizeof(request),
		"GET / HTTP/1.1\r\n"
		... "Host: example.com\r\n"
		... "Connection: close\r\n"
		... "\r\n");

	socket.Send(request);
	PrintToServer("[TCP] Request sent");
}

static void OnSocketReceive(Socket socket, const char[] buffer, const int size, const char[] senderIP, int senderPort, any data) {
	PrintToServer("[TCP] Received %d bytes:", size);
	// Print first 200 chars to avoid flooding console
	char preview[201];
	strcopy(preview, sizeof(preview), buffer);
	PrintToServer("%s", preview);
}

static void OnSocketDisconnect(Socket socket, any data) {
	PrintToServer("[TCP] Disconnected");
	delete g_Socket;
	g_Socket = null;
}

static void OnSocketError(Socket socket, const int errorType, const int errorNum, any data) {
	PrintToServer("[TCP] Error: type=%d, errno=%d", errorType, errorNum);
	delete g_Socket;
	g_Socket = null;
}