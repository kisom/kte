// PythonHighlighter.h - simple Python highlighter with triple-quote state
#pragma once

#include "LanguageHighlighter.h"
#include <unordered_set>

namespace kte {

class PythonHighlighter final : public StatefulHighlighter {
public:
    PythonHighlighter();
    void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;
    LineState HighlightLineStateful(const Buffer &buf, int row, const LineState &prev, std::vector<HighlightSpan> &out) const override;
private:
    std::unordered_set<std::string> kws_;
};

} // namespace kte
