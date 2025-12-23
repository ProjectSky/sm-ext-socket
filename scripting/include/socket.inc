#if defined _socket_included
	#endinput
#endif
#define _socket_included

enum SocketType {
	SOCKET_TCP = 1,
	SOCKET_UDP
}

enum SocketErrorType {
	SOCKET_EMPTY_HOST = 1,
	SOCKET_NO_HOST,
	SOCKET_CONNECT_ERROR,
	SOCKET_SEND_ERROR,
	SOCKET_BIND_ERROR,
	SOCKET_RECV_ERROR,
	SOCKET_LISTEN_ERROR
}

enum SocketOption {
	ConcatenateCallbacks = 1,  // Max chunk size for concatenated callbacks (0 = disabled, min 4096)
	ForceFrameLock,            // Force mutex lock in GameFrame() (may cause lag)
	CallbacksPerFrame,         // Max callbacks per game frame (default: 1)
	SocketBroadcast,           // SO_BROADCAST
	SocketReuseAddr,           // SO_REUSEADDR
	SocketKeepAlive,           // SO_KEEPALIVE
	SocketLinger,              // SO_LINGER (seconds, 0 = disabled)
	SocketOOBInline,           // SO_OOBINLINE
	SocketSendBuffer,          // SO_SNDBUF (bytes)
	SocketReceiveBuffer,       // SO_RCVBUF (bytes)
	SocketDontRoute,           // SO_DONTROUTE
	SocketReceiveLowWatermark, // SO_RCVLOWAT (bytes)
	SocketReceiveTimeout,      // SO_RCVTIMEO (ms, 0 = disabled)
	SocketSendLowWatermark,    // SO_SNDLOWAT (bytes)
	SocketSendTimeout,         // SO_SNDTIMEO (ms, 0 = disabled)
	DebugMode,                 // Enable debug logging
	SocketConnectTimeout,      // Connect timeout (ms, 0 = disabled, TCP only)
	SocketAutoClose            // Auto close handle on disconnect/error (0 = disabled, 1 = enabled)
}

/**
 * Callback for connection established
 *
 * @note TCP: Called when connection to remote host is established
 * @note UDP: Called when Connect() hostname resolution completes (UDP is connectionless)
 *
 * @param socket    Socket handle
 * @param data      User data passed to SetConnectCallback
 */
typedef SocketConnectCallback = function void (Socket socket, any data);

/**
 * Callback for incoming connection on listening socket
 *
 * @note TCP only: Called when a new client connects to a listening socket
 * @note UDP: Not applicable (UDP is connectionless, use ReceiveCallback instead)
 *
 * @param socket      Listening socket handle
 * @param newSocket   New client socket handle
 * @param remoteIP    Client IP address
 * @param remotePort  Client port
 * @param data        User data passed to SetIncomingCallback
 */
typedef SocketIncomingCallback = function void (Socket socket, Socket newSocket, const char[] remoteIP, int remotePort, any data);

/**
 * Callback for data received
 *
 * @note TCP: Called when data is received from connected peer
 * @note UDP: Called when data is received from any sender
 *
 * @param socket      Socket handle
 * @param buffer      Received data (null-terminated)
 * @param size        Data length (excluding null terminator)
 * @param senderIP    Sender IP address (UDP only, empty for TCP)
 * @param senderPort  Sender port (UDP only, 0 for TCP)
 * @param data        User data passed to SetReceiveCallback
 */
typedef SocketReceiveCallback = function void (Socket socket, const char[] buffer, const int size, const char[] senderIP, int senderPort, any data);

/**
 * Callback for remote disconnection
 *
 * @note TCP only: Called when remote peer closes the connection
 * @note UDP: Not applicable (UDP is connectionless)
 *
 * @param socket    Socket handle
 * @param data      User data passed to SetDisconnectCallback
 */
typedef SocketDisconnectCallback = function void (Socket socket, any data);

/**
 * Callback for socket error
 *
 * @param socket      Socket handle
 * @param errorType   Error type (SocketErrorType)
 * @param errorNum    System errno
 * @param data        User data passed to SetErrorCallback
 */
typedef SocketErrorCallback = function void (Socket socket, const int errorType, const int errorNum, any data);

/**
 * Callback for listen ready
 *
 * @note Called when Listen() completes and socket is ready to accept connections (TCP) or receive data (UDP)
 *
 * @param socket       Socket handle
 * @param localIP      Local bound IP address
 * @param localPort    Local bound port
 * @param data         User data passed to SetListenCallback
 */
typedef SocketListenCallback = function void (Socket socket, const char[] localIP, int localPort, any data);

// Socket methodmap for TCP/UDP communication
methodmap Socket < Handle {
	/**
	 * Creates a new socket
	 *
	 * @param protocol    Protocol type (default: SOCKET_TCP)
	 * @return            Socket handle or null on failure
	 */
	public native Socket(SocketType protocol = SOCKET_TCP);

	/**
	 * Binds socket to local address
	 *
	 * @param address    Local IP or hostname
	 * @param port       Local port
	 * @return           True on success
	 */
	public native bool Bind(const char[] address, int port);

	/**
	 * Connects to remote host
	 *
	 * @note TCP: Establishes a connection to the remote host
	 * @note UDP: Sets default destination for Send() (no actual connection)
	 *
	 * @param host    Remote IP or hostname
	 * @param port    Remote port
	 */
	public native void Connect(const char[] host, int port);

	/**
	 * Disconnects socket without closing handle
	 *
	 * @return    True on success
	 */
	public native bool Disconnect();

	/**
	 * Forcefully closes TCP connection with RST (TCP only)
	 *
	 * @note Immediately terminates connection without TIME_WAIT state
	 * @note Only works for TCP sockets, returns false for UDP
	 *
	 * @return    True on success
	 */
	public native bool CloseReset();

	/**
	 * Starts listening for connections (TCP) or receiving data (UDP)
	 *
	 * @note TCP: Starts accepting incoming connections (requires SetIncomingCallback)
	 * @note UDP: Starts receiving data (use SetReceiveCallback)
	 *
	 * @return    True on success
	 */
	public native bool Listen();

	/**
	 * Sends data
	 *
	 * @note TCP: Sends to connected peer
	 * @note UDP: Requires prior Connect() call to set destination
	 *
	 * @param data    Data to send
	 * @param size    Data length (-1 = strlen)
	 */
	public native void Send(const char[] data, int size = -1);

	/**
	 * Sends data to specific destination (UDP only)
	 *
	 * @param data    Data to send
	 * @param size    Data length (-1 = strlen)
	 * @param host    Destination IP or hostname
	 * @param port    Destination port
	 */
	public native void SendTo(const char[] data, int size = -1, const char[] host, int port);

	/**
	 * Sets socket option
	 *
	 * @param option    Option to set
	 * @param value     Option value
	 * @return          True on success false on failure
	 */
	public native bool SetOption(SocketOption option, int value);

	/**
	 * Sets receive callback
	 *
	 * @param callback    Callback function
	 * @param data        User data passed to callback
	 */
	public native void SetReceiveCallback(SocketReceiveCallback callback, any data = 0);

	/**
	 * Sets disconnect callback
	 *
	 * @param callback    Callback function
	 * @param data        User data passed to callback
	 */
	public native void SetDisconnectCallback(SocketDisconnectCallback callback, any data = 0);

	/**
	 * Sets error callback
	 *
	 * @param callback    Callback function
	 * @param data        User data passed to callback
	 */
	public native void SetErrorCallback(SocketErrorCallback callback, any data = 0);

	/**
	 * Sets connect callback
	 *
	 * @param callback    Callback function
	 * @param data        User data passed to callback
	 */
	public native void SetConnectCallback(SocketConnectCallback callback, any data = 0);

	/**
	 * Sets incoming connection callback (TCP only)
	 *
	 * @note Required for TCP server mode. Not applicable for UDP.
	 *
	 * @param callback    Callback function
	 * @param data        User data passed to callback
	 */
	public native void SetIncomingCallback(SocketIncomingCallback callback, any data = 0);

	/**
	 * Sets listen ready callback
	 *
	 * @note Called when Listen() completes and socket is ready
	 *
	 * @param callback    Callback function
	 * @param data        User data passed to callback
	 */
	public native void SetListenCallback(SocketListenCallback callback, any data = 0);

	/**
	 * Gets local system hostname
	 *
	 * @param buffer       Destination buffer
	 * @param maxLength    Buffer size
	 * @return             True on success false on failure
	 */
	public static native bool GetHostName(char[] buffer, int maxLength);

	/**
	 * Gets local bound address
	 *
	 * @note Returns the actual bound address after Listen() or Connect()
	 * @note Useful when binding to port 0 to get the assigned port
	 *
	 * @param buffer       Destination buffer
	 * @param maxLength    Buffer size
	 * @return             True on success false on failure
	 */
	public native bool GetLocalAddress(char[] buffer, int maxLength);

	/**
	 * Gets local bound port
	 *
	 * @note Returns the actual bound port after Listen() or Connect()
	 * @note Useful when binding to port 0 to get the assigned port
	 *
	 * @return    Local port number (0 if not bound)
	 */
	public native int GetLocalPort();

	/**
	 * Whether socket is connected
	 */
	property bool Connected {
		public native get();
	}
}

public Extension __ext_socket = {
	name = "Socket",
	file = "socket.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_socket_SetNTVOptional() {
	MarkNativeAsOptional("Socket.Socket");
	MarkNativeAsOptional("Socket.Bind");
	MarkNativeAsOptional("Socket.Connect");
	MarkNativeAsOptional("Socket.Disconnect");
	MarkNativeAsOptional("Socket.CloseReset");
	MarkNativeAsOptional("Socket.Listen");
	MarkNativeAsOptional("Socket.Send");
	MarkNativeAsOptional("Socket.SendTo");
	MarkNativeAsOptional("Socket.SetOption");
	MarkNativeAsOptional("Socket.SetReceiveCallback");
	MarkNativeAsOptional("Socket.SetDisconnectCallback");
	MarkNativeAsOptional("Socket.SetErrorCallback");
	MarkNativeAsOptional("Socket.SetConnectCallback");
	MarkNativeAsOptional("Socket.SetIncomingCallback");
	MarkNativeAsOptional("Socket.SetListenCallback");
	MarkNativeAsOptional("Socket.GetHostName");
	MarkNativeAsOptional("Socket.GetLocalAddress");
	MarkNativeAsOptional("Socket.GetLocalPort");
	MarkNativeAsOptional("Socket.Connected.get");
}
#endif