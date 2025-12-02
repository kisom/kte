// HighlighterRegistry.h - create/detect language highlighters and allow external registration
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "LanguageHighlighter.h"

namespace kte {

class HighlighterRegistry {
public:
    using Factory = std::function<std::unique_ptr<LanguageHighlighter>()>;

    // Create a highlighter for normalized filetype id (e.g., "cpp", "json", "markdown", "shell", "go", "python", "rust", "lisp").
    static std::unique_ptr<LanguageHighlighter> CreateFor(std::string_view filetype);

    // Detect filetype by path extension and shebang (first line).
    // Returns normalized id or empty string if unknown.
    static std::string DetectForPath(std::string_view path, std::string_view first_line);

    // Normalize various aliases/extensions to canonical ids.
    static std::string Normalize(std::string_view ft);

    // Extensibility: allow external code to register highlighters at runtime.
    // The filetype key is normalized via Normalize(). If a factory is already registered for the
    // normalized key and override=false, the existing factory is kept.
    static void Register(std::string_view filetype, Factory factory, bool override_existing = true);

    // Returns true if a factory is registered for the (normalized) filetype.
    static bool IsRegistered(std::string_view filetype);

    // Return a list of currently registered (normalized) filetypes. Primarily for diagnostics/tests.
    static std::vector<std::string> RegisteredFiletypes();

#ifdef KTE_ENABLE_TREESITTER
    // Forward declaration to avoid hard dependency when disabled.
    struct TSLanguage;
    // Convenience: register a Tree-sitter-backed highlighter for a filetype.
    // The getter should return a non-null language pointer for the grammar.
    static void RegisterTreeSitter(std::string_view filetype,
                                   const TSLanguage* (*get_language)());
#endif
};

} // namespace kte
