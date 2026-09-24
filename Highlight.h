// Highlight.h - core syntax highlighting types for kte
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kte {
// Token kinds shared between renderers and highlighters
enum class TokenKind {
	Default,
	Keyword,
	Type,
	String,
	Char,
	Comment,
	Number,
	Preproc,
	Constant,
	Function,
	Operator,
	Punctuation,
	Identifier,
	Whitespace,
	Error
};

struct HighlightSpan {
	int col_start{0}; // inclusive, 0-based columns in buffer indices
	int col_end{0}; // exclusive
	TokenKind kind{TokenKind::Default};
};

struct LineHighlight {
	std::vector<HighlightSpan> spans;
	std::uint64_t version{0}; // buffer version used for this line
};


// A line's spans ready to draw: clamped to the line, ordered by start, not
// overlapping (the earlier span wins), and widened to whole UTF-8
// characters. Highlighters may emit one span per byte of a non-ASCII
// character; drawn separately, each byte showed as a replacement glyph.
inline void
SanitizeSpans(std::string_view line, const std::vector<HighlightSpan> &in, std::vector<HighlightSpan> &out)
{
	out.clear();
	const long len = static_cast<long>(line.size());
	for (const HighlightSpan &sp: in) {
		long s = std::clamp<long>(std::min(sp.col_start, sp.col_end), 0, len);
		long e = std::clamp<long>(std::max(sp.col_start, sp.col_end), 0, len);
		if (e > s)
			out.push_back(HighlightSpan{static_cast<int>(s), static_cast<int>(e), sp.kind});
	}
	std::stable_sort(out.begin(), out.end(), [](const HighlightSpan &a, const HighlightSpan &b) {
		return a.col_start < b.col_start;
	});
	auto continuation = [&](long i) {
		return i < len && (static_cast<unsigned char>(line[static_cast<std::size_t>(i)]) & 0xC0) == 0x80;
	};
	std::size_t w = 0;
	long prev_end = 0;
	for (const HighlightSpan &sp: out) {
		long s = sp.col_start, e = sp.col_end;
		while (s > 0 && continuation(s))
			--s;
		while (e < len && continuation(e))
			++e;
		s = std::max(s, prev_end);
		if (e <= s)
			continue;
		out[w++] = HighlightSpan{static_cast<int>(s), static_cast<int>(e), sp.kind};
		prev_end = e;
	}
	out.resize(w);
}
} // namespace kte
