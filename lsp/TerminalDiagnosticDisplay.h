/*
 * TerminalDiagnosticDisplay.h - Terminal (ncurses) diagnostics visualization stub
 */
#ifndef KTE_LSP_TERMINAL_DIAGNOSTIC_DISPLAY_H
#define KTE_LSP_TERMINAL_DIAGNOSTIC_DISPLAY_H

#include <string>
#include <vector>

#include "DiagnosticDisplay.h"

class TerminalRenderer; // fwd

namespace kte::lsp {
class TerminalDiagnosticDisplay final : public DiagnosticDisplay {
public:
	explicit TerminalDiagnosticDisplay(TerminalRenderer *renderer);

	void updateDiagnostics(const std::string &uri,
	                       const std::vector<Diagnostic> &diagnostics) override;

	void showInlineDiagnostic(const Diagnostic &diagnostic) override;

	void showDiagnosticList(const std::vector<Diagnostic> &diagnostics) override;

	void hideDiagnosticList() override;

	void updateStatusBar(int errorCount, int warningCount) override;

private:
	[[maybe_unused]] TerminalRenderer *renderer_{}; // non-owning
};
} // namespace kte::lsp

#endif // KTE_LSP_TERMINAL_DIAGNOSTIC_DISPLAY_H