// Highlight.h - core syntax highlighting types for kte
#pragma once

#include <cstdint>
#include <vector>

namespace kte {

// Token kinds shared between renderers and highlighters
enum class TokenKind {
    Default,
    Keyword,
    Type,
    String,
    Char,
    Comment,
    Number,
    Preproc,
    Constant,
    Function,
    Operator,
    Punctuation,
    Identifier,
    Whitespace,
    Error
};

struct HighlightSpan {
    int col_start{0}; // inclusive, 0-based columns in buffer indices
    int col_end{0};   // exclusive
    TokenKind kind{TokenKind::Default};
};

struct LineHighlight {
    std::vector<HighlightSpan> spans;
    std::uint64_t version{0}; // buffer version used for this line
};

} // namespace kte
