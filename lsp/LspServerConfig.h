/*
 * LspServerConfig.h - per-language LSP server configuration
 */
#ifndef KTE_LSP_SERVER_CONFIG_H
#define KTE_LSP_SERVER_CONFIG_H

#include <string>
#include <unordered_map>
#include <vector>

namespace kte::lsp {
enum class LspSyncMode {
	None = 0,
	Full = 1,
	Incremental = 2,
};

struct LspServerConfig {
	std::string command; // executable name/path
	std::vector<std::string> args; // CLI args
	std::vector<std::string> filePatterns; // e.g. {"*.rs"}
	std::string rootPatterns; // e.g. "Cargo.toml"
	LspSyncMode preferredSyncMode = LspSyncMode::Incremental;
	bool autostart                = true;
	std::unordered_map<std::string, std::string> initializationOptions; // placeholder
	std::unordered_map<std::string, std::string> settings; // placeholder
};

// Provide a small set of defaults; callers may ignore
inline std::vector<LspServerConfig>
GetDefaultServerConfigs()
{
	return std::vector<LspServerConfig>{
		LspServerConfig{
			.command = "rust-analyzer", .args = {}, .filePatterns = {"*.rs"}, .rootPatterns = "Cargo.toml"
		},
		LspServerConfig{
			.command = "clangd", .args = {"--background-index"},
			.filePatterns = {"*.c", "*.cc", "*.cpp", "*.h", "*.hpp"},
			.rootPatterns = "compile_commands.json"
		},
		LspServerConfig{.command = "gopls", .args = {}, .filePatterns = {"*.go"}, .rootPatterns = "go.mod"},
	};
}
} // namespace kte::lsp

#endif // KTE_LSP_SERVER_CONFIG_H