/**
 * Socket UDP Example
 *
 * Demonstrates UDP server that receives messages and echoes them back.
 *
 * Commands:
 *   sm_udpserver [port]              - Start UDP server (default port: 27021)
 *   sm_udpsend <host> <port> <msg>   - Send UDP message to specified destination
 *   sm_udpstop                       - Stop UDP server
 *
 * Usage:
 *   1. Load the plugin
 *   2. Run "sm_udpserver" to start listening
 *   3. Send messages from another client: echo "hello" | nc -u 127.0.0.1 27021
 *   4. Or use "sm_udpsend 127.0.0.1 27021 hello" from another server
 *   5. Run "sm_udpstop" to stop the server
 */

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <socket>

public Plugin myinfo = {
	name = "Socket UDP Example",
	author = "ProjectSky",
	description = "Demonstrates UDP send/receive",
	version = "1.0.0",
	url = "https://github.com/ProjectSky/sm-ext-socket"
}

Socket g_UdpSocket;

public void OnPluginStart() {
	RegConsoleCmd("sm_udpserver", Command_UdpServer, "Start UDP server");
	RegConsoleCmd("sm_udpsend", Command_UdpSend, "Send UDP message");
	RegConsoleCmd("sm_udpstop", Command_UdpStop, "Stop UDP server");
}

Action Command_UdpServer(int client, int args) {
	if (g_UdpSocket != null) {
		ReplyToCommand(client, "[UDP] Already running");
		return Plugin_Handled;
	}

	int port = 27021;
	if (args >= 1) {
		char arg[16];
		GetCmdArg(1, arg, sizeof(arg));
		port = StringToInt(arg);
	}

	g_UdpSocket = new Socket(SOCKET_UDP);
	g_UdpSocket.SetOption(SocketReuseAddr, 1);
	g_UdpSocket.SetReceiveCallback(Socket_OnReceive);
	g_UdpSocket.SetListenCallback(Socket_OnListen);
	g_UdpSocket.SetErrorCallback(Socket_OnError);

	g_UdpSocket.Bind("0.0.0.0", port);
	g_UdpSocket.Listen();

	return Plugin_Handled;
}

Action Command_UdpSend(int client, int args) {
	if (args < 3) {
		ReplyToCommand(client, "Usage: sm_udpsend <host> <port> <message>");
		return Plugin_Handled;
	}

	char host[64], portStr[16], message[256];
	GetCmdArg(1, host, sizeof(host));
	GetCmdArg(2, portStr, sizeof(portStr));
	GetCmdArg(3, message, sizeof(message));

	int port = StringToInt(portStr);

	if (g_UdpSocket != null) {
		g_UdpSocket.SendTo(message, -1, host, port);
		PrintToServer("[UDP] Sent via server socket to %s:%d: %s", host, port, message);
	} else {
		ReplyToCommand(client, "[UDP] Start server first with sm_udpserver to send messages");
	}

	return Plugin_Handled;
}

Action Command_UdpStop(int client, int args) {
	if (g_UdpSocket != null) {
		delete g_UdpSocket;
		g_UdpSocket = null;
		ReplyToCommand(client, "[UDP] Stopped");
	}
	return Plugin_Handled;
}

void Socket_OnListen(Socket socket, const char[] localIP, int localPort, any data) {
	PrintToServer("[UDP] Listening on %s:%d", localIP, localPort);
}

void Socket_OnReceive(Socket socket, const char[] buffer, const int size, const char[] senderIP, int senderPort, any data) {
	PrintToServer("[UDP] Received from %s:%d (%d bytes): %s", senderIP, senderPort, size, buffer);

	// Don't echo back echo responses (prevent infinite loop)
	if (strncmp(buffer, "Echo:", 5) == 0) return;

	// Echo back to sender
	char response[512];
	FormatEx(response, sizeof(response), "Echo: %s", buffer);
	socket.SendTo(response, -1, senderIP, senderPort);
}

void Socket_OnError(Socket socket, const int errorType, const char[] errorMsg, any data) {
	PrintToServer("[UDP] Error: type=%d, message=%s", errorType, errorMsg);
}