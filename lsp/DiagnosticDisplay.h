/*
 * DiagnosticDisplay.h - Abstract interface for showing diagnostics
 */
#ifndef KTE_LSP_DIAGNOSTIC_DISPLAY_H
#define KTE_LSP_DIAGNOSTIC_DISPLAY_H

#include <string>
#include <vector>

#include "Diagnostic.h"

namespace kte::lsp {
class DiagnosticDisplay {
public:
	virtual ~DiagnosticDisplay() = default;

	virtual void updateDiagnostics(const std::string &uri,
	                               const std::vector<Diagnostic> &diagnostics) = 0;

	virtual void showInlineDiagnostic(const Diagnostic &diagnostic) = 0;

	virtual void showDiagnosticList(const std::vector<Diagnostic> &diagnostics) = 0;

	virtual void hideDiagnosticList() = 0;

	virtual void updateStatusBar(int errorCount, int warningCount) = 0;
};
} // namespace kte::lsp

#endif // KTE_LSP_DIAGNOSTIC_DISPLAY_H