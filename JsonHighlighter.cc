#include "JsonHighlighter.h"
#include "Buffer.h"
#include <cctype>

namespace kte {
static bool
is_digit(char c)
{
	return c >= '0' && c <= '9';
}


void
JSONHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
	const auto &rows = buf.Rows();
	if (row < 0 || static_cast<std::size_t>(row) >= rows.size())
		return;
	std::string s = static_cast<std::string>(rows[static_cast<std::size_t>(row)]);
	int n         = static_cast<int>(s.size());
	auto push     = [&](int a, int b, TokenKind k) {
		if (b > a)
			out.push_back({a, b, k});
	};

	int i = 0;
	while (i < n) {
		char c = s[i];
		if (c == ' ' || c == '\t') {
			int j = i + 1;
			while (j < n && (s[j] == ' ' || s[j] == '\t'))
				++j;
			push(i, j, TokenKind::Whitespace);
			i = j;
			continue;
		}
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
			push(i, j, TokenKind::String);
			i = j;
			continue;
		}
		if (is_digit(c) || (c == '-' && i + 1 < n && is_digit(s[i + 1]))) {
			int j = i + 1;
			while (j < n && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == 'e' ||
			                 s[j] == 'E' || s[j] == '+' || s[j] == '-' || s[j] == '_'))
				++j;
			push(i, j, TokenKind::Number);
			i = j;
			continue;
		}
		// booleans/null
		if (std::isalpha(static_cast<unsigned char>(c))) {
			int j = i + 1;
			while (j < n && std::isalpha(static_cast<unsigned char>(s[j])))
				++j;
			std::string id = s.substr(i, j - i);
			if (id == "true" || id == "false" || id == "null")
				push(i, j, TokenKind::Constant);
			else
				push(i, j, TokenKind::Identifier);
			i = j;
			continue;
		}
		// punctuation
		if (c == '{' || c == '}' || c == '[' || c == ']' || c == ',' || c == ':') {
			push(i, i + 1, TokenKind::Punctuation);
			++i;
			continue;
		}
		// fallback
		push(i, i + 1, TokenKind::Default);
		++i;
	}
}
} // namespace kte