// ShellHighlighter.h - simple POSIX shell highlighter
#pragma once

#include "LanguageHighlighter.h"

namespace kte {

class ShellHighlighter final : public LanguageHighlighter {
public:
    void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;
};

} // namespace kte
