/*
 * JsonRpcTransport.h - minimal JSON-RPC over stdio transport
 */
#ifndef KTE_JSON_RPC_TRANSPORT_H
#define KTE_JSON_RPC_TRANSPORT_H

#include <optional>
#include <string>
#include <mutex>

namespace kte::lsp {
struct JsonRpcMessage {
	std::string raw; // raw JSON payload (stub)
};

class JsonRpcTransport {
public:
	JsonRpcTransport() = default;

	~JsonRpcTransport() = default;

	// Connect this transport to file descriptors (read from inFd, write to outFd)
	void connect(int inFd, int outFd);

	// Send a method call (request or notification)
	// 'payload' should be a complete JSON object string to send as the message body.
	void send(const std::string &method, const std::string &payload);

	// Blocking read next message; returns nullopt on EOF or error
	std::optional<JsonRpcMessage> read();

private:
	int inFd_  = -1;
	int outFd_ = -1;
	std::mutex writeMutex_;

	// Limits to keep the transport resilient
	static constexpr size_t kMaxHeaderLine = 16 * 1024; // 16 KiB per header line
	static constexpr size_t kMaxBody       = 64ull * 1024ull * 1024ull; // 64 MiB body cap
};
} // namespace kte::lsp

#endif // KTE_JSON_RPC_TRANSPORT_H