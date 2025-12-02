/*
 * UtfCodec.cc - UTF-8 <-> UTF-16 code unit position conversions
 */
#include "UtfCodec.h"

#include <cassert>

namespace kte::lsp {
// Decode next code point from a UTF-8 string.
// On invalid input, consumes 1 byte and returns U+FFFD.
// Returns: (codepoint, bytesConsumed)
static inline std::pair<uint32_t, size_t>
decodeUtf8(std::string_view s, size_t i)
{
	if (i >= s.size())
		return {0, 0};
	unsigned char c0 = static_cast<unsigned char>(s[i]);
	if (c0 < 0x80) {
		return {c0, 1};
	}
	// Determine sequence length
	if ((c0 & 0xE0) == 0xC0) {
		if (i + 1 >= s.size())
			return {0xFFFD, 1};
		unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
		if ((c1 & 0xC0) != 0x80)
			return {0xFFFD, 1};
		uint32_t cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
		// Overlong check: must be >= 0x80
		if (cp < 0x80)
			return {0xFFFD, 1};
		return {cp, 2};
	}
	if ((c0 & 0xF0) == 0xE0) {
		if (i + 2 >= s.size())
			return {0xFFFD, 1};
		unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
		unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
		if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80)
			return {0xFFFD, 1};
		uint32_t cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
		// Overlong / surrogate range check
		if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))
			return {0xFFFD, 1};
		return {cp, 3};
	}
	if ((c0 & 0xF8) == 0xF0) {
		if (i + 3 >= s.size())
			return {0xFFFD, 1};
		unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
		unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
		unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
		if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
			return {0xFFFD, 1};
		uint32_t cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
		// Overlong / max range check
		if (cp < 0x10000 || cp > 0x10FFFF)
			return {0xFFFD, 1};
		return {cp, 4};
	}
	return {0xFFFD, 1};
}


static inline size_t
utf16UnitsForCodepoint(uint32_t cp)
{
	return (cp <= 0xFFFF) ? 1 : 2;
}


size_t
utf8ColToUtf16Units(std::string_view lineUtf8, size_t utf8Col)
{
	// Count by Unicode scalars up to utf8Col; clamp at EOL
	size_t units = 0;
	size_t col   = 0;
	size_t i     = 0;
	while (i < lineUtf8.size()) {
		if (col >= utf8Col)
			break;
		auto [cp, n] = decodeUtf8(lineUtf8, i);
		if (n == 0)
			break;
		units += utf16UnitsForCodepoint(cp);
		i += n;
		++col;
	}
	return units;
}


size_t
utf16UnitsToUtf8Col(std::string_view lineUtf8, size_t utf16Units)
{
	// Traverse code points until consuming utf16Units (or reaching EOL)
	size_t units = 0;
	size_t col   = 0;
	size_t i     = 0;
	while (i < lineUtf8.size()) {
		auto [cp, n] = decodeUtf8(lineUtf8, i);
		if (n == 0)
			break;
		size_t add = utf16UnitsForCodepoint(cp);
		if (units + add > utf16Units)
			break;
		units += add;
		i += n;
		++col;
		if (units == utf16Units)
			break;
	}
	return col;
}


Position
toUtf16(const std::string &uri, const Position &pUtf8, const LineProvider &provider)
{
	Position out          = pUtf8;
	std::string_view line = provider ? provider(uri, pUtf8.line) : std::string_view();
	out.character         = static_cast<int>(utf8ColToUtf16Units(line, static_cast<size_t>(pUtf8.character)));
	return out;
}


Position
toUtf8(const std::string &uri, const Position &pUtf16, const LineProvider &provider)
{
	Position out          = pUtf16;
	std::string_view line = provider ? provider(uri, pUtf16.line) : std::string_view();
	out.character         = static_cast<int>(utf16UnitsToUtf8Col(line, static_cast<size_t>(pUtf16.character)));
	return out;
}


Range
toUtf16(const std::string &uri, const Range &rUtf8, const LineProvider &provider)
{
	Range r;
	r.start = toUtf16(uri, rUtf8.start, provider);
	r.end   = toUtf16(uri, rUtf8.end, provider);
	return r;
}


Range
toUtf8(const std::string &uri, const Range &rUtf16, const LineProvider &provider)
{
	Range r;
	r.start = toUtf8(uri, rUtf16.start, provider);
	r.end   = toUtf8(uri, rUtf16.end, provider);
	return r;
}
} // namespace kte::lsp