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

// Start UDP server to receive messages
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
	g_UdpSocket.SetReceiveCallback(OnUdpReceive);
	g_UdpSocket.SetListenCallback(OnUdpListenReady);
	g_UdpSocket.SetErrorCallback(OnUdpError);

	g_UdpSocket.Bind("0.0.0.0", port);
	g_UdpSocket.Listen();

	return Plugin_Handled;
}

// Send UDP message: sm_udpsend <host> <port> <message>
// Note: Uses the server socket if running, otherwise creates a temporary one
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

	// Use existing server socket if available, otherwise create new one
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

void OnUdpListenReady(Socket socket, const char[] localIP, int localPort, any data) {
	PrintToServer("[UDP] Listening on %s:%d", localIP, localPort);
}

static void OnUdpReceive(Socket socket, const char[] buffer, const int size, const char[] senderIP, int senderPort, any data) {
	PrintToServer("[UDP] Received from %s:%d (%d bytes): %s", senderIP, senderPort, size, buffer);

	// Don't echo back echo responses (prevent infinite loop)
	if (strncmp(buffer, "Echo:", 5) == 0) return;

	// Echo back to sender
	char response[512];
	FormatEx(response, sizeof(response), "Echo: %s", buffer);
	socket.SendTo(response, -1, senderIP, senderPort);
}

void OnUdpError(Socket socket, const int errorType, const int errorNum, any data) {
	PrintToServer("[UDP] Error: type=%d, errno=%d", errorType, errorNum);
}

public void OnPluginEnd() {
	if (g_UdpSocket != null) {
		delete g_UdpSocket;
	}
}