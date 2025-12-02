/*
 * LspProcessClient.h - process-based LSP client (initial stub)
 */
#ifndef KTE_LSP_PROCESS_CLIENT_H
#define KTE_LSP_PROCESS_CLIENT_H

#include <memory>
#include <string>
#include <vector>

#include "LspClient.h"
#include "JsonRpcTransport.h"

namespace kte::lsp {
class LspProcessClient : public LspClient {
public:
	LspProcessClient(std::string serverCommand, std::vector<std::string> serverArgs);

	~LspProcessClient() override;

	bool initialize(const std::string &rootPath) override;

	void shutdown() override;

	void didOpen(const std::string &uri, const std::string &languageId,
	             int version, const std::string &text) override;

	void didChange(const std::string &uri, int version,
	               const std::vector<TextDocumentContentChangeEvent> &changes) override;

	void didClose(const std::string &uri) override;

	void didSave(const std::string &uri) override;

	bool isRunning() const override;

	std::string getServerName() const override;

private:
	std::string command_;
	std::vector<std::string> args_;
	std::unique_ptr<JsonRpcTransport> transport_;
	bool running_ = false;
};
} // namespace kte::lsp

#endif // KTE_LSP_PROCESS_CLIENT_H