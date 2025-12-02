/*
 * JsonRpcTransport.cc - placeholder
 */
#include "JsonRpcTransport.h"

namespace kte::lsp {
void
JsonRpcTransport::send(const std::string &/*method*/, const std::string &/*payload*/)
{
	// stub: no-op
}


std::optional<JsonRpcMessage>
JsonRpcTransport::read()
{
	return std::nullopt; // stub
}
} // namespace kte::lsp