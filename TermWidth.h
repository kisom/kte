// TermWidth.h - terminal cell width of a decoded character
//
// Shared by TerminalRenderer (drawing and cursor placement) and the command
// layer (horizontal scrolling, mouse column mapping) so they always agree.
// Tabs are handled by the callers, which know the current column.
#pragma once

#include <cstddef>
#include <cwchar>
#include <string_view>

namespace kte {
// Tab stops every kTabWidth columns, in every frontend and in the command
// layer's horizontal scrolling (they must agree, or the cursor and the
// scroll offset are misplaced on lines with tabs).
constexpr std::size_t kTabWidth = 8;


// C0 controls and DEL are drawn as caret notation ("^M", "^?"): two cells.
// ncurses would otherwise act on them (carriage return, backspace) and
// corrupt the line.
inline bool
IsCaretControl(wchar_t wch)
{
	return (wch >= 0 && wch < 0x20) || wch == 0x7f;
}


// C1 controls (U+0080..U+009F, also what a stray byte in that range decodes
// to) are drawn as '?': sent raw, U+009B is a terminal CSI.
inline bool
IsC1Control(wchar_t wch)
{
	return wch >= 0x80 && wch <= 0x9f;
}


inline int
CellWidth(wchar_t wch)
{
	if (IsCaretControl(wch))
		return 2;
	if (IsC1Control(wch))
		return 1;
	const int w = wcwidth(wch);
	return w < 0 ? 1 : w;
}


// Decode the character at line[i]: its byte length and display width in
// cells (a tab reaches the next multiple of tabw from column col). Measures
// the way TerminalRenderer draws: an invalid byte or NUL is one byte, measured
// as the byte value.
inline void
MeasureChar(std::string_view line, std::size_t i, std::size_t col, std::size_t &len, std::size_t &width,
            std::size_t tabw = kTabWidth)
{
	std::mbstate_t state{};
	wchar_t wch   = 0;
	const auto rc = std::mbrtowc(&wch, line.data() + i, line.size() - i, &state);
	if (rc == static_cast<std::size_t>(-1) || rc == static_cast<std::size_t>(-2) || rc == 0) {
		wch = static_cast<unsigned char>(line[i]);
		len = 1;
	} else {
		len = rc;
	}
	if (wch == L'\t')
		width = tabw - (col % tabw);
	else
		width = static_cast<std::size_t>(CellWidth(wch));
}
} // namespace kte
