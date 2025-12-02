// JsonHighlighter.h - simple JSON line highlighter
#pragma once

#include "LanguageHighlighter.h"
#include <vector>

namespace kte {

class JSONHighlighter final : public LanguageHighlighter {
public:
    void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;
};

} // namespace kte
