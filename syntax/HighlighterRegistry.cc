#include "HighlighterRegistry.h"
#include "CppHighlighter.h"

#include <algorithm>
#include <filesystem>
#include <vector>
#include <cctype>

// Forward declare simple highlighters implemented in this project
namespace kte {
// Registration storage
struct RegEntry {
	std::string ft; // normalized
	HighlighterRegistry::Factory factory;
};


static std::vector<RegEntry> &
registry()
{
	static std::vector<RegEntry> reg;
	return reg;
}


class JSONHighlighter;
class MarkdownHighlighter;
class ShellHighlighter;
class GoHighlighter;
class PythonHighlighter;
class RustHighlighter;
class LispHighlighter;
class SqlHighlighter;
class ErlangHighlighter;
class ForthHighlighter;
}

// Headers for the above
#include "JsonHighlighter.h"
#include "MarkdownHighlighter.h"
#include "ShellHighlighter.h"
#include "GoHighlighter.h"
#include "PythonHighlighter.h"
#include "RustHighlighter.h"
#include "LispHighlighter.h"
#include "SqlHighlighter.h"
#include "ErlangHighlighter.h"
#include "ForthHighlighter.h"

namespace kte {
static std::string
to_lower(std::string_view s)
{
	std::string r(s);
	std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return r;
}


std::string
HighlighterRegistry::Normalize(std::string_view ft)
{
	std::string f = to_lower(ft);
	if (f == "c" || f == "c++" || f == "cc" || f == "hpp" || f == "hh" || f == "h" || f == "cxx")
		return "cpp";
	if (f == "cpp")
		return "cpp";
	if (f == "json")
		return "json";
	if (f == "markdown" || f == "md" || f == "mkd" || f == "mdown")
		return "markdown";
	if (f == "shell" || f == "sh" || f == "bash" || f == "zsh" || f == "ksh" || f == "fish")
		return "shell";
	if (f == "go" || f == "golang")
		return "go";
	if (f == "py" || f == "python")
		return "python";
	if (f == "rs" || f == "rust")
		return "rust";
	if (f == "lisp" || f == "scheme" || f == "scm" || f == "rkt" || f == "el" || f == "clj" || f == "cljc" || f ==
	    "cl")
		return "lisp";
	if (f == "sql" || f == "sqlite" || f == "sqlite3")
		return "sql";
	if (f == "erlang" || f == "erl" || f == "hrl")
		return "erlang";
	if (f == "forth" || f == "fth" || f == "4th" || f == "fs")
		return "forth";
	return f;
}


std::unique_ptr<LanguageHighlighter>
HighlighterRegistry::CreateFor(std::string_view filetype)
{
	std::string ft = Normalize(filetype);
	// Prefer externally registered factories
	for (const auto &e: registry()) {
		if (e.ft == ft && e.factory)
			return e.factory();
	}
	if (ft == "cpp")
		return std::make_unique<CppHighlighter>();
	if (ft == "json")
		return std::make_unique<JSONHighlighter>();
	if (ft == "markdown")
		return std::make_unique<MarkdownHighlighter>();
	if (ft == "shell")
		return std::make_unique<ShellHighlighter>();
	if (ft == "go")
		return std::make_unique<GoHighlighter>();
	if (ft == "python")
		return std::make_unique<PythonHighlighter>();
	if (ft == "rust")
		return std::make_unique<RustHighlighter>();
	if (ft == "lisp")
		return std::make_unique<LispHighlighter>();
	if (ft == "sql")
		return std::make_unique<SqlHighlighter>();
	if (ft == "erlang")
		return std::make_unique<ErlangHighlighter>();
	if (ft == "forth")
		return std::make_unique<ForthHighlighter>();
	return nullptr;
}


static std::string
shebang_to_ft(std::string_view first_line)
{
	if (first_line.size() < 2 || first_line.substr(0, 2) != "#!")
		return "";
	std::string low = to_lower(first_line);
	if (low.find("python") != std::string::npos)
		return "python";
	if (low.find("bash") != std::string::npos)
		return "shell";
	if (low.find("sh") != std::string::npos)
		return "shell";
	if (low.find("zsh") != std::string::npos)
		return "shell";
	if (low.find("fish") != std::string::npos)
		return "shell";
	if (low.find("scheme") != std::string::npos || low.find("racket") != std::string::npos || low.find("guile") !=
	    std::string::npos)
		return "lisp";
	return "";
}


std::string
HighlighterRegistry::DetectForPath(std::string_view path, std::string_view first_line)
{
	// Extension
	std::string p(path);
	std::error_code ec;
	std::string ext = std::filesystem::path(p).extension().string();
	for (auto &ch: ext)
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	if (!ext.empty()) {
		if (ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".h" || ext == ".hpp" || ext
		    == ".hh")
			return "cpp";
		if (ext == ".json")
			return "json";
		if (ext == ".md" || ext == ".markdown" || ext == ".mkd")
			return "markdown";
		if (ext == ".sh" || ext == ".bash" || ext == ".zsh" || ext == ".ksh" || ext == ".fish")
			return "shell";
		if (ext == ".go")
			return "go";
		if (ext == ".py")
			return "python";
		if (ext == ".rs")
			return "rust";
		if (ext == ".lisp" || ext == ".scm" || ext == ".rkt" || ext == ".el" || ext == ".clj" || ext == ".cljc"
		    || ext == ".cl")
			return "lisp";
		if (ext == ".sql" || ext == ".sqlite")
			return "sql";
		if (ext == ".erl" || ext == ".hrl")
			return "erlang";
		if (ext == ".forth" || ext == ".fth" || ext == ".4th" || ext == ".fs")
			return "forth";
	}
	// Shebang
	std::string ft = shebang_to_ft(first_line);
	return ft;
}
} // namespace kte

// Extensibility API implementations
namespace kte {
void
HighlighterRegistry::Register(std::string_view filetype, Factory factory, bool override_existing)
{
	std::string ft = Normalize(filetype);
	for (auto &e: registry()) {
		if (e.ft == ft) {
			if (override_existing)
				e.factory = std::move(factory);
			return;
		}
	}
	registry().push_back(RegEntry{ft, std::move(factory)});
}


bool
HighlighterRegistry::IsRegistered(std::string_view filetype)
{
	std::string ft = Normalize(filetype);
	for (const auto &e: registry())
		if (e.ft == ft)
			return true;
	return false;
}


std::vector<std::string>
HighlighterRegistry::RegisteredFiletypes()
{
	std::vector<std::string> out;
	out.reserve(registry().size());
	for (const auto &e: registry())
		out.push_back(e.ft);
	return out;
}

#ifdef KTE_ENABLE_TREESITTER
// Forward declare adapter factory
std::unique_ptr<LanguageHighlighter> CreateTreeSitterHighlighter(const char *filetype,
                                                                 const void * (*get_lang)());

void
HighlighterRegistry::RegisterTreeSitter(std::string_view filetype,
                                        const TSLanguage * (*get_language)())
{
	std::string ft = Normalize(filetype);
	Register(ft, [ft, get_language]() {
		return CreateTreeSitterHighlighter(ft.c_str(), reinterpret_cast<const void* (*)()>(get_language));
	}, /*override_existing=*/true);
}
#endif
} // namespace kte