// test_utfcodec.cc - simple tests for UtfCodec helpers
#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>

#include "lsp/UtfCodec.h"

using namespace kte::lsp;


static std::string_view
lp(const std::string &, int)
{
	return std::string_view();
}


int
main()
{
	// ASCII: each scalar = 1 UTF-16 unit
	{
		std::string s = "hello"; // 5 ASCII
		assert(utf8ColToUtf16Units(s, 0) == 0);
		assert(utf8ColToUtf16Units(s, 3) == 3);
		assert(utf16UnitsToUtf8Col(s, 3) == 3);
		assert(utf16UnitsToUtf8Col(s, 10) == 5); // clamp to EOL
	}

	// BMP multibyte (e.g., ü U+00FC, α U+03B1) -> still 1 UTF-16 unit
	{
		std::string s = u8"aüαb"; // bytes: a [C3 BC] [CE B1] b
		// columns by codepoints: a(0), ü(1), α(2), b(3)
		assert(utf8ColToUtf16Units(s, 0) == 0);
		assert(utf8ColToUtf16Units(s, 1) == 1);
		assert(utf8ColToUtf16Units(s, 2) == 2);
		assert(utf8ColToUtf16Units(s, 4) == 4); // past EOL clamps to 4 units

		assert(utf16UnitsToUtf8Col(s, 0) == 0);
		assert(utf16UnitsToUtf8Col(s, 2) == 2);
		assert(utf16UnitsToUtf8Col(s, 4) == 4);
	}

	// Non-BMP (emoji) -> 2 UTF-16 units per code point
	{
		std::string s = u8"A😀B"; // U+1F600 between A and B
		// codepoints: A, 😀, B => utf8 columns 0..3
		// utf16 units: A(1), 😀(2), B(1) cumulative: 0,1,3,4
		assert(utf8ColToUtf16Units(s, 0) == 0);
		assert(utf8ColToUtf16Units(s, 1) == 1); // after A
		assert(utf8ColToUtf16Units(s, 2) == 3); // after 😀 (2 units)
		assert(utf8ColToUtf16Units(s, 3) == 4); // after B

		assert(utf16UnitsToUtf8Col(s, 0) == 0);
		assert(utf16UnitsToUtf8Col(s, 1) == 1); // A
		assert(utf16UnitsToUtf8Col(s, 2) == 1); // mid-surrogate -> stays before 😀
		assert(utf16UnitsToUtf8Col(s, 3) == 2); // end of 😀
		assert(utf16UnitsToUtf8Col(s, 4) == 3); // after B
		assert(utf16UnitsToUtf8Col(s, 10) == 3); // clamp
	}

	// Invalid UTF-8: treat invalid byte as U+FFFD (1 UTF-16 unit), consume 1 byte
	{
		std::string s;
		s.push_back('X');
		s.push_back(char(0xFF)); // invalid single byte
		s.push_back('Y');
		// Columns by codepoints as we decode: 'X', U+FFFD, 'Y'
		assert(utf8ColToUtf16Units(s, 0) == 0);
		assert(utf8ColToUtf16Units(s, 1) == 1);
		assert(utf8ColToUtf16Units(s, 2) == 2);
		assert(utf8ColToUtf16Units(s, 3) == 3);

		assert(utf16UnitsToUtf8Col(s, 0) == 0);
		assert(utf16UnitsToUtf8Col(s, 1) == 1);
		assert(utf16UnitsToUtf8Col(s, 2) == 2);
		assert(utf16UnitsToUtf8Col(s, 3) == 3);
	}

	// Position/Range helpers with a simple provider
	{
		std::string lines[]   = {u8"A😀B"};
		LineProvider provider = [&](const std::string &, int line) -> std::string_view {
			return (line == 0) ? std::string_view(lines[0]) : std::string_view();
		};
		Position p8{0, 2}; // after 😀 in utf8 columns
		Position p16 = toUtf16("file:///x", p8, provider);
		assert(p16.line == 0 && p16.character == 3);

		Position back = toUtf8("file:///x", p16, provider);
		assert(back.line == 0 && back.character == 2);

		Range r8{{0, 1}, {0, 3}}; // A|😀|B end
		Range r16 = toUtf16("file:///x", r8, provider);
		assert(r16.start.character == 1 && r16.end.character == 4);
	}

	std::puts("test_utfcodec: OK");
	return 0;
}