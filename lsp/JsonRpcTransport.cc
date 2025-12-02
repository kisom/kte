/*
 * JsonRpcTransport.cc - minimal stdio JSON-RPC framing (Content-Length)
 */
#include "JsonRpcTransport.h"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <optional>
#include <unistd.h>

namespace kte::lsp {
void
JsonRpcTransport::connect(int inFd, int outFd)
{
	inFd_  = inFd;
	outFd_ = outFd;
}


void
JsonRpcTransport::send(const std::string &/*method*/, const std::string &payload)
{
	if (outFd_ < 0)
		return;
	const std::string header = "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
	std::lock_guard<std::mutex> lk(writeMutex_);
	// write header
	const char *hbuf = header.data();
	size_t hleft     = header.size();
	while (hleft > 0) {
		ssize_t n = ::write(outFd_, hbuf, hleft);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return;
		}
		hbuf += static_cast<size_t>(n);
		hleft -= static_cast<size_t>(n);
	}
	// write payload
	const char *pbuf = payload.data();
	size_t pleft     = payload.size();
	while (pleft > 0) {
		ssize_t n = ::write(outFd_, pbuf, pleft);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return;
		}
		pbuf += static_cast<size_t>(n);
		pleft -= static_cast<size_t>(n);
	}
}


static bool
readLineCrlf(int fd, std::string &out, size_t maxLen)
{
	out.clear();
	char ch;
	while (true) {
		ssize_t n = ::read(fd, &ch, 1);
		if (n == 0)
			return false; // EOF
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		out.push_back(ch);
		// Handle CRLF or bare LF as end-of-line
		if ((out.size() >= 2 && out[out.size() - 2] == '\r' && out[out.size() - 1] == '\n') ||
		    (out.size() >= 1 && out[out.size() - 1] == '\n')) {
			return true;
		}
		if (out.size() > maxLen) {
			// sanity cap
			return false;
		}
	}
}


std::optional<JsonRpcMessage>
JsonRpcTransport::read()
{
	if (inFd_ < 0)
		return std::nullopt;
	// Parse headers (case-insensitive), accept/ignore extras
	size_t contentLength = 0;
	while (true) {
		std::string line;
		if (!readLineCrlf(inFd_, line, kMaxHeaderLine))
			return std::nullopt;
		// Normalize end-of-line handling: consider blank line as end of headers
		if (line == "\r\n" || line == "\n" || line == "\r")
			break;
		// Trim trailing CRLF
		if (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
			while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
				line.pop_back();
		}
		// Find colon
		auto pos = line.find(':');
		if (pos == std::string::npos)
			continue;
		std::string name  = line.substr(0, pos);
		std::string value = line.substr(pos + 1);
		// trim leading spaces in value
		size_t i = 0;
		while (i < value.size() && (value[i] == ' ' || value[i] == '\t'))
			++i;
		value.erase(0, i);
		// lower-case name for comparison
		for (auto &c: name)
			c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
		if (name == "content-length") {
			size_t len = static_cast<size_t>(std::strtoull(value.c_str(), nullptr, 10));
			if (len > kMaxBody) {
				return std::nullopt; // drop too-large message
			}
			contentLength = len;
		}
		// else: ignore other headers
	}
	if (contentLength == 0)
		return std::nullopt;
	std::string body;
	body.resize(contentLength);
	size_t readTotal = 0;
	while (readTotal < contentLength) {
		ssize_t n = ::read(inFd_, &body[readTotal], contentLength - readTotal);
		if (n == 0)
			return std::nullopt;
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return std::nullopt;
		}
		readTotal += static_cast<size_t>(n);
	}
	return JsonRpcMessage{std::move(body)};
}
} // namespace kte::lsp