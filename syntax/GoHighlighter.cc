#include "GoHighlighter.h"
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
	return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}


static bool
is_ident_char(char c)
{
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}


GoHighlighter::GoHighlighter()
{
	const char *kw[] = {
		"break", "case", "chan", "const", "continue", "default", "defer", "else", "fallthrough", "for", "func",
		"go", "goto", "if", "import", "interface", "map", "package", "range", "return", "select", "struct",
		"switch", "type", "var"
	};
	for (auto s: kw)
		kws_.insert(s);
	const char *tp[] = {
		"bool", "byte", "complex64", "complex128", "error", "float32", "float64", "int", "int8", "int16",
		"int32", "int64", "rune", "string", "uint", "uint8", "uint16", "uint32", "uint64", "uintptr"
	};
	for (auto s: tp)
		types_.insert(s);
}


void
GoHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
	const auto &rows = buf.Rows();
	if (row < 0 || static_cast<std::size_t>(row) >= rows.size())
		return;
	std::string s = static_cast<std::string>(rows[static_cast<std::size_t>(row)]);
	int n         = static_cast<int>(s.size());
	int i         = 0;
	int bol       = 0;
	while (bol < n && (s[bol] == ' ' || s[bol] == '\t'))
		++bol;
	// line comment
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
		if (c == '/' && i + 1 < n && s[i + 1] == '/') {
			push(out, i, n, TokenKind::Comment);
			break;
		}
		if (c == '/' && i + 1 < n && s[i + 1] == '*') {
			int j       = i + 2;
			bool closed = false;
			while (j + 1 <= n) {
				if (j + 1 < n && s[j] == '*' && s[j + 1] == '/') {
					j += 2;
					closed = true;
					break;
				}
				++j;
			}
			if (!closed) {
				push(out, i, n, TokenKind::Comment);
				break;
			} else {
				push(out, i, j, TokenKind::Comment);
				i = j;
				continue;
			}
		}
		if (c == '"' || c == '`') {
			char q   = c;
			int j    = i + 1;
			bool esc = false;
			if (q == '`') {
				while (j < n && s[j] != '`')
					++j;
				if (j < n)
					++j;
			} else {
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
			}
			push(out, i, j, TokenKind::String);
			i = j;
			continue;
		}
		if (std::isdigit(static_cast<unsigned char>(c))) {
			int j = i + 1;
			while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == 'x' ||
			                 s[j] == 'X' || s[j] == '_'))
				++j;
			push(out, i, j, TokenKind::Number);
			i = j;
			continue;
		}
		if (is_ident_start(c)) {
			int j = i + 1;
			while (j < n && is_ident_char(s[j]))
				++j;
			std::string id = s.substr(i, j - i);
			TokenKind k    = TokenKind::Identifier;
			if (kws_.count(id))
				k = TokenKind::Keyword;
			else if (types_.count(id))
				k = TokenKind::Type;
			push(out, i, j, k);
			i = j;
			continue;
		}
		if (std::ispunct(static_cast<unsigned char>(c))) {
			TokenKind k = TokenKind::Operator;
			if (c == ';' || c == ',' || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c ==
			    ']')
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