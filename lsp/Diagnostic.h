/*
 * Diagnostic.h - LSP diagnostic data types
 */
#ifndef KTE_LSP_DIAGNOSTIC_H
#define KTE_LSP_DIAGNOSTIC_H

#include <optional>
#include <string>
#include <vector>

#include "LspTypes.h"

namespace kte::lsp {
enum class DiagnosticSeverity {
	Error = 1,
	Warning = 2,
	Information = 3,
	Hint = 4
};

struct DiagnosticRelatedInformation {
	std::string uri; // related location URI
	Range range; // related range
	std::string message;
};

struct Diagnostic {
	Range range{};
	DiagnosticSeverity severity{DiagnosticSeverity::Information};
	std::optional<std::string> code;
	std::optional<std::string> source;
	std::string message;
	std::vector<DiagnosticRelatedInformation> relatedInfo;
};
} // namespace kte::lsp

#endif // KTE_LSP_DIAGNOSTIC_H