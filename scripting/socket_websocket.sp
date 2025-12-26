/**
 * Socket WebSocket Client Example
 *
 * Demonstrates a basic WebSocket client implementation over TCP.
 * Note: Only supports WS (unencrypted), not WSS (TLS).
 *
 * Commands:
 *   sm_wsconnect <host> <port> [path]  - Connect to WebSocket server
 *   sm_wssend <message>                - Send text message
 *   sm_wsclose                         - Close connection
 *
 * Testing (start a local WS server first):
 *   - Python: python -c "import asyncio,websockets;asyncio.run(websockets.serve(lambda w,p:asyncio.gather(w.send('Echo:'+m)async for m in w),port=8765))"
 *   - Node.js: npx wscat -l 8765
 *   - websocat: websocat -s 8765
 *   Then: sm_wsconnect localhost 8765 /
 */

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <socket>

public Plugin myinfo = {
	name = "Socket WebSocket Client",
	author = "ProjectSky",
	description = "WebSocket client over TCP socket",
	version = "1.0.0",
	url = "https://github.com/ProjectSky/sm-ext-socket"
}

enum WSState {
	WS_DISCONNECTED,
	WS_CONNECTING,
	WS_HANDSHAKING,
	WS_CONNECTED
}

Socket g_Socket;
WSState g_State = WS_DISCONNECTED;
char g_Host[128];
char g_Path[256];
char g_WebSocketKey[32];
char g_RecvBuffer[65536];
int g_RecvBufferLen;

public void OnPluginStart() {
	RegConsoleCmd("sm_wsconnect", Command_Connect, "Connect to WebSocket server");
	RegConsoleCmd("sm_wssend", Command_Send, "Send WebSocket message");
	RegConsoleCmd("sm_wsclose", Command_Close, "Close WebSocket connection");
}

Action Command_Connect(int client, int args) {
	if (g_State != WS_DISCONNECTED) {
		ReplyToCommand(client, "[WS] Already connected or connecting");
		return Plugin_Handled;
	}

	if (args < 2) {
		ReplyToCommand(client, "Usage: sm_wsconnect <host> <port> [path]");
		return Plugin_Handled;
	}

	char portStr[16];
	GetCmdArg(1, g_Host, sizeof(g_Host));
	GetCmdArg(2, portStr, sizeof(portStr));
	int port = StringToInt(portStr);

	if (args >= 3) {
		GetCmdArg(3, g_Path, sizeof(g_Path));
	} else {
		strcopy(g_Path, sizeof(g_Path), "/");
	}

	// Generate random WebSocket key (base64 of 16 random bytes, simplified)
	GenerateWebSocketKey(g_WebSocketKey, sizeof(g_WebSocketKey));

	g_Socket = new Socket(SOCKET_TCP);
	g_Socket.SetConnectCallback(Socket_OnConnect);
	g_Socket.SetReceiveCallback(Socket_OnReceive);
	g_Socket.SetDisconnectCallback(Socket_OnDisconnect);
	g_Socket.SetErrorCallback(Socket_OnError);

	g_State = WS_CONNECTING;
	g_RecvBufferLen = 0;
	g_Socket.Connect(g_Host, port);

	PrintToServer("[WS] Connecting to %s:%d%s", g_Host, port, g_Path);
	return Plugin_Handled;
}

Action Command_Send(int client, int args) {
	if (g_State != WS_CONNECTED) {
		ReplyToCommand(client, "[WS] Not connected");
		return Plugin_Handled;
	}

	if (args < 1) {
		ReplyToCommand(client, "Usage: sm_wssend <message>");
		return Plugin_Handled;
	}

	char message[1024];
	GetCmdArgString(message, sizeof(message));

	SendWebSocketFrame(message);
	PrintToServer("[WS] Sent: %s", message);
	return Plugin_Handled;
}

Action Command_Close(int client, int args) {
	if (g_State == WS_DISCONNECTED) {
		ReplyToCommand(client, "[WS] Not connected");
		return Plugin_Handled;
	}

	if (g_State == WS_CONNECTED) {
		// Send close frame (opcode 0x8) with mask
		char closeFrame[6];
		closeFrame[0] = 0x88; // FIN + Close opcode
		closeFrame[1] = 0x80; // Mask bit set, 0 length payload
		// Generate random mask key
		closeFrame[2] = GetRandomInt(0, 255); // Mask key byte 1
		closeFrame[3] = GetRandomInt(0, 255); // Mask key byte 2
		closeFrame[4] = GetRandomInt(0, 255); // Mask key byte 3
		closeFrame[5] = GetRandomInt(0, 255); // Mask key byte 4
		g_Socket.Send(closeFrame, 6);
	}

	CloseConnection();
	ReplyToCommand(client, "[WS] Closed");
	return Plugin_Handled;
}

void Socket_OnConnect(Socket socket, any data) {
	PrintToServer("[WS] TCP connected, sending handshake...");
	g_State = WS_HANDSHAKING;

	// Send WebSocket upgrade request
	char request[1024];
	FormatEx(request, sizeof(request),
		"GET %s HTTP/1.1\r\n"
		... "Host: %s\r\n"
		... "Upgrade: websocket\r\n"
		... "Connection: Upgrade\r\n"
		... "Sec-WebSocket-Key: %s\r\n"
		... "Sec-WebSocket-Version: 13\r\n"
		... "\r\n",
		g_Path, g_Host, g_WebSocketKey);

	socket.Send(request, -1);
}

void Socket_OnReceive(Socket socket, const char[] buffer, const int size, const char[] senderIP, int senderPort, any data) {
	// Append to receive buffer
	for (int i = 0; i < size && g_RecvBufferLen < sizeof(g_RecvBuffer) - 1; i++) {
		g_RecvBuffer[g_RecvBufferLen++] = buffer[i];
	}
	g_RecvBuffer[g_RecvBufferLen] = '\0';

	if (g_State == WS_HANDSHAKING) {
		// Check for complete HTTP response
		if (StrContains(g_RecvBuffer, "\r\n\r\n") != -1) {
			if (StrContains(g_RecvBuffer, "101") != -1 && StrContains(g_RecvBuffer, "Upgrade") != -1) {
				PrintToServer("[WS] Handshake successful, connected!");
				g_State = WS_CONNECTED;

				// Find end of HTTP headers and process any remaining data as WebSocket frames
				int headerEnd = StrContains(g_RecvBuffer, "\r\n\r\n") + 4;
				if (headerEnd < g_RecvBufferLen) {
					// Move remaining data to start of buffer
					int remaining = g_RecvBufferLen - headerEnd;
					for (int i = 0; i < remaining; i++) {
						g_RecvBuffer[i] = g_RecvBuffer[headerEnd + i];
					}
					g_RecvBufferLen = remaining;
					ProcessWebSocketFrames();
				} else {
					g_RecvBufferLen = 0;
				}
			} else {
				PrintToServer("[WS] Handshake failed: %s", g_RecvBuffer);
				CloseConnection();
			}
		}
	} else if (g_State == WS_CONNECTED) {
		ProcessWebSocketFrames();
	}
}

void Socket_OnDisconnect(Socket socket, any data) {
	PrintToServer("[WS] Disconnected");
	g_State = WS_DISCONNECTED;
	g_Socket = null;
}

void Socket_OnError(Socket socket, const int errorType, const char[] errorMsg, any data) {
	PrintToServer("[WS] Error: %s", errorMsg);
	CloseConnection();
}

void CloseConnection() {
	if (g_Socket != null) {
		delete g_Socket;
		g_Socket = null;
	}
	g_State = WS_DISCONNECTED;
	g_RecvBufferLen = 0;
}

void SendWebSocketFrame(const char[] message) {
	int msgLen = strlen(message);
	char frame[1400];
	int frameLen = 0;

	// First byte: FIN (1) + RSV (000) + Opcode (0001 = text)
	frame[frameLen++] = 0x81;

	// Second byte: MASK (1) + Payload length
	if (msgLen <= 125) {
		frame[frameLen++] = 0x80 | msgLen;
	} else if (msgLen <= 65535) {
		frame[frameLen++] = 0x80 | 126;
		frame[frameLen++] = (msgLen >> 8) & 0xFF;
		frame[frameLen++] = msgLen & 0xFF;
	} else {
		PrintToServer("[WS] Message too long");
		return;
	}

	// Masking key (4 bytes, using simple values for demo)
	int maskKey[4];
	maskKey[0] = GetRandomInt(0, 255);
	maskKey[1] = GetRandomInt(0, 255);
	maskKey[2] = GetRandomInt(0, 255);
	maskKey[3] = GetRandomInt(0, 255);

	frame[frameLen++] = maskKey[0];
	frame[frameLen++] = maskKey[1];
	frame[frameLen++] = maskKey[2];
	frame[frameLen++] = maskKey[3];

	// Masked payload
	for (int i = 0; i < msgLen; i++) {
		frame[frameLen++] = message[i] ^ maskKey[i % 4];
	}

	g_Socket.Send(frame, frameLen);
}

void ProcessWebSocketFrames() {
	while (g_RecvBufferLen >= 2) {
		int opcode = g_RecvBuffer[0] & 0x0F;
		bool masked = (g_RecvBuffer[1] & 0x80) != 0;
		int payloadLen = g_RecvBuffer[1] & 0x7F;
		int headerLen = 2;

		if (payloadLen == 126) {
			if (g_RecvBufferLen < 4) return; // Need more data
			payloadLen = ((g_RecvBuffer[2] & 0xFF) << 8) | (g_RecvBuffer[3] & 0xFF);
			headerLen = 4;
		} else if (payloadLen == 127) {
			if (g_RecvBufferLen < 10) return;
			// 64-bit length, just use lower 32 bits
			payloadLen = ((g_RecvBuffer[6] & 0xFF) << 24) | ((g_RecvBuffer[7] & 0xFF) << 16) |
			             ((g_RecvBuffer[8] & 0xFF) << 8) | (g_RecvBuffer[9] & 0xFF);
			headerLen = 10;
		}

		if (masked) headerLen += 4;

		int totalLen = headerLen + payloadLen;
		if (g_RecvBufferLen < totalLen) return; // Need more data

		// Extract payload
		char payload[4096];
		int payloadStart = headerLen;

		if (masked) {
			int maskOffset = headerLen - 4;
			for (int i = 0; i < payloadLen && i < sizeof(payload) - 1; i++) {
				payload[i] = g_RecvBuffer[payloadStart + i] ^ g_RecvBuffer[maskOffset + (i % 4)];
			}
		} else {
			for (int i = 0; i < payloadLen && i < sizeof(payload) - 1; i++) {
				payload[i] = g_RecvBuffer[payloadStart + i];
			}
		}
		payload[payloadLen < sizeof(payload) ? payloadLen : sizeof(payload) - 1] = '\0';

		// Handle frame by opcode
		switch (opcode) {
			case 0x1: {
				PrintToServer("[WS] Received: %s", payload);
			}
			case 0x2: {
				PrintToServer("[WS] Received binary (%d bytes)", payloadLen);
			}
			case 0x8: {
				PrintToServer("[WS] Server sent close frame");
				CloseConnection();
				return;
			}
			case 0x9: {
				PrintToServer("[WS] Received ping, sending pong");
				SendPongFrame(payload, payloadLen);
			}
			case 0xA: {
				PrintToServer("[WS] Received pong");
			}
		}

		// Remove processed frame from buffer
		int remaining = g_RecvBufferLen - totalLen;
		for (int i = 0; i < remaining; i++) {
			g_RecvBuffer[i] = g_RecvBuffer[totalLen + i];
		}
		g_RecvBufferLen = remaining;
	}
}

void SendPongFrame(const char[] payload, int payloadLen) {
	char frame[256];
	int frameLen = 0;

	frame[frameLen++] = 0x8A; // FIN + Pong opcode

	if (payloadLen <= 125) {
		frame[frameLen++] = 0x80 | payloadLen;
	} else {
		return; // Pong payload too large
	}

	// Masking key
	int maskKey[4];
	maskKey[0] = GetRandomInt(0, 255);
	maskKey[1] = GetRandomInt(0, 255);
	maskKey[2] = GetRandomInt(0, 255);
	maskKey[3] = GetRandomInt(0, 255);

	frame[frameLen++] = maskKey[0];
	frame[frameLen++] = maskKey[1];
	frame[frameLen++] = maskKey[2];
	frame[frameLen++] = maskKey[3];

	for (int i = 0; i < payloadLen; i++) {
		frame[frameLen++] = payload[i] ^ maskKey[i % 4];
	}

	g_Socket.Send(frame, frameLen);
}

void GenerateWebSocketKey(char[] key, int maxlen) {
	// Generate a simple base64-like key (not cryptographically secure, but works for demo)
	char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for (int i = 0; i < 22 && i < maxlen - 3; i++) {
		key[i] = chars[GetRandomInt(0, 63)];
	}
	key[22] = '=';
	key[23] = '=';
	key[24] = '\0';
}