#include "KKeymap.h"
#include <ncurses.h>


auto
KLookupKCommand(const int ascii_key, const bool ctrl, CommandId &out) -> bool
{
	// For k-prefix, preserve case to allow distinct mappings (e.g., 'U' vs 'u').
	const int k_lower = KLowerAscii(ascii_key);

	// 1) Try Control-specific C-k mappings first
	if (ctrl) {
		switch (k_lower) {
		case 'd':
			out = CommandId::KillLine;
			return true; // C-k C-d
		case 'x':
			out = CommandId::SaveAndQuit;
			return true; // C-k C-x
		case 'q':
			out = CommandId::QuitNow;
			return true; // C-k C-q (quit immediately)
		default:
			// Important: do not return here — fall through to non-ctrl table
			// so that C-k u/U still work even if Ctrl is (incorrectly) held
			break;
		}
	}

	// 2) Case-sensitive bindings must be checked before case-insensitive table.
	if (ascii_key == 'r') {
		out = CommandId::Redo; // C-k r (redo)
		return true;
	}

	// 3) Non-control k-table (lowercased)
	switch (k_lower) {
	case 'j':
		out = CommandId::JumpToMark;
		return true; // C-k j
	case 'f':
		out = CommandId::FlushKillRing;
		return true; // C-k f
	case 'd':
		out = CommandId::KillToEOL;
		return true; // C-k d
	case 'y':
		out = CommandId::Yank;
		return true; // C-k y
	case 's':
		out = CommandId::Save;
		return true; // C-k s
	case 'e':
		out = CommandId::OpenFileStart;
		return true; // C-k e (open file)
	case 'b':
		out = CommandId::BufferSwitchStart;
		return true; // C-k b (switch buffer by name)
	case 'c':
		out = CommandId::BufferClose;
		return true; // C-k c (close current buffer)
	case 'n':
		out = CommandId::BufferPrev;
		return true; // C-k n (switch to previous buffer)
	case 'x':
		out = CommandId::SaveAndQuit;
		return true; // C-k x
	case 'q':
		out = CommandId::Quit;
		return true; // C-k q
	case 'p':
		out = CommandId::BufferNext;
		return true; // C-k p (switch to next buffer)
	case 'u':
		out = CommandId::Undo;
		return true; // C-k u (undo)
	case '-':
		out = CommandId::UnindentRegion;
		return true; // C-k - (unindent region)
	case '=':
		out = CommandId::IndentRegion;
		return true; // C-k = (indent region)
	case 'l':
		out = CommandId::ReloadBuffer;
		return true; // C-k l (reload buffer)
	case 'a':
		out = CommandId::MarkAllAndJumpEnd;
		return true; // C-k a (mark all and jump to end)
	default:
		break;
	}
	return false;
}


auto
KLookupCtrlCommand(const int ascii_key, CommandId &out) -> bool
{
	const int k = KLowerAscii(ascii_key);
	switch (k) {
	case 'w':
		out = CommandId::KillRegion; // C-w
		return true;
	case 'y':
		out = CommandId::Yank; // C-y
		return true;
	case 'd':
		out = CommandId::DeleteChar; // C-d
		return true;
	case 'n':
		out = CommandId::MoveDown;
		return true;
	case 'p':
		out = CommandId::MoveUp;
		return true;
	case 'f':
		out = CommandId::MoveRight;
		return true;
	case 'b':
		out = CommandId::MoveLeft;
		return true;
	case 'a':
		out = CommandId::MoveHome;
		return true;
	case 'e':
		out = CommandId::MoveEnd;
		return true;
	case 's':
		out = CommandId::FindStart;
		return true;
	case 'l':
		out = CommandId::Refresh;
		return true;
	case 'g':
		out = CommandId::Refresh;
		return true;
	case 'x':
		out = CommandId::SaveAndQuit; // direct C-x mapping (GUI had this)
		return true;
	default:
		break;
	}
	return false;
}


auto
KLookupEscCommand(const int ascii_key, CommandId &out) -> bool
{
	const int k = KLowerAscii(ascii_key);
	// Handle KEY_BACKSPACE (ESC BACKSPACE, Alt-Backspace)
	if (ascii_key == KEY_BACKSPACE) {
		out = CommandId::DeleteWordPrev;
		return true;
	}
	switch (k) {
	case '<':
		out = CommandId::MoveFileStart; // Esc <
		return true;
	case '>':
		out = CommandId::MoveFileEnd; // Esc >
		return true;
	case 'm':
		out = CommandId::ToggleMark; // Esc m
		return true;
	case 'w':
		out = CommandId::CopyRegion; // Esc w (Alt-w)
		return true;
	case 'b':
		out = CommandId::WordPrev;
		return true;
	case 'f':
		out = CommandId::WordNext;
		return true;
	case 'd':
		out = CommandId::DeleteWordNext; // Esc d (Alt-d)
		return true;
	case 'q':
		out = CommandId::ReflowParagraph; // Esc q (reflow paragraph)
		return true;
	default:
		break;
	}
	return false;
}
