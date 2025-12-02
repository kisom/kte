#include "CppHighlighter.h"
#include "Buffer.h"
#include <cctype>

namespace kte {
static bool
is_digit(char c)
{
	return c >= '0' && c <= '9';
}


CppHighlighter::CppHighlighter()
{
	const char *kw[] = {
		"if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
		"return", "goto", "struct", "class", "namespace", "using", "template", "typename",
		"public", "private", "protected", "virtual", "override", "const", "constexpr", "auto",
		"static", "inline", "operator", "new", "delete", "try", "catch", "throw", "friend",
		"enum", "union", "extern", "volatile", "mutable", "noexcept", "sizeof", "this"
	};
	for (auto s: kw)
		keywords_.insert(s);
	const char *types[] = {
		"int", "long", "short", "char", "signed", "unsigned", "float", "double", "void",
		"bool", "wchar_t", "size_t", "ptrdiff_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
		"int8_t", "int16_t", "int32_t", "int64_t"
	};
	for (auto s: types)
		types_.insert(s);
}


bool
CppHighlighter::is_ident_start(char c)
{
	return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}


bool
CppHighlighter::is_ident_char(char c)
{
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}


void
CppHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
	// Stateless entry simply delegates to stateful with a clean previous state
	StatefulHighlighter::LineState prev;
	(void) HighlightLineStateful(buf, row, prev, out);
}


StatefulHighlighter::LineState
CppHighlighter::HighlightLineStateful(const Buffer &buf,
                                      int row,
                                      const LineState &prev,
                                      std::vector<HighlightSpan> &out) const
{
	const auto &rows                     = buf.Rows();
	StatefulHighlighter::LineState state = prev;
	if (row < 0 || static_cast<std::size_t>(row) >= rows.size())
		return state;
	std::string s = static_cast<std::string>(rows[static_cast<std::size_t>(row)]);
	if (s.empty())
		return state;

	auto push = [&](int a, int b, TokenKind k) {
		if (b > a)
			out.push_back({a, b, k});
	};
	int n   = static_cast<int>(s.size());
	int bol = 0;
	while (bol < n && (s[bol] == ' ' || s[bol] == '\t'))
		++bol;
	int i = 0;

	// Continue multi-line raw string from previous line
	if (state.in_raw_string) {
		std::string needle = ")" + state.raw_delim + "\"";
		auto pos           = s.find(needle);
		if (pos == std::string::npos) {
			push(0, n, TokenKind::String);
			state.in_raw_string = true;
			return state;
		} else {
			int end = static_cast<int>(pos + needle.size());
			push(0, end, TokenKind::String);
			i                   = end;
			state.in_raw_string = false;
			state.raw_delim.clear();
		}
	}

	// Continue multi-line block comment from previous line
	if (state.in_block_comment) {
		int j = i;
		while (i + 1 < n) {
			if (s[i] == '*' && s[i + 1] == '/') {
				i += 2;
				push(j, i, TokenKind::Comment);
				state.in_block_comment = false;
				break;
			}
			++i;
		}
		if (state.in_block_comment) {
			push(j, n, TokenKind::Comment);
			return state;
		}
	}

	while (i < n) {
		char c = s[i];
		// Preprocessor at beginning of line (after leading whitespace)
		if (i == bol && c == '#') {
			push(0, n, TokenKind::Preproc);
			break;
		}

		// Whitespace
		if (c == ' ' || c == '\t') {
			int j = i + 1;
			while (j < n && (s[j] == ' ' || s[j] == '\t'))
				++j;
			push(i, j, TokenKind::Whitespace);
			i = j;
			continue;
		}

		// Line comment
		if (c == '/' && i + 1 < n && s[i + 1] == '/') {
			push(i, n, TokenKind::Comment);
			break;
		}

		// Block comment
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
			if (closed) {
				push(i, j, TokenKind::Comment);
				i = j;
				continue;
			}
			// Spill to next lines
			push(i, n, TokenKind::Comment);
			state.in_block_comment = true;
			return state;
		}

		// Raw string start: very simple detection: R"delim(
		if (c == 'R' && i + 1 < n && s[i + 1] == '"') {
			int k = i + 2;
			std::string delim;
			while (k < n && s[k] != '(') {
				delim.push_back(s[k]);
				++k;
			}
			if (k < n && s[k] == '(') {
				int body_start     = k + 1;
				std::string needle = ")" + delim + "\"";
				auto pos           = s.find(needle, static_cast<std::size_t>(body_start));
				if (pos == std::string::npos) {
					push(i, n, TokenKind::String);
					state.in_raw_string = true;
					state.raw_delim     = delim;
					return state;
				} else {
					int end = static_cast<int>(pos + needle.size());
					push(i, end, TokenKind::String);
					i = end;
					continue;
				}
			}
			// If malformed, just treat 'R' as identifier fallback
		}

		// Regular string literal
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

		// Char literal
		if (c == '\'') {
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
				if (d == '\'')
					break;
			}
			push(i, j, TokenKind::Char);
			i = j;
			continue;
		}

		// Number literal (simple)
		if (is_digit(c) || (c == '.' && i + 1 < n && is_digit(s[i + 1]))) {
			int j = i + 1;
			while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == 'x' ||
			                 s[j] == 'X' || s[j] == 'b' || s[j] == 'B' || s[j] == '_'))
				++j;
			push(i, j, TokenKind::Number);
			i = j;
			continue;
		}

		// Identifier / keyword / type
		if (is_ident_start(c)) {
			int j = i + 1;
			while (j < n && is_ident_char(s[j]))
				++j;
			std::string id = s.substr(i, j - i);
			TokenKind k    = TokenKind::Identifier;
			if (keywords_.count(id))
				k = TokenKind::Keyword;
			else if (types_.count(id))
				k = TokenKind::Type;
			push(i, j, k);
			i = j;
			continue;
		}

		// Operators and punctuation (single char for now)
		TokenKind kind = TokenKind::Operator;
		if (std::ispunct(static_cast<unsigned char>(c)) && c != '_' && c != '#') {
			if (c == ';' || c == ',' || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c ==
			    ']')
				kind = TokenKind::Punctuation;
			push(i, i + 1, kind);
			++i;
			continue;
		}

		// Fallback
		push(i, i + 1, TokenKind::Default);
		++i;
	}

	return state;
}
} // namespace kte