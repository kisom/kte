/*
 * LspProcessClient.cc - initial stub implementation
 */
#include "LspProcessClient.h"

namespace kte::lsp {
LspProcessClient::LspProcessClient(std::string serverCommand, std::vector<std::string> serverArgs)
	: command_(std::move(serverCommand)), args_(std::move(serverArgs)), transport_(new JsonRpcTransport()) {}


LspProcessClient::~LspProcessClient() = default;


bool
LspProcessClient::initialize(const std::string &/*rootPath*/)
{
	// Phase 1–2: no real process spawn yet
	running_ = true;
	return true;
}


void
LspProcessClient::shutdown()
{
	running_ = false;
}


void
LspProcessClient::didOpen(const std::string &/*uri*/, const std::string &/*languageId*/,
                          int /*version*/, const std::string &/*text*/)
{
	// Stub: would send textDocument/didOpen
}


void
LspProcessClient::didChange(const std::string &/*uri*/, int /*version*/,
                            const std::vector<TextDocumentContentChangeEvent> &/*changes*/)
{
	// Stub: would send textDocument/didChange
}


void
LspProcessClient::didClose(const std::string &/*uri*/)
{
	// Stub
}


void
LspProcessClient::didSave(const std::string &/*uri*/)
{
	// Stub
}


bool
LspProcessClient::isRunning() const
{
	return running_;
}


std::string
LspProcessClient::getServerName() const
{
	return command_;
}
} // namespace kte::lsp