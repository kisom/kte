/*
 * LspManager.cc - central coordination of LSP servers and diagnostics
 */

#include "LspManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstdarg>

#include "../Buffer.h"
#include "../Editor.h"
#include "BufferChangeTracker.h"
#include "LspProcessClient.h"
#include "UtfCodec.h"

namespace fs = std::filesystem;

namespace kte::lsp {
static void
lsp_debug_file(const char *fmt, ...)
{
	FILE *f = std::fopen("/tmp/kte-lsp.log", "a");
	if (!f)
		return;
	// prepend timestamp
	std::time_t t = std::time(nullptr);
	char ts[32];
	std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
	std::fprintf(f, "[%s] ", ts);
	va_list ap;
	va_start(ap, fmt);
	std::vfprintf(f, fmt, ap);
	va_end(ap);
	std::fputc('\n', f);
	std::fclose(f);
}


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
	// Allow env override of server path
	std::string command = cfg.command;
	if (lang == "cpp") {
		if (const char *p = std::getenv("KTE_LSP_CLANGD"); p && *p)
			command   = p;
	} else if (lang == "go") {
		if (const char *p = std::getenv("KTE_LSP_GOPLS"); p && *p)
			command   = p;
	} else if (lang == "rust") {
		if (const char *p = std::getenv("KTE_LSP_RUST_ANALYZER"); p && *p)
			command   = p;
	}
	if (debug_) {
		std::fprintf(stderr, "[kte][lsp] startServerForBuffer: lang=%s cmd=%s args=%zu file=%s\n",
		             lang.c_str(), command.c_str(), cfg.args.size(), buffer->Filename().c_str());
		lsp_debug_file("startServerForBuffer: lang=%s cmd=%s args=%zu file=%s",
		               lang.c_str(), command.c_str(), cfg.args.size(), buffer->Filename().c_str());
	}
	auto client = std::make_unique<LspProcessClient>(command, cfg.args);
	// Wire diagnostics handler to manager
	client->setDiagnosticsHandler([this](const std::string &uri, const std::vector<Diagnostic> &diags) {
		this->handleDiagnostics(uri, diags);
	});
	// Determine workspace root using rootPatterns if set; fallback to file's parent
	std::string rootPath;
	if (!buffer->Filename().empty()) {
		rootPath = detectWorkspaceRoot(buffer->Filename(), cfg);
		if (rootPath.empty()) {
			fs::path p(buffer->Filename());
			rootPath = p.has_parent_path() ? p.parent_path().string() : std::string{};
		}
	}
	if (debug_) {
		const char *pathEnv = std::getenv("PATH");
		std::fprintf(stderr, "[kte][lsp] initializing server: rootPath=%s PATH=%s\n",
		             rootPath.c_str(), pathEnv ? pathEnv : "<null>");
		lsp_debug_file("initializing server: rootPath=%s PATH=%s",
		               rootPath.c_str(), pathEnv ? pathEnv : "<null>");
	}
	if (!client->initialize(rootPath)) {
		if (debug_) {
			std::fprintf(stderr, "[kte][lsp] initialize failed for lang=%s\n", lang.c_str());
			lsp_debug_file("initialize failed for lang=%s", lang.c_str());
		}
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


bool
LspManager::startServerForLanguage(const std::string &languageId, const std::string &rootPath)
{
	auto cfgIt = serverConfigs_.find(languageId);
	if (cfgIt == serverConfigs_.end())
		return false;

	// If already running, nothing to do
	auto it = servers_.find(languageId);
	if (it != servers_.end() && it->second && it->second->isRunning()) {
		return true;
	}

	const auto &cfg     = cfgIt->second;
	std::string command = cfg.command;
	if (languageId == "cpp") {
		if (const char *p = std::getenv("KTE_LSP_CLANGD"); p && *p)
			command   = p;
	} else if (languageId == "go") {
		if (const char *p = std::getenv("KTE_LSP_GOPLS"); p && *p)
			command   = p;
	} else if (languageId == "rust") {
		if (const char *p = std::getenv("KTE_LSP_RUST_ANALYZER"); p && *p)
			command   = p;
	}
	if (debug_) {
		std::fprintf(stderr, "[kte][lsp] startServerForLanguage: lang=%s cmd=%s args=%zu root=%s\n",
		             languageId.c_str(), command.c_str(), cfg.args.size(), rootPath.c_str());
		lsp_debug_file("startServerForLanguage: lang=%s cmd=%s args=%zu root=%s",
		               languageId.c_str(), command.c_str(), cfg.args.size(), rootPath.c_str());
	}
	auto client = std::make_unique<LspProcessClient>(command, cfg.args);
	client->setDiagnosticsHandler([this](const std::string &uri, const std::vector<Diagnostic> &diags) {
		this->handleDiagnostics(uri, diags);
	});
	std::string root = rootPath;
	if (!root.empty()) {
		// keep
	} else {
		// Try cwd if not provided
		root = std::string();
	}
	if (!client->initialize(root)) {
		if (debug_) {
			std::fprintf(stderr, "[kte][lsp] initialize failed for lang=%s\n", languageId.c_str());
			lsp_debug_file("initialize failed for lang=%s", languageId.c_str());
		}
		return false;
	}
	servers_[languageId] = std::move(client);
	return true;
}


bool
LspManager::restartServer(const std::string &languageId, const std::string &rootPath)
{
	stopServer(languageId);
	return startServerForLanguage(languageId, rootPath);
}


void
LspManager::onBufferOpened(Buffer *buffer)
{
	if (debug_) {
		std::fprintf(stderr, "[kte][lsp] onBufferOpened: file=%s lang=%s\n",
		             buffer->Filename().c_str(), getLanguageId(buffer).c_str());
		lsp_debug_file("onBufferOpened: file=%s lang=%s",
		               buffer->Filename().c_str(), getLanguageId(buffer).c_str());
	}
	if (!startServerForBuffer(buffer)) {
		if (debug_) {
			std::fprintf(stderr, "[kte][lsp] onBufferOpened: server did not start\n");
			lsp_debug_file("onBufferOpened: server did not start");
		}
		return;
	}
	auto *client = ensureServerForLanguage(getLanguageId(buffer));
	if (!client)
		return;

	const auto uri         = getUri(buffer);
	const auto lang        = getLanguageId(buffer);
	const int version      = static_cast<int>(buffer->Version());
	const std::string text = buffer->FullText();
	if (debug_) {
		std::fprintf(stderr, "[kte][lsp] didOpen: uri=%s lang=%s version=%d bytes=%zu\n",
		             uri.c_str(), lang.c_str(), version, text.size());
		lsp_debug_file("didOpen: uri=%s lang=%s version=%d bytes=%zu",
		               uri.c_str(), lang.c_str(), version, text.size());
	}
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

	// Option A: convert ranges from UTF-8 (editor coords) -> UTF-16 (LSP wire)
	std::vector<TextDocumentContentChangeEvent> changes16;
	changes16.reserve(changes.size());
	// LineProvider that serves lines from this buffer by URI
	Buffer *bufForUri = buffer; // changes are for this buffer
	auto provider     = [bufForUri](const std::string &/*u*/, int line) -> std::string_view {
		if (!bufForUri)
			return std::string_view();
		const auto &rows = bufForUri->Rows();
		if (line < 0 || static_cast<size_t>(line) >= rows.size())
			return std::string_view();
		// Materialize one line into a thread_local scratch; return view
		thread_local std::string scratch;
		scratch = static_cast<std::string>(rows[static_cast<size_t>(line)]);
		return std::string_view(scratch);
	};
	for (const auto &ch: changes) {
		TextDocumentContentChangeEvent out = ch;
		if (ch.range.has_value()) {
			Range r16 = toUtf16(uri, *ch.range, provider);
			if (debug_) {
				lsp_debug_file("didChange range convert: L%d C%d-%d -> L%d C%d-%d",
				               ch.range->start.line, ch.range->start.character,
				               ch.range->end.character,
				               r16.start.line, r16.start.character, r16.end.character);
			}
			out.range = r16;
		}
		changes16.push_back(std::move(out));
	}
	client->didChange(uri, version, changes16);
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
		const auto uri = getUri(buffer);
		// Convert position to UTF-16 using Option A provider
		auto provider = [buffer](const std::string &/*u*/, int line) -> std::string_view {
			if (!buffer)
				return std::string_view();
			const auto &rows = buffer->Rows();
			if (line < 0 || static_cast<size_t>(line) >= rows.size())
				return std::string_view();
			thread_local std::string scratch;
			scratch = static_cast<std::string>(rows[static_cast<size_t>(line)]);
			return std::string_view(scratch);
		};
		Position p16 = toUtf16(uri, pos, provider);
		if (debug_) {
			lsp_debug_file("completion pos convert: L%d C%d -> L%d C%d", pos.line, pos.character, p16.line,
			               p16.character);
		}
		client->completion(uri, p16, std::move(callback));
	}
}


void
LspManager::requestHover(Buffer *buffer, Position pos, HoverCallback callback)
{
	if (auto *client = ensureServerForLanguage(getLanguageId(buffer))) {
		const auto uri = getUri(buffer);
		auto provider  = [buffer](const std::string &/*u*/, int line) -> std::string_view {
			if (!buffer)
				return std::string_view();
			const auto &rows = buffer->Rows();
			if (line < 0 || static_cast<size_t>(line) >= rows.size())
				return std::string_view();
			thread_local std::string scratch;
			scratch = static_cast<std::string>(rows[static_cast<size_t>(line)]);
			return std::string_view(scratch);
		};
		Position p16 = toUtf16(uri, pos, provider);
		if (debug_) {
			lsp_debug_file("hover pos convert: L%d C%d -> L%d C%d", pos.line, pos.character, p16.line,
			               p16.character);
		}
		// Wrap the callback to convert any returned range from UTF-16 (wire) -> UTF-8 (editor)
		HoverCallback wrapped = [this, uri, provider, cb = std::move(callback)](const HoverResult &res16,
			const std::string &err) {
			if (!cb)
				return;
			if (!res16.range.has_value()) {
				cb(res16, err);
				return;
			}
			HoverResult res8 = res16;
			res8.range       = toUtf8(uri, *res16.range, provider);
			if (debug_) {
				const auto &r16 = *res16.range;
				const auto &r8  = *res8.range;
				lsp_debug_file("hover range convert: L%d %d-%d -> L%d %d-%d",
				               r16.start.line, r16.start.character, r16.end.character,
				               r8.start.line, r8.start.character, r8.end.character);
			}
			cb(res8, err);
		};
		client->hover(uri, p16, std::move(wrapped));
	}
}


void
LspManager::requestDefinition(Buffer *buffer, Position pos, LocationCallback callback)
{
	if (auto *client = ensureServerForLanguage(getLanguageId(buffer))) {
		const auto uri = getUri(buffer);
		auto provider  = [buffer](const std::string &/*u*/, int line) -> std::string_view {
			if (!buffer)
				return std::string_view();
			const auto &rows = buffer->Rows();
			if (line < 0 || static_cast<size_t>(line) >= rows.size())
				return std::string_view();
			thread_local std::string scratch;
			scratch = static_cast<std::string>(rows[static_cast<size_t>(line)]);
			return std::string_view(scratch);
		};
		Position p16 = toUtf16(uri, pos, provider);
		if (debug_) {
			lsp_debug_file("definition pos convert: L%d C%d -> L%d C%d", pos.line, pos.character, p16.line,
			               p16.character);
		}
		// Wrap callback to convert Location ranges from UTF-16 (wire) -> UTF-8 (editor)
		LocationCallback wrapped = [this, uri, provider, cb = std::move(callback)](
			const std::vector<Location> &locs16,
			const std::string &err) {
			if (!cb)
				return;
			std::vector<Location> locs8;
			locs8.reserve(locs16.size());
			for (const auto &l: locs16) {
				Location x = l;
				x.range    = toUtf8(uri, l.range, provider);
				if (debug_) {
					lsp_debug_file("definition range convert: L%d %d-%d -> L%d %d-%d",
					               l.range.start.line, l.range.start.character,
					               l.range.end.character,
					               x.range.start.line, x.range.start.character,
					               x.range.end.character);
				}
				locs8.push_back(std::move(x));
			}
			cb(locs8, err);
		};
		client->definition(uri, p16, std::move(wrapped));
	}
}


void
LspManager::handleDiagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics)
{
	// Convert incoming ranges from UTF-16 (wire) -> UTF-8 (editor)
	std::vector<Diagnostic> conv = diagnostics;
	Buffer *buf                  = findBufferByUri(uri);
	auto provider                = [buf](const std::string &/*u*/, int line) -> std::string_view {
		if (!buf)
			return std::string_view();
		const auto &rows = buf->Rows();
		if (line < 0 || static_cast<size_t>(line) >= rows.size())
			return std::string_view();
		thread_local std::string scratch;
		scratch = static_cast<std::string>(rows[static_cast<size_t>(line)]);
		return std::string_view(scratch);
	};
	for (auto &d: conv) {
		Range r8 = toUtf8(uri, d.range, provider);
		if (debug_) {
			lsp_debug_file("diagnostic range convert: L%d C%d-%d -> L%d C%d-%d",
			               d.range.start.line, d.range.start.character, d.range.end.character,
			               r8.start.line, r8.start.character, r8.end.character);
		}
		d.range = r8;
	}
	diagnosticStore_.setDiagnostics(uri, conv);
	if (display_) {
		display_->updateDiagnostics(uri, conv);
		display_->updateStatusBar(diagnosticStore_.getErrorCount(uri), diagnosticStore_.getWarningCount(uri));
	}
}


bool
LspManager::toggleAutostart(const std::string &languageId)
{
	auto it = serverConfigs_.find(languageId);
	if (it == serverConfigs_.end())
		return false;
	it->second.autostart = !it->second.autostart;
	return it->second.autostart;
}


std::vector<std::string>
LspManager::configuredLanguages() const
{
	std::vector<std::string> out;
	out.reserve(serverConfigs_.size());
	for (const auto &kv: serverConfigs_)
		out.push_back(kv.first);
	std::sort(out.begin(), out.end());
	return out;
}


std::vector<std::string>
LspManager::runningLanguages() const
{
	std::vector<std::string> out;
	for (const auto &kv: servers_) {
		if (kv.second && kv.second->isRunning())
			out.push_back(kv.first);
	}
	std::sort(out.begin(), out.end());
	return out;
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


// Resolve a Buffer* by matching constructed file URI
Buffer *
LspManager::findBufferByUri(const std::string &uri)
{
	if (!editor_)
		return nullptr;
	// Compare against getUri for each buffer
	auto &bufs = editor_->Buffers();
	for (auto &b: bufs) {
		if (getUri(&b) == uri)
			return &b;
	}
	return nullptr;
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
	client->setDiagnosticsHandler([this](const std::string &uri, const std::vector<Diagnostic> &diags) {
		this->handleDiagnostics(uri, diags);
	});
	// No specific file context here; initialize with empty or current working dir
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


// Detect workspace root by walking up from filePath looking for any of the
// configured rootPatterns (simple filenames). Supports comma/semicolon-separated
// patterns in cfg.rootPatterns.
std::string
LspManager::detectWorkspaceRoot(const std::string &filePath, const LspServerConfig &cfg)
{
	if (filePath.empty())
		return {};
	fs::path start(filePath);
	fs::path dir = start.has_parent_path() ? start.parent_path() : start;

	// Build cache key
	const std::string cacheKey = (dir.string() + "|" + cfg.rootPatterns);
	auto it                    = rootCache_.find(cacheKey);
	if (it != rootCache_.end()) {
		return it->second;
	}

	// Split patterns by ',', ';', or ':'
	std::vector<std::string> pats;
	{
		std::string acc;
		for (char c: cfg.rootPatterns) {
			if (c == ',' || c == ';' || c == ':') {
				if (!acc.empty()) {
					pats.push_back(acc);
					acc.clear();
				}
			} else if (!std::isspace(static_cast<unsigned char>(c))) {
				acc.push_back(c);
			}
		}
		if (!acc.empty())
			pats.push_back(acc);
	}
	// If no patterns defined, cache empty and return {}
	if (pats.empty()) {
		rootCache_[cacheKey] = {};
		return {};
	}

	fs::path cur = dir;
	while (true) {
		// Check each pattern in this directory
		for (const auto &pat: pats) {
			if (pat.empty())
				continue;
			fs::path candidate = cur / pat;
			std::error_code ec;
			bool exists = fs::exists(candidate, ec);
			if (!ec && exists) {
				rootCache_[cacheKey] = cur.string();
				return rootCache_[cacheKey];
			}
		}
		if (cur.has_parent_path()) {
			fs::path parent = cur.parent_path();
			if (parent == cur)
				break; // reached root guard
			cur = parent;
		} else {
			break;
		}
	}
	rootCache_[cacheKey] = {};
	return {};
}
} // namespace kte::lsp