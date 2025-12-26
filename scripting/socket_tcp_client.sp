/**
 * Socket TCP Client Example
 *
 * Demonstrates basic TCP client connection to a remote server.
 *
 * Commands:
 *   sm_tcptest - Connect to example.com:80 and send HTTP GET request
 *
 * Usage:
 *   1. Load the plugin
 *   2. Run "sm_tcptest" in server console
 *   3. Watch console for connection status and response
 */

#pragma semicolon 1
#pragma newdecls required

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
	g_Socket.SetConnectCallback(Socket_OnConnect);
	g_Socket.SetReceiveCallback(Socket_OnReceive);
	g_Socket.SetDisconnectCallback(Socket_OnDisconnect);
	g_Socket.SetErrorCallback(Socket_OnError);

	// Connect to example.com on port 80
	g_Socket.Connect("example.com", 80);
	PrintToServer("[TCP] Connecting to example.com:80...");

	return Plugin_Handled;
}

void Socket_OnConnect(Socket socket, any data) {
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

void Socket_OnReceive(Socket socket, const char[] buffer, const int size, const char[] senderIP, int senderPort, any data) {
	PrintToServer("[TCP] Received %d bytes:", size);
	// Print first 200 chars to avoid flooding console
	char preview[201];
	strcopy(preview, sizeof(preview), buffer);
	PrintToServer("%s", preview);
}

void Socket_OnDisconnect(Socket socket, any data) {
	PrintToServer("[TCP] Disconnected");
	delete g_Socket;
	g_Socket = null;
}

void Socket_OnError(Socket socket, const int errorType, const char[] errorMsg, any data) {
	PrintToServer("[TCP] Error: type=%d, message=%s", errorType, errorMsg);
	delete g_Socket;
	g_Socket = null;
}