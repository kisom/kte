// MarkdownHighlighter.h - simple Markdown highlighter
#pragma once

#include "LanguageHighlighter.h"

namespace kte {
class MarkdownHighlighter final : public StatefulHighlighter {
public:
	void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;

	LineState HighlightLineStateful(const Buffer &buf, int row, const LineState &prev,
	                                std::vector<HighlightSpan> &out) const override;
};
} // namespace kte