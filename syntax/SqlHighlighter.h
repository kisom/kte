// SqlHighlighter.h - simple SQL/SQLite highlighter
#pragma once

#include "LanguageHighlighter.h"
#include <unordered_set>

namespace kte {
class SqlHighlighter final : public LanguageHighlighter {
public:
	SqlHighlighter();

	void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;

private:
	std::unordered_set<std::string> kws_;
	std::unordered_set<std::string> types_;
};
} // namespace kte