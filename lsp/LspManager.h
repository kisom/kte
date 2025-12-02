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
#include "UtfCodec.h"

namespace kte::lsp {
class LspManager {
public:
	explicit LspManager(Editor *editor, DiagnosticDisplay *display);

	// Server management
	void registerServer(const std::string &languageId, const LspServerConfig &config);

	bool startServerForBuffer(Buffer *buffer);

	void stopServer(const std::string &languageId);

	void stopAllServers();

	// Manual lifecycle controls
	bool startServerForLanguage(const std::string &languageId, const std::string &rootPath = std::string());

	bool restartServer(const std::string &languageId, const std::string &rootPath = std::string());

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


	// Configuration utilities
	bool toggleAutostart(const std::string &languageId);

	std::vector<std::string> configuredLanguages() const;

	std::vector<std::string> runningLanguages() const;

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

	// Workspace root detection helpers/cache
	std::string detectWorkspaceRoot(const std::string &filePath, const LspServerConfig &cfg);

	// key = startDir + "|" + cfg.rootPatterns
	std::unordered_map<std::string, std::string> rootCache_;

	// Resolve a buffer by its file:// (or untitled:) URI
	Buffer *findBufferByUri(const std::string &uri);
};
} // namespace kte::lsp

#endif // KTE_LSP_MANAGER_H