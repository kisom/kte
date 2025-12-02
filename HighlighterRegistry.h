// HighlighterRegistry.h - create/detect language highlighters
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "LanguageHighlighter.h"

namespace kte {

class HighlighterRegistry {
public:
    // Create a highlighter for normalized filetype id (e.g., "cpp", "json", "markdown", "shell", "go", "python", "rust", "lisp").
    static std::unique_ptr<LanguageHighlighter> CreateFor(std::string_view filetype);

    // Detect filetype by path extension and shebang (first line).
    // Returns normalized id or empty string if unknown.
    static std::string DetectForPath(std::string_view path, std::string_view first_line);

    // Normalize various aliases/extensions to canonical ids.
    static std::string Normalize(std::string_view ft);
};

} // namespace kte
