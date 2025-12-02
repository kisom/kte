/*
 * DiagnosticStore.cc - implementation
 */
#include "DiagnosticStore.h"

#include <algorithm>

namespace kte::lsp {
void
DiagnosticStore::setDiagnostics(const std::string &uri, std::vector<Diagnostic> diagnostics)
{
	diagnostics_[uri] = std::move(diagnostics);
}


const std::vector<Diagnostic> &
DiagnosticStore::getDiagnostics(const std::string &uri) const
{
	auto it = diagnostics_.find(uri);
	static const std::vector<Diagnostic> kEmpty;
	if (it == diagnostics_.end())
		return kEmpty;
	return it->second;
}


std::vector<Diagnostic>
DiagnosticStore::getDiagnosticsAtLine(const std::string &uri, int line) const
{
	std::vector<Diagnostic> out;
	auto it = diagnostics_.find(uri);
	if (it == diagnostics_.end())
		return out;
	out.reserve(it->second.size());
	for (const auto &d: it->second) {
		if (containsLine(d.range, line))
			out.push_back(d);
	}
	return out;
}


std::optional<Diagnostic>
DiagnosticStore::getDiagnosticAtPosition(const std::string &uri, Position pos) const
{
	auto it = diagnostics_.find(uri);
	if (it == diagnostics_.end())
		return std::nullopt;
	for (const auto &d: it->second) {
		if (containsPosition(d.range, pos))
			return d;
	}
	return std::nullopt;
}


void
DiagnosticStore::clear(const std::string &uri)
{
	diagnostics_.erase(uri);
}


void
DiagnosticStore::clearAll()
{
	diagnostics_.clear();
}


int
DiagnosticStore::getErrorCount(const std::string &uri) const
{
	auto it = diagnostics_.find(uri);
	if (it == diagnostics_.end())
		return 0;
	int count = 0;
	for (const auto &d: it->second) {
		if (d.severity == DiagnosticSeverity::Error)
			++count;
	}
	return count;
}


int
DiagnosticStore::getWarningCount(const std::string &uri) const
{
	auto it = diagnostics_.find(uri);
	if (it == diagnostics_.end())
		return 0;
	int count = 0;
	for (const auto &d: it->second) {
		if (d.severity == DiagnosticSeverity::Warning)
			++count;
	}
	return count;
}


bool
DiagnosticStore::containsLine(const Range &r, int line)
{
	return (line > r.start.line || line == r.start.line) &&
	       (line < r.end.line || line == r.end.line);
}


bool
DiagnosticStore::containsPosition(const Range &r, const Position &p)
{
	if (p.line < r.start.line || p.line > r.end.line)
		return false;
	if (r.start.line == r.end.line) {
		return p.line == r.start.line && p.character >= r.start.character && p.character <= r.end.character;
	}
	if (p.line == r.start.line)
		return p.character >= r.start.character;
	if (p.line == r.end.line)
		return p.character <= r.end.character;
	return true; // between start and end lines
}
} // namespace kte::lsp