/*
 * JsonRpcTransport.h - placeholder transport for JSON-RPC over stdio (stub)
 */
#ifndef KTE_JSON_RPC_TRANSPORT_H
#define KTE_JSON_RPC_TRANSPORT_H

#include <optional>
#include <string>

namespace kte::lsp {
struct JsonRpcMessage {
	std::string raw; // raw JSON payload (stub)
};

class JsonRpcTransport {
public:
	JsonRpcTransport() = default;

	~JsonRpcTransport() = default;

	// Send a method call (request or notification) - stub does nothing
	void send(const std::string &method, const std::string &payload);

	// Blocking read next message (stub => returns nullopt)
	std::optional<JsonRpcMessage> read();
};
} // namespace kte::lsp

#endif // KTE_JSON_RPC_TRANSPORT_H