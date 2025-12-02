// NullHighlighter.h - default highlighter that emits a single Default span per line
#pragma once

#include "LanguageHighlighter.h"

namespace kte {
class NullHighlighter final : public LanguageHighlighter {
public:
	void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;
};
} // namespace kte