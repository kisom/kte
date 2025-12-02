/*
 * LspManager.cc - central coordination of LSP servers and diagnostics
 */

#include "LspManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

#include "../Buffer.h"
#include "../Editor.h"
#include "BufferChangeTracker.h"
#include "LspProcessClient.h"

namespace fs = std::filesystem;

namespace kte::lsp {
LspManager::LspManager(Editor *editor, DiagnosticDisplay *display)
	: editor_(editor), display_(display)
{
	// Pre-populate with sensible default server configs
	registerDefaultServers();
}


void
LspManager::registerServer(const std::string &languageId, const LspServerConfig &config)
{
	serverConfigs_[languageId] = config;
}


bool
LspManager::startServerForBuffer(Buffer *buffer)
{
	const auto lang = getLanguageId(buffer);
	if (lang.empty())
		return false;

	if (servers_.find(lang) != servers_.end() && servers_[lang]->isRunning()) {
		return true;
	}

	auto it = serverConfigs_.find(lang);
	if (it == serverConfigs_.end()) {
		return false;
	}

	const auto &cfg = it->second;
	// Respect autostart for automatic starts on buffer open
	if (!cfg.autostart) {
		return false;
	}
	auto client = std::make_unique<LspProcessClient>(cfg.command, cfg.args);
	// Determine root as parent of file for now; future: walk rootPatterns
	std::string rootPath;
	if (!buffer->Filename().empty()) {
		fs::path p(buffer->Filename());
		rootPath = p.has_parent_path() ? p.parent_path().string() : std::string{};
	}
	if (!client->initialize(rootPath)) {
		return false;
	}
	servers_[lang] = std::move(client);
	return true;
}


void
LspManager::stopServer(const std::string &languageId)
{
	auto it = servers_.find(languageId);
	if (it != servers_.end()) {
		it->second->shutdown();
		servers_.erase(it);
	}
}


void
LspManager::stopAllServers()
{
	for (auto &kv: servers_) {
		kv.second->shutdown();
	}
	servers_.clear();
}


void
LspManager::onBufferOpened(Buffer *buffer)
{
	if (!startServerForBuffer(buffer))
		return;
	auto *client = ensureServerForLanguage(getLanguageId(buffer));
	if (!client)
		return;

	const auto uri         = getUri(buffer);
	const auto lang        = getLanguageId(buffer);
	const int version      = static_cast<int>(buffer->Version());
	const std::string text = buffer->FullText();
	client->didOpen(uri, lang, version, text);
}


void
LspManager::onBufferChanged(Buffer *buffer)
{
	auto *client = ensureServerForLanguage(getLanguageId(buffer));
	if (!client)
		return;
	const auto uri = getUri(buffer);
	int version    = static_cast<int>(buffer->Version());

	std::vector<TextDocumentContentChangeEvent> changes;
	if (auto *tracker = buffer->GetChangeTracker()) {
		changes = tracker->getChanges();
		tracker->clearChanges();
		version = tracker->getVersion();
	} else {
		// Fallback: full document change
		TextDocumentContentChangeEvent ev;
		ev.range.reset();
		ev.text = buffer->FullText();
		changes.push_back(std::move(ev));
	}
	client->didChange(uri, version, changes);
}


void
LspManager::onBufferClosed(Buffer *buffer)
{
	auto *client = ensureServerForLanguage(getLanguageId(buffer));
	if (!client)
		return;
	client->didClose(getUri(buffer));
	// Clear diagnostics for this file
	diagnosticStore_.clear(getUri(buffer));
}


void
LspManager::onBufferSaved(Buffer *buffer)
{
	auto *client = ensureServerForLanguage(getLanguageId(buffer));
	if (!client)
		return;
	client->didSave(getUri(buffer));
}


void
LspManager::requestCompletion(Buffer *buffer, Position pos, CompletionCallback callback)
{
	if (auto *client = ensureServerForLanguage(getLanguageId(buffer))) {
		client->completion(getUri(buffer), pos, std::move(callback));
	}
}


void
LspManager::requestHover(Buffer *buffer, Position pos, HoverCallback callback)
{
	if (auto *client = ensureServerForLanguage(getLanguageId(buffer))) {
		client->hover(getUri(buffer), pos, std::move(callback));
	}
}


void
LspManager::requestDefinition(Buffer *buffer, Position pos, LocationCallback callback)
{
	if (auto *client = ensureServerForLanguage(getLanguageId(buffer))) {
		client->definition(getUri(buffer), pos, std::move(callback));
	}
}


void
LspManager::handleDiagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics)
{
	diagnosticStore_.setDiagnostics(uri, diagnostics);
	if (display_) {
		display_->updateDiagnostics(uri, diagnostics);
		display_->updateStatusBar(diagnosticStore_.getErrorCount(uri), diagnosticStore_.getWarningCount(uri));
	}
}


std::string
LspManager::getLanguageId(Buffer *buffer)
{
	// Prefer explicit filetype if set
	const auto &ft = buffer->Filetype();
	if (!ft.empty())
		return ft;
	// Otherwise map extension
	fs::path p(buffer->Filename());
	return extToLanguageId(p.extension().string());
}


std::string
LspManager::getUri(Buffer *buffer)
{
	const auto &path = buffer->Filename();
	if (path.empty()) {
		// Untitled buffer: use a pseudo-URI
		return std::string("untitled:") + std::to_string(reinterpret_cast<std::uintptr_t>(buffer));
	}
	fs::path p(path);
	p = fs::weakly_canonical(p);
#ifdef _WIN32
	// rudimentary file URI; future: robust encoding
	return std::string("file:/") + p.string();
#else
	return std::string("file://") + p.string();
#endif
}


std::string
LspManager::extToLanguageId(const std::string &ext)
{
	std::string e = ext;
	if (!e.empty() && e[0] == '.')
		e.erase(0, 1);
	std::string lower;
	lower.resize(e.size());
	std::transform(e.begin(), e.end(), lower.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	if (lower == "rs")
		return "rust";
	if (lower == "c" || lower == "cc" || lower == "cpp" || lower == "h" || lower == "hpp")
		return "cpp";
	if (lower == "go")
		return "go";
	if (lower == "py")
		return "python";
	if (lower == "js")
		return "javascript";
	if (lower == "ts")
		return "typescript";
	if (lower == "json")
		return "json";
	if (lower == "sh" || lower == "bash" || lower == "zsh")
		return "shell";
	if (lower == "md")
		return "markdown";
	return lower; // best-effort
}


LspClient *
LspManager::ensureServerForLanguage(const std::string &languageId)
{
	auto it = servers_.find(languageId);
	if (it != servers_.end() && it->second && it->second->isRunning()) {
		return it->second.get();
	}
	// Attempt to start from config if present
	auto cfg = serverConfigs_.find(languageId);
	if (cfg == serverConfigs_.end())
		return nullptr;
	auto client = std::make_unique<LspProcessClient>(cfg->second.command, cfg->second.args);
	if (!client->initialize(""))
		return nullptr;
	auto *ret            = client.get();
	servers_[languageId] = std::move(client);
	return ret;
}


void
LspManager::registerDefaultServers()
{
	// Import defaults and register by inferred languageId from file patterns
	for (const auto &cfg: GetDefaultServerConfigs()) {
		if (cfg.filePatterns.empty()) {
			// If no patterns, we can't infer; skip
			continue;
		}
		for (const auto &pat: cfg.filePatterns) {
			const auto lang = patternToLanguageId(pat);
			if (lang.empty())
				continue;
			// Don't overwrite if user already registered a server for this lang
			if (serverConfigs_.find(lang) == serverConfigs_.end()) {
				serverConfigs_.emplace(lang, cfg);
			}
		}
	}
}


std::string
LspManager::patternToLanguageId(const std::string &pattern)
{
	// Expect patterns like "*.rs", "*.cpp" etc. Extract extension and reuse extToLanguageId
	// Find last '.' in the pattern and take substring after it, stripping any trailing wildcards
	std::string ext;
	// Common case: starts with *.
	auto pos = pattern.rfind('.');
	if (pos != std::string::npos && pos + 1 < pattern.size()) {
		ext = pattern.substr(pos + 1);
		// Remove any trailing wildcard characters
		while (!ext.empty() && (ext.back() == '*' || ext.back() == '?')) {
			ext.pop_back();
		}
	} else {
		// No dot; try to treat whole pattern as extension after trimming leading '*'
		ext = pattern;
		while (!ext.empty() && (ext.front() == '*' || ext.front() == '.')) {
			ext.erase(ext.begin());
		}
	}
	if (ext.empty())
		return {};
	return extToLanguageId(ext);
}
} // namespace kte::lsp
