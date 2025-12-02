// LispHighlighter.h - simple Lisp/Scheme family highlighter
#pragma once

#include "LanguageHighlighter.h"
#include <unordered_set>

namespace kte {

class LispHighlighter final : public LanguageHighlighter {
public:
    LispHighlighter();
    void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;
private:
    std::unordered_set<std::string> kws_;
};

} // namespace kte
