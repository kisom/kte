// ErlangHighlighter.h - simple Erlang highlighter
#pragma once

#include "LanguageHighlighter.h"
#include <unordered_set>

namespace kte {
class ErlangHighlighter final : public LanguageHighlighter {
public:
	ErlangHighlighter();

	void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;

private:
	std::unordered_set<std::string> kws_;
};
} // namespace kte