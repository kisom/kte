// TermWidth.h - terminal cell width of a decoded character
//
// Shared by TerminalRenderer (drawing and cursor placement) and the command
// layer (horizontal scrolling, mouse column mapping) so they always agree.
// Tabs are handled by the callers, which know the current column.
#pragma once

#include <cwchar>

namespace kte {
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
} // namespace kte
