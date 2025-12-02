#include "ShellHighlighter.h"
#include "Buffer.h"
#include <cctype>

namespace kte {
static void
push(std::vector<HighlightSpan> &out, int a, int b, TokenKind k)
{
	if (b > a)
		out.push_back({a, b, k});
}


void
ShellHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
	const auto &rows = buf.Rows();
	if (row < 0 || static_cast<std::size_t>(row) >= rows.size())
		return;
	std::string s = static_cast<std::string>(rows[static_cast<std::size_t>(row)]);
	int n         = static_cast<int>(s.size());
	int i         = 0;
	// if first non-space is '#', whole line is comment
	int bol = 0;
	while (bol < n && (s[bol] == ' ' || s[bol] == '\t'))
		++bol;
	if (bol < n && s[bol] == '#') {
		push(out, bol, n, TokenKind::Comment);
		if (bol > 0)
			push(out, 0, bol, TokenKind::Whitespace);
		return;
	}
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
		if (c == '#') {
			push(out, i, n, TokenKind::Comment);
			break;
		}
		if (c == '\'' || c == '"') {
			char q   = c;
			int j    = i + 1;
			bool esc = false;
			while (j < n) {
				char d = s[j++];
				if (q == '"') {
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
				} else {
					if (d == '\'')
						break;
				}
			}
			push(out, i, j, TokenKind::String);
			i = j;
			continue;
		}
		// simple keywords
		if (std::isalpha(static_cast<unsigned char>(c))) {
			int j = i + 1;
			while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'))
				++j;
			std::string id           = s.substr(i, j - i);
			static const char *kws[] = {
				"if", "then", "fi", "for", "in", "do", "done", "case", "esac", "while", "function",
				"elif", "else"
			};
			bool kw = false;
			for (auto k: kws)
				if (id == k) {
					kw = true;
					break;
				}
			push(out, i, j, kw ? TokenKind::Keyword : TokenKind::Identifier);
			i = j;
			continue;
		}
		if (std::ispunct(static_cast<unsigned char>(c))) {
			TokenKind k = TokenKind::Operator;
			if (c == '(' || c == ')' || c == '{' || c == '}' || c == ',' || c == ';')
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