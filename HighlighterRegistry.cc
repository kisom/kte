#include "HighlighterRegistry.h"
#include "CppHighlighter.h"

#include <algorithm>
#include <filesystem>

// Forward declare simple highlighters implemented in this project
namespace kte {
class JSONHighlighter; class MarkdownHighlighter; class ShellHighlighter;
class GoHighlighter; class PythonHighlighter; class RustHighlighter; class LispHighlighter;
}

// Headers for the above
#include "JsonHighlighter.h"
#include "MarkdownHighlighter.h"
#include "ShellHighlighter.h"
#include "GoHighlighter.h"
#include "PythonHighlighter.h"
#include "RustHighlighter.h"
#include "LispHighlighter.h"

namespace kte {

static std::string to_lower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return r;
}

std::string HighlighterRegistry::Normalize(std::string_view ft)
{
    std::string f = to_lower(ft);
    if (f == "c" || f == "c++" || f == "cc" || f == "hpp" || f == "hh" || f == "h" || f == "cxx") return "cpp";
    if (f == "cpp") return "cpp";
    if (f == "json") return "json";
    if (f == "markdown" || f == "md" || f == "mkd" || f == "mdown") return "markdown";
    if (f == "shell" || f == "sh" || f == "bash" || f == "zsh" || f == "ksh" || f == "fish") return "shell";
    if (f == "go" || f == "golang") return "go";
    if (f == "py" || f == "python") return "python";
    if (f == "rs" || f == "rust") return "rust";
    if (f == "lisp" || f == "scheme" || f == "scm" || f == "rkt" || f == "el" || f == "clj" || f == "cljc" || f == "cl") return "lisp";
    return f;
}

std::unique_ptr<LanguageHighlighter> HighlighterRegistry::CreateFor(std::string_view filetype)
{
    std::string ft = Normalize(filetype);
    if (ft == "cpp") return std::make_unique<CppHighlighter>();
    if (ft == "json") return std::make_unique<JSONHighlighter>();
    if (ft == "markdown") return std::make_unique<MarkdownHighlighter>();
    if (ft == "shell") return std::make_unique<ShellHighlighter>();
    if (ft == "go") return std::make_unique<GoHighlighter>();
    if (ft == "python") return std::make_unique<PythonHighlighter>();
    if (ft == "rust") return std::make_unique<RustHighlighter>();
    if (ft == "lisp") return std::make_unique<LispHighlighter>();
    return nullptr;
}

static std::string shebang_to_ft(std::string_view first_line) {
    if (first_line.size() < 2 || first_line.substr(0,2) != "#!") return "";
    std::string low = to_lower(first_line);
    if (low.find("python") != std::string::npos) return "python";
    if (low.find("bash") != std::string::npos) return "shell";
    if (low.find("sh") != std::string::npos) return "shell";
    if (low.find("zsh") != std::string::npos) return "shell";
    if (low.find("fish") != std::string::npos) return "shell";
    if (low.find("scheme") != std::string::npos || low.find("racket") != std::string::npos || low.find("guile") != std::string::npos) return "lisp";
    return "";
}

std::string HighlighterRegistry::DetectForPath(std::string_view path, std::string_view first_line)
{
    // Extension
    std::string p(path);
    std::error_code ec;
    std::string ext = std::filesystem::path(p).extension().string();
    for (auto &ch: ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (!ext.empty()) {
        if (ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".h" || ext == ".hpp" || ext == ".hh") return "cpp";
        if (ext == ".json") return "json";
        if (ext == ".md" || ext == ".markdown" || ext == ".mkd") return "markdown";
        if (ext == ".sh" || ext == ".bash" || ext == ".zsh" || ext == ".ksh" || ext == ".fish") return "shell";
        if (ext == ".go") return "go";
        if (ext == ".py") return "python";
        if (ext == ".rs") return "rust";
        if (ext == ".lisp" || ext == ".scm" || ext == ".rkt" || ext == ".el" || ext == ".clj" || ext == ".cljc" || ext == ".cl") return "lisp";
    }
    // Shebang
    std::string ft = shebang_to_ft(first_line);
    return ft;
}

} // namespace kte
