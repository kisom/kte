// TreeSitterHighlighter.h - optional adapter for Tree-sitter (behind KTE_ENABLE_TREESITTER)
#pragma once

#ifdef KTE_ENABLE_TREESITTER

#include <memory>
#include <string>
#include <vector>

#include "LanguageHighlighter.h"

// Forward-declare Tree-sitter C API to avoid hard coupling in headers if includes are not present
extern "C" {
struct TSLanguage;
struct TSParser;
struct TSTree;
}

namespace kte {

// A minimal adapter that uses Tree-sitter to parse the whole buffer and then, for now,
// does very limited token classification. This acts as a scaffold for future richer
// queries. If no queries are provided, it currently produces no spans (safe fallback).
class TreeSitterHighlighter : public LanguageHighlighter {
public:
    explicit TreeSitterHighlighter(const TSLanguage* lang, std::string filetype);
    ~TreeSitterHighlighter() override;

    void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;

private:
    const TSLanguage* language_{nullptr};
    std::string filetype_;
    // Lazy parser to avoid startup cost; mutable to allow creation in const method
    mutable TSParser* parser_{nullptr};
    mutable TSTree* tree_{nullptr};

    void ensureParsed(const Buffer& buf) const;
    void disposeParser() const;
};

// Factory used by HighlighterRegistry when registering via RegisterTreeSitter.
std::unique_ptr<LanguageHighlighter> CreateTreeSitterHighlighter(const char* filetype,
                                                                 const void* (*get_lang)());

} // namespace kte

#endif // KTE_ENABLE_TREESITTER
