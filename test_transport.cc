// test_transport.cc - transport framing tests
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <optional>
#include <unistd.h>

#include "lsp/JsonRpcTransport.h"

using namespace kte::lsp;


static void
write_all(int fd, const void *bufv, size_t len)
{
	const char *buf = static_cast<const char *>(bufv);
	size_t left     = len;
	while (left > 0) {
		ssize_t n = ::write(fd, buf, left);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			std::perror("write");
			std::abort();
		}
		buf += static_cast<size_t>(n);
		left -= static_cast<size_t>(n);
	}
}


int
main()
{
	int p[2];
	assert(pipe(p) == 0);
	int readFd  = p[0];
	int writeFd = p[1];

	JsonRpcTransport t;
	// We only need inFd for read tests; pass writeFd for completeness
	t.connect(readFd, writeFd);

	auto sendMsg = [&](const std::string &payload, bool lowerHeader) {
		std::string header = (lowerHeader ? std::string("content-length: ") : std::string("Content-Length: ")) +
		                     std::to_string(payload.size()) + "\r\n\r\n";
		write_all(writeFd, header.data(), header.size());
		// Send body in two parts to exercise partial reads
		size_t mid = payload.size() / 2;
		write_all(writeFd, payload.data(), mid);
		write_all(writeFd, payload.data() + mid, payload.size() - mid);
	};

	std::string p1 = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":null}";
	std::string p2 = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\"}";
	sendMsg(p1, false);
	sendMsg(p2, true); // case-insensitive header

	auto m1 = t.read();
	assert(m1.has_value());
	assert(m1->raw == p1);

	auto m2 = t.read();
	assert(m2.has_value());
	assert(m2->raw == p2);

	// Close write end to signal EOF; next read should return nullopt
	::close(writeFd);
	auto m3 = t.read();
	assert(!m3.has_value());

	::close(readFd);
	std::puts("test_transport: OK");
	return 0;
}