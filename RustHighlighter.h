// RustHighlighter.h - simple Rust highlighter
#pragma once

#include "LanguageHighlighter.h"
#include <unordered_set>

namespace kte {

class RustHighlighter final : public LanguageHighlighter {
public:
    RustHighlighter();
    void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;
private:
    std::unordered_set<std::string> kws_;
    std::unordered_set<std::string> types_;
};

} // namespace kte
