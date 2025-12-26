/**
 * Socket Unix Domain Socket Example
 *
 * Demonstrates Unix domain socket communication (Linux/macOS only).
 * Unix sockets provide fast IPC (Inter-Process Communication) on the same machine.
 *
 * Commands:
 *   sm_unixserver         - Start Unix socket server
 *   sm_unixclient         - Connect to Unix socket server
 *   sm_unixsend <message> - Send message via Unix socket
 *   sm_unixstop           - Stop Unix socket server/client
 *
 * Usage:
 *   1. Load the plugin
 *   2. Run "sm_unixserver" to start the server
 *   3. Run "sm_unixclient" to connect as a client
 *   4. Run "sm_unixsend hello" to send a message
 *   5. Run "sm_unixstop" to stop everything
 *
 * Note: Unix sockets are NOT supported on Windows. The socket file is created at /tmp/sm_socket.sock
 */

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <socket>

public Plugin myinfo = {
	name = "Socket Unix Example",
	author = "ProjectSky",
	description = "Demonstrates Unix domain socket (Linux only)",
	version = "1.0.0",
	url = "https://github.com/ProjectSky/sm-ext-socket"
}

Socket g_ServerSocket;
Socket g_ClientSocket;
ArrayList g_Clients;

#define SOCKET_PATH "/tmp/sm_socket.sock"

public void OnPluginStart() {
	g_Clients = new ArrayList();

	RegConsoleCmd("sm_unixserver", Command_UnixServer, "Start Unix socket server");
	RegConsoleCmd("sm_unixclient", Command_UnixClient, "Connect to Unix socket server");
	RegConsoleCmd("sm_unixsend", Command_UnixSend, "Send message via Unix socket");
	RegConsoleCmd("sm_unixstop", Command_UnixStop, "Stop Unix socket server/client");
}

Action Command_UnixServer(int client, int args) {
	if (g_ServerSocket != null) {
		ReplyToCommand(client, "[Unix] Server already running");
		return Plugin_Handled;
	}

	g_ServerSocket = new Socket(SOCKET_UNIX);
	if (g_ServerSocket == null) {
		ReplyToCommand(client, "[Unix] Failed to create socket (Unix sockets not supported on Windows)");
		return Plugin_Handled;
	}

	g_ServerSocket.SetIncomingCallback(Socket_OnIncoming);
	g_ServerSocket.SetListenCallback(Socket_OnListen);
	g_ServerSocket.SetErrorCallback(Socket_OnServerError);

	g_ServerSocket.Bind(SOCKET_PATH, 0);
	g_ServerSocket.Listen();

	ReplyToCommand(client, "[Unix] Starting server on %s", SOCKET_PATH);
	return Plugin_Handled;
}

Action Command_UnixClient(int client, int args) {
	if (g_ClientSocket != null) {
		ReplyToCommand(client, "[Unix] Client already connected");
		return Plugin_Handled;
	}

	g_ClientSocket = new Socket(SOCKET_UNIX);
	if (g_ClientSocket == null) {
		ReplyToCommand(client, "[Unix] Failed to create socket");
		return Plugin_Handled;
	}

	g_ClientSocket.SetConnectCallback(Socket_OnConnect);
	g_ClientSocket.SetReceiveCallback(Socket_OnReceive);
	g_ClientSocket.SetDisconnectCallback(Socket_OnDisconnect);
	g_ClientSocket.SetErrorCallback(Socket_OnError);

	g_ClientSocket.Connect(SOCKET_PATH, 0);

	ReplyToCommand(client, "[Unix] Connecting to %s", SOCKET_PATH);
	return Plugin_Handled;
}

Action Command_UnixSend(int client, int args) {
	if (g_ClientSocket == null) {
		ReplyToCommand(client, "[Unix] Not connected");
		return Plugin_Handled;
	}

	if (args < 1) {
		ReplyToCommand(client, "Usage: sm_unixsend <message>");
		return Plugin_Handled;
	}

	char message[256];
	GetCmdArgString(message, sizeof(message));
	g_ClientSocket.Send(message);

	ReplyToCommand(client, "[Unix] Sent: %s", message);
	return Plugin_Handled;
}

Action Command_UnixStop(int client, int args) {
	StopAll();
	ReplyToCommand(client, "[Unix] Stopped");
	return Plugin_Handled;
}

void StopAll() {
	for (int i = 0; i < g_Clients.Length; i++) {
		Socket s = view_as<Socket>(g_Clients.Get(i));
		if (s != null) delete s;
	}
	g_Clients.Clear();

	if (g_ServerSocket != null) {
		delete g_ServerSocket;
		g_ServerSocket = null;
	}

	if (g_ClientSocket != null) {
		delete g_ClientSocket;
		g_ClientSocket = null;
	}
}

void Socket_OnListen(Socket socket, const char[] localIP, int localPort, any data) {
	PrintToServer("[Unix] Server listening on %s", localIP);
}

void Socket_OnIncoming(Socket socket, Socket newSocket, const char[] remoteIP, int remotePort, any data) {
	PrintToServer("[Unix] Client connected");

	newSocket.SetReceiveCallback(Socket_OnClientReceive);
	newSocket.SetDisconnectCallback(Socket_OnClientDisconnect);
	newSocket.SetErrorCallback(Socket_OnClientError);

	g_Clients.Push(newSocket);
	newSocket.Send("Welcome to Unix socket server!\n");
}

void Socket_OnClientReceive(Socket socket, const char[] buffer, const int size, const char[] senderIP, int senderPort, any data) {
	PrintToServer("[Unix Server] Received: %s", buffer);

	char response[512];
	FormatEx(response, sizeof(response), "Echo: %s", buffer);
	socket.Send(response);
}

void Socket_OnClientDisconnect(Socket socket, any data) {
	PrintToServer("[Unix Server] Client disconnected");
	int index = g_Clients.FindValue(socket);
	if (index != -1) g_Clients.Erase(index);
	delete socket;
}

void Socket_OnClientError(Socket socket, const int errorType, const char[] errorMsg, any data) {
	PrintToServer("[Unix Server] Client error: type=%d, message=%s", errorType, errorMsg);
	int index = g_Clients.FindValue(socket);
	if (index != -1) g_Clients.Erase(index);
	delete socket;
}

void Socket_OnConnect(Socket socket, any data) {
	PrintToServer("[Unix Client] Connected");
}

void Socket_OnReceive(Socket socket, const char[] buffer, const int size, const char[] senderIP, int senderPort, any data) {
	PrintToServer("[Unix Client] Received: %s", buffer);
}

void Socket_OnDisconnect(Socket socket, any data) {
	PrintToServer("[Unix Client] Disconnected");
	g_ClientSocket = null;
	delete socket;
}

void Socket_OnError(Socket socket, const int errorType, const char[] errorMsg, any data) {
	PrintToServer("[Unix Client] Error: type=%d, message=%s", errorType, errorMsg);
	g_ClientSocket = null;
	delete socket;
}

void Socket_OnServerError(Socket socket, const int errorType, const char[] errorMsg, any data) {
	PrintToServer("[Unix Server] Error: type=%d, message=%s", errorType, errorMsg);
	StopAll();
}