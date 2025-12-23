#include "socket/SocketTypes.h"

RemoteEndpoint ExtractEndpoint(const sockaddr* addr) {
	RemoteEndpoint endpoint;
	if (!addr) return endpoint;

	if (addr->sa_family == AF_INET) {
		auto* ipv4 = reinterpret_cast<const sockaddr_in*>(addr);
		char ipStr[INET_ADDRSTRLEN];
		uv_ip4_name(ipv4, ipStr, sizeof(ipStr));
		endpoint.address = ipStr;
		endpoint.port = ntohs(ipv4->sin_port);
	} else if (addr->sa_family == AF_INET6) {
		auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(addr);
		char ipStr[INET6_ADDRSTRLEN];
		uv_ip6_name(ipv6, ipStr, sizeof(ipStr));
		endpoint.address = ipStr;
		endpoint.port = ntohs(ipv6->sin6_port);
	}
	return endpoint;
}