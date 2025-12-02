// ForthHighlighter.h - simple Forth highlighter
#pragma once

#include "LanguageHighlighter.h"
#include <unordered_set>

namespace kte {
class ForthHighlighter final : public LanguageHighlighter {
public:
	ForthHighlighter();

	void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;

private:
	std::unordered_set<std::string> kws_;
};
} // namespace kte