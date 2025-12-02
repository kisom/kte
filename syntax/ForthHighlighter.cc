#include "ForthHighlighter.h"
#include "../Buffer.h"
#include <cctype>

namespace kte {
static void
push(std::vector<HighlightSpan> &out, int a, int b, TokenKind k)
{
	if (b > a)
		out.push_back({a, b, k});
}


static bool
is_word_char(char c)
{
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '>' || c == '<' || c == '?';
}


ForthHighlighter::ForthHighlighter()
{
	const char *kw[] = {
		":", ";", "if", "else", "then", "begin", "until", "while", "repeat",
		"do", "loop", "+loop", "leave", "again", "case", "of", "endof", "endcase",
		".", ".r", ".s", ".\"", ",", "cr", "emit", "type", "key",
		"+", "-", "*", "/", "mod", "/mod", "+-", "abs", "min", "max",
		"dup", "drop", "swap", "over", "rot", "-rot", "nip", "tuck", "pick", "roll",
		"and", "or", "xor", "invert", "lshift", "rshift",
		"variable", "constant", "value", "to", "create", "does>", "allot", ",",
		"cells", "cell+", "chars", "char+",
		"[", "]", "immediate",
		"s\"", ".\""
	};
	for (auto s: kw)
		kws_.insert(s);
}


void
ForthHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
	const auto &rows = buf.Rows();
	if (row < 0 || static_cast<std::size_t>(row) >= rows.size())
		return;
	std::string s = static_cast<std::string>(rows[static_cast<std::size_t>(row)]);
	int n         = static_cast<int>(s.size());
	int i         = 0;

	while (i < n) {
		char c = s[i];
		if (c == ' ' || c == '\t') {
			int j = i + 1;
			while (j < n && (s[j] == ' ' || s[j] == '\t'))
				++j;
			push(out, i, j, TokenKind::Whitespace);
			i = j;
			continue;
		}
		// backslash comment to end of line
		if (c == '\\') {
			push(out, i, n, TokenKind::Comment);
			break;
		}
		// parenthesis comment ( ... ) if at word boundary
		if (c == '(') {
			int j = i + 1;
			while (j < n && s[j] != ')')
				++j;
			if (j < n)
				++j;
			push(out, i, j, TokenKind::Comment);
			i = j;
			continue;
		}
		// strings: ." ... " and S" ... " and raw "..."
		if (c == '"') {
			int j = i + 1;
			while (j < n && s[j] != '"')
				++j;
			if (j < n)
				++j;
			push(out, i, j, TokenKind::String);
			i = j;
			continue;
		}
		if (std::isdigit(static_cast<unsigned char>(c))) {
			int j = i + 1;
			while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '#'))
				++j;
			push(out, i, j, TokenKind::Number);
			i = j;
			continue;
		}
		// word/identifier
		if (std::isalpha(static_cast<unsigned char>(c)) || std::ispunct(static_cast<unsigned char>(c))) {
			int j = i + 1;
			while (j < n && is_word_char(s[j]))
				++j;
			std::string w = s.substr(i, j - i);
			// normalize to lowercase for keyword compare (Forth is case-insensitive typically)
			std::string lower;
			lower.reserve(w.size());
			for (char ch: w)
				lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
			TokenKind k = kws_.count(lower) ? TokenKind::Keyword : TokenKind::Identifier;
			// Single-char punctuation fallback
			if (w.size() == 1 && std::ispunct(static_cast<unsigned char>(w[0])) && !kws_.count(lower)) {
				k = (w[0] == '(' || w[0] == ')' || w[0] == ',')
					    ? TokenKind::Punctuation
					    : TokenKind::Operator;
			}
			push(out, i, j, k);
			i = j;
			continue;
		}
		push(out, i, i + 1, TokenKind::Default);
		++i;
	}
}
} // namespace kte