#include "ErlangHighlighter.h"
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
is_ident_start(char c)
{
	return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '\'';
}


static bool
is_ident_char(char c)
{
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '@' || c == ':' || c == '?';
}


ErlangHighlighter::ErlangHighlighter()
{
	const char *kw[] = {
		"after", "begin", "case", "catch", "cond", "div", "end", "fun", "if", "let", "of",
		"receive", "when", "try", "rem", "and", "andalso", "orelse", "not", "band", "bor", "bxor",
		"bnot", "xor", "module", "export", "import", "record", "define", "undef", "include", "include_lib"
	};
	for (auto s: kw)
		kws_.insert(s);
}


void
ErlangHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
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
		// comment
		if (c == '%') {
			push(out, i, n, TokenKind::Comment);
			break;
		}
		// strings
		if (c == '"') {
			int j    = i + 1;
			bool esc = false;
			while (j < n) {
				char d = s[j++];
				if (esc) {
					esc = false;
					continue;
				}
				if (d == '\\') {
					esc = true;
					continue;
				}
				if (d == '"')
					break;
			}
			push(out, i, j, TokenKind::String);
			i = j;
			continue;
		}
		// char literal $X
		if (c == '$') {
			int j = i + 1;
			if (j < n && s[j] == '\\' && j + 1 < n)
				j += 2;
			else if (j < n)
				++j;
			push(out, i, j, TokenKind::Char);
			i = j;
			continue;
		}
		// numbers
		if (std::isdigit(static_cast<unsigned char>(c))) {
			int j = i + 1;
			while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '#' || s[j] == '.' ||
			                 s[j] == '_'))
				++j;
			push(out, i, j, TokenKind::Number);
			i = j;
			continue;
		}
		// atoms/variables/identifiers (including quoted atoms)
		if (is_ident_start(c)) {
			// quoted atom: '...'
			if (c == '\'') {
				int j    = i + 1;
				bool esc = false;
				while (j < n) {
					char d = s[j++];
					if (d == '\'') {
						if (j < n && s[j] == '\'') {
							++j;
							continue;
						}
						break;
					}
					if (d == '\\')
						esc = !esc;
				}
				push(out, i, j, TokenKind::Identifier);
				i = j;
				continue;
			}
			int j = i + 1;
			while (j < n && is_ident_char(s[j]))
				++j;
			std::string id = s.substr(i, j - i);
			// lowercase leading -> atom/function/module; uppercase or '_' -> variable
			TokenKind k = TokenKind::Identifier;
			// keyword check (lowercase)
			std::string lower;
			lower.reserve(id.size());
			for (char ch: id)
				lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
			if (kws_.count(lower))
				k = TokenKind::Keyword;
			push(out, i, j, k);
			i = j;
			continue;
		}
		if (std::ispunct(static_cast<unsigned char>(c))) {
			TokenKind k = TokenKind::Operator;
			if (c == ',' || c == ';' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c ==
			    '}')
				k = TokenKind::Punctuation;
			push(out, i, i + 1, k);
			++i;
			continue;
		}
		push(out, i, i + 1, TokenKind::Default);
		++i;
	}
}
} // namespace kte