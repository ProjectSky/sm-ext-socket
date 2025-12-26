/**
 * Socket TCP Server Example
 *
 * Demonstrates TCP echo server that accepts multiple client connections.
 *
 * Commands:
 *   sm_tcpserver [port] - Start TCP echo server (default port: 27020)
 *   sm_tcpstop          - Stop TCP server and disconnect all clients
 *
 * Usage:
 *   1. Load the plugin
 *   2. Run "sm_tcpserver" or "sm_tcpserver 12345" to start on custom port
 *   3. Connect with telnet/netcat: telnet localhost 27020
 *   4. Send messages and receive echoes
 *   5. Run "sm_tcpstop" to stop the server
 */

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <socket>

public Plugin myinfo = {
	name = "Socket TCP Server Example",
	author = "ProjectSky",
	description = "Demonstrates TCP echo server",
	version = "1.0.0",
	url = "https://github.com/ProjectSky/sm-ext-socket"
}

Socket g_ServerSocket;
ArrayList g_Clients;

public void OnPluginStart() {
	g_Clients = new ArrayList();
	RegConsoleCmd("sm_tcpserver", Command_TcpServer, "Start TCP echo server");
	RegConsoleCmd("sm_tcpstop", Command_TcpStop, "Stop TCP server");
}

Action Command_TcpServer(int client, int args) {
	if (g_ServerSocket != null) {
		ReplyToCommand(client, "[TCP Server] Already running");
		return Plugin_Handled;
	}

	int port = 27020;
	if (args >= 1) {
		char arg[16];
		GetCmdArg(1, arg, sizeof(arg));
		port = StringToInt(arg);
	}

	g_ServerSocket = new Socket();
	g_ServerSocket.SetOption(SocketReuseAddr, 1);
	g_ServerSocket.SetIncomingCallback(Socket_OnIncoming);
	g_ServerSocket.SetListenCallback(Socket_OnListen);
	g_ServerSocket.SetErrorCallback(Socket_OnServerError);

	g_ServerSocket.Bind("0.0.0.0", port);
	g_ServerSocket.Listen();

	return Plugin_Handled;
}

Action Command_TcpStop(int client, int args) {
	StopServer();
	ReplyToCommand(client, "[TCP Server] Stopped");
	return Plugin_Handled;
}

void StopServer() {
	// Close all client connections
	for (int i = 0; i < g_Clients.Length; i++) {
		Socket clientSocket = view_as<Socket>(g_Clients.Get(i));
		if (clientSocket != null) {
			delete clientSocket;
		}
	}
	g_Clients.Clear();

	// Close server socket
	if (g_ServerSocket != null) {
		delete g_ServerSocket;
		g_ServerSocket = null;
	}
}

void Socket_OnListen(Socket socket, const char[] localIP, int localPort, any data) {
	PrintToServer("[TCP Server] Listening on %s:%d", localIP, localPort);
}

void Socket_OnIncoming(Socket socket, Socket newSocket, const char[] remoteIP, int remotePort, any data) {
	PrintToServer("[TCP Server] Client connected: %s:%d", remoteIP, remotePort);

	newSocket.SetReceiveCallback(Socket_OnClientReceive);
	newSocket.SetDisconnectCallback(Socket_OnClientDisconnect);
	newSocket.SetErrorCallback(Socket_OnClientError);

	g_Clients.Push(newSocket);

	// Send welcome message
	newSocket.Send("Welcome to the echo server!\n");
}

void Socket_OnClientReceive(Socket socket, const char[] buffer, const int size, const char[] senderIP, int senderPort, any data) {
	PrintToServer("[TCP Server] Received from client: %s", buffer);

	// Echo back
	char response[1024];
	FormatEx(response, sizeof(response), "Echo: %s", buffer);
	socket.Send(response);
}

void Socket_OnClientDisconnect(Socket socket, any data) {
	PrintToServer("[TCP Server] Client disconnected");
	RemoveClient(socket);
}

void Socket_OnClientError(Socket socket, const int errorType, const char[] errorMsg, any data) {
	PrintToServer("[TCP Server] Client error: type=%d, message=%s", errorType, errorMsg);
	RemoveClient(socket);
}

void Socket_OnServerError(Socket socket, const int errorType, const char[] errorMsg, any data) {
	PrintToServer("[TCP Server] Server error: type=%d, message=%s", errorType, errorMsg);
	StopServer();
}

void RemoveClient(Socket socket) {
	int index = g_Clients.FindValue(socket);
	if (index != -1) {
		g_Clients.Erase(index);
	}
	delete socket;
}