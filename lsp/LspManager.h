/*
 * LspManager.h - central coordination of LSP servers and diagnostics
 */
#ifndef KTE_LSP_MANAGER_H
#define KTE_LSP_MANAGER_H

#include <memory>
#include <string>
#include <unordered_map>

class Buffer; // fwd
class Editor; // fwd

#include "DiagnosticDisplay.h"
#include "DiagnosticStore.h"
#include "LspClient.h"
#include "LspServerConfig.h"

namespace kte::lsp {
class LspManager {
public:
	explicit LspManager(Editor *editor, DiagnosticDisplay *display);

	// Server management
	void registerServer(const std::string &languageId, const LspServerConfig &config);

	bool startServerForBuffer(Buffer *buffer);

	void stopServer(const std::string &languageId);

	void stopAllServers();

	// Document sync (to be called by editor/buffer events)
	void onBufferOpened(Buffer *buffer);

	void onBufferChanged(Buffer *buffer);

	void onBufferClosed(Buffer *buffer);

	void onBufferSaved(Buffer *buffer);

	// Feature requests (stubs)
	void requestCompletion(Buffer *buffer, Position pos, CompletionCallback callback);

	void requestHover(Buffer *buffer, Position pos, HoverCallback callback);

	void requestDefinition(Buffer *buffer, Position pos, LocationCallback callback);

	// Diagnostics (public so LspClient impls can forward results here later)
	void handleDiagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics);


	void setDebugLogging(bool enabled)
	{
		debug_ = enabled;
	}

private:
	[[maybe_unused]] Editor *editor_{}; // non-owning
	DiagnosticDisplay *display_{}; // non-owning
	DiagnosticStore diagnosticStore_{};

	// Key: languageId → client
	std::unordered_map<std::string, std::unique_ptr<LspClient> > servers_;
	std::unordered_map<std::string, LspServerConfig> serverConfigs_;

	// Helpers
	static std::string getLanguageId(Buffer *buffer);

	static std::string getUri(Buffer *buffer);

	static std::string extToLanguageId(const std::string &ext);

	LspClient *ensureServerForLanguage(const std::string &languageId);

	bool debug_ = false;

	// Configuration helpers
	void registerDefaultServers();

	static std::string patternToLanguageId(const std::string &pattern);
};
} // namespace kte::lsp

#endif // KTE_LSP_MANAGER_H