#include "MarkdownHighlighter.h"
#include "Buffer.h"
#include <cctype>

namespace kte {
static void
push_span(std::vector<HighlightSpan> &out, int a, int b, TokenKind k)
{
	if (b > a)
		out.push_back({a, b, k});
}


void
MarkdownHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
	LineState st; // not used in stateless entry
	(void) HighlightLineStateful(buf, row, st, out);
}


StatefulHighlighter::LineState
MarkdownHighlighter::HighlightLineStateful(const Buffer &buf, int row, const LineState &prev,
                                           std::vector<HighlightSpan> &out) const
{
	StatefulHighlighter::LineState state = prev;
	const auto &rows                     = buf.Rows();
	if (row < 0 || static_cast<std::size_t>(row) >= rows.size())
		return state;
	std::string s = static_cast<std::string>(rows[static_cast<std::size_t>(row)]);
	int n         = static_cast<int>(s.size());

	// Reuse in_block_comment flag as "in fenced code" state.
	if (state.in_block_comment) {
		// If line contains closing fence ``` then close after it
		auto pos = s.find("```");
		if (pos == std::string::npos) {
			push_span(out, 0, n, TokenKind::String);
			state.in_block_comment = true;
			return state;
		} else {
			int end = static_cast<int>(pos + 3);
			push_span(out, 0, end, TokenKind::String);
			// rest of line processed normally after fence
			int i = end;
			// whitespace
			if (i < n)
				push_span(out, i, n, TokenKind::Default);
			state.in_block_comment = false;
			return state;
		}
	}

	// Detect fenced code block start at beginning (allow leading spaces)
	int bol = 0;
	while (bol < n && (s[bol] == ' ' || s[bol] == '\t'))
		++bol;
	if (bol + 3 <= n && s.compare(bol, 3, "```") == 0) {
		push_span(out, bol, n, TokenKind::String);
		state.in_block_comment = true; // enter fenced mode
		return state;
	}

	// Headings: lines starting with 1-6 '#'
	if (bol < n && s[bol] == '#') {
		int j = bol;
		while (j < n && s[j] == '#')
			++j; // hashes
		// include following space and text as Keyword to stand out
		push_span(out, bol, n, TokenKind::Keyword);
		return state;
	}

	// Process inline: emphasis and code spans
	int i = 0;
	while (i < n) {
		char c = s[i];
		if (c == '`') {
			int j = i + 1;
			while (j < n && s[j] != '`')
				++j;
			if (j < n)
				++j;
			push_span(out, i, j, TokenKind::String);
			i = j;
			continue;
		}
		if (c == '*' || c == '_') {
			// bold/italic markers: treat the marker and until next same marker as Type to highlight
			char m = c;
			int j  = i + 1;
			while (j < n && s[j] != m)
				++j;
			if (j < n)
				++j;
			push_span(out, i, j, TokenKind::Type);
			i = j;
			continue;
		}
		// links []() minimal: treat [text](url) as Function
		if (c == '[') {
			int j = i + 1;
			while (j < n && s[j] != ']')
				++j;
			if (j < n)
				++j; // include ]
			if (j < n && s[j] == '(') {
				while (j < n && s[j] != ')')
					++j;
				if (j < n)
					++j;
			}
			push_span(out, i, j, TokenKind::Function);
			i = j;
			continue;
		}
		// whitespace
		if (c == ' ' || c == '\t') {
			int j = i + 1;
			while (j < n && (s[j] == ' ' || s[j] == '\t'))
				++j;
			push_span(out, i, j, TokenKind::Whitespace);
			i = j;
			continue;
		}
		// fallback: default single char
		push_span(out, i, i + 1, TokenKind::Default);
		++i;
	}
	return state;
}
} // namespace kte