/*
 * TerminalDiagnosticDisplay.cc - minimal stub implementation
 */
#include "TerminalDiagnosticDisplay.h"

#include "../TerminalRenderer.h"

namespace kte::lsp {
TerminalDiagnosticDisplay::TerminalDiagnosticDisplay(TerminalRenderer *renderer)
	: renderer_(renderer) {}


void
TerminalDiagnosticDisplay::updateDiagnostics(const std::string &uri,
                                             const std::vector<Diagnostic> &diagnostics)
{
	(void) uri;
	(void) diagnostics;
	// Stub: no rendering yet. Future: gutter markers, underlines, virtual text.
}


void
TerminalDiagnosticDisplay::showInlineDiagnostic(const Diagnostic &diagnostic)
{
	(void) diagnostic;
	// Stub: show as message line in future.
}


void
TerminalDiagnosticDisplay::showDiagnosticList(const std::vector<Diagnostic> &diagnostics)
{
	(void) diagnostics;
	// Stub: open a panel/list in future.
}


void
TerminalDiagnosticDisplay::hideDiagnosticList()
{
	// Stub
}


void
TerminalDiagnosticDisplay::updateStatusBar(int errorCount, int warningCount)
{
	(void) errorCount;
	(void) warningCount;
	// Stub: integrate with status bar rendering later.
}
} // namespace kte::lsp