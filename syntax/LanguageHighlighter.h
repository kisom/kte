// LanguageHighlighter.h - interface for line-based highlighters
#pragma once

#include <memory>
#include <vector>
#include <string>

#include "../Highlight.h"

class Buffer;

namespace kte {
class LanguageHighlighter {
public:
	virtual ~LanguageHighlighter() = default;

	// Produce highlight spans for a given buffer row. Implementations should append to out.
	virtual void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const = 0;


	virtual bool Stateful() const
	{
		return false;
	}
};

// Optional extension for stateful highlighters (e.g., multi-line comments/strings).
// Engines may detect and use this via dynamic_cast without breaking stateless impls.
class StatefulHighlighter : public LanguageHighlighter {
public:
	struct LineState {
		bool in_block_comment{false};
		bool in_raw_string{false};
		// For raw strings, remember the delimiter between the opening R"delim( and closing )delim"
		std::string raw_delim;
	};

	// Highlight one line given the previous line state; return the resulting state after this line.
	// Implementations should append spans for this line to out and compute the next state.
	virtual LineState HighlightLineStateful(const Buffer &buf,
	                                        int row,
	                                        const LineState &prev,
	                                        std::vector<HighlightSpan> &out) const = 0;


	bool Stateful() const override
	{
		return true;
	}
};
} // namespace kte