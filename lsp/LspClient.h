/*
 * LspClient.h - Core LSP client abstraction (initial stub)
 */
#ifndef KTE_LSP_CLIENT_H
#define KTE_LSP_CLIENT_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "LspTypes.h"

namespace kte::lsp {
// Callback types (stubs for future phases)
using CompletionCallback = std::function<void()>;
using HoverCallback      = std::function<void()>;
using LocationCallback   = std::function<void()>;

class LspClient {
public:
	virtual ~LspClient() = default;

	// Lifecycle
	virtual bool initialize(const std::string &rootPath) = 0;

	virtual void shutdown() = 0;

	// Document Synchronization
	virtual void didOpen(const std::string &uri, const std::string &languageId,
	                     int version, const std::string &text) = 0;

	virtual void didChange(const std::string &uri, int version,
	                       const std::vector<TextDocumentContentChangeEvent> &changes) = 0;

	virtual void didClose(const std::string &uri) = 0;

	virtual void didSave(const std::string &uri) = 0;

	// Language Features (not yet implemented)
	virtual void completion(const std::string &, Position,
	                        CompletionCallback) {}


	virtual void hover(const std::string &, Position,
	                   HoverCallback) {}


	virtual void definition(const std::string &, Position,
	                        LocationCallback) {}


	// Process Management
	virtual bool isRunning() const = 0;

	virtual std::string getServerName() const = 0;
};
} // namespace kte::lsp

#endif // KTE_LSP_CLIENT_H