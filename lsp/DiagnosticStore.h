/*
 * DiagnosticStore.h - Central storage for diagnostics by document URI
 */
#ifndef KTE_LSP_DIAGNOSTIC_STORE_H
#define KTE_LSP_DIAGNOSTIC_STORE_H

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Diagnostic.h"

namespace kte::lsp {
class DiagnosticStore {
public:
	void setDiagnostics(const std::string &uri, std::vector<Diagnostic> diagnostics);

	const std::vector<Diagnostic> &getDiagnostics(const std::string &uri) const;

	std::vector<Diagnostic> getDiagnosticsAtLine(const std::string &uri, int line) const;

	std::optional<Diagnostic> getDiagnosticAtPosition(const std::string &uri, Position pos) const;

	void clear(const std::string &uri);

	void clearAll();

	int getErrorCount(const std::string &uri) const;

	int getWarningCount(const std::string &uri) const;

private:
	std::unordered_map<std::string, std::vector<Diagnostic> > diagnostics_;

	static bool containsLine(const Range &r, int line);

	static bool containsPosition(const Range &r, const Position &p);
};
} // namespace kte::lsp

#endif // KTE_LSP_DIAGNOSTIC_STORE_H