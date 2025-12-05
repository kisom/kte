#include <iostream>
#include <ncurses.h>
#include <ostream>

#include "KKeymap.h"


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
			return true;
		case 'q':
			out = CommandId::QuitNow;
			return true;
		case 'x':
			out = CommandId::SaveAndQuit;
			return true;
		default:
			return false;
		}
	}

	// 2) Case-sensitive bindings must be checked before case-insensitive table.
	if (ascii_key == 'r') {
		out = CommandId::Redo; // C-k r (redo)
		return true;
	}
	if (ascii_key == '\'') {
		out = CommandId::ToggleReadOnly; // C-k ' (toggle read-only)
		return true;
	}

	switch (k_lower) {
	case 'a':
		out = CommandId::MarkAllAndJumpEnd;
		return true;
	case 'k':
		out = CommandId::CenterOnCursor; // C-k k center current line
		return true;
	case 'b':
		out = CommandId::BufferSwitchStart;
		return true;
	case 'c':
		out = CommandId::BufferClose;
		return true;
	case 'd':
		out = CommandId::KillToEOL;
		return true;
	case 'e':
		out = CommandId::OpenFileStart;
		return true;
	case 'E':
		std::cerr << "E is not a valid command" << std::endl;
		return false;
	case 'f':
		out = CommandId::FlushKillRing;
		return true;
	case 'g':
		out = CommandId::JumpToLine;
		return true;
	case 'h':
		out = CommandId::ShowHelp;
		return true;
	case 'j':
		out = CommandId::JumpToMark;
		return true;
	case 'l':
		out = CommandId::ReloadBuffer;
		return true;
	case 'n':
		out = CommandId::BufferPrev;
		return true;
	case 'o':
		out = CommandId::ChangeWorkingDirectory;
		return true;
	case 'p':
		out = CommandId::BufferNext;
		return true;
	case 'q':
		out = CommandId::Quit;
		return true;
	case 's':
		out = CommandId::Save;
		return true;
	case 'u':
		out = CommandId::Undo;
		return true;
	case 'v':
		out = CommandId::VisualFilePickerToggle;
		return true;
	case 'w':
		out = CommandId::ShowWorkingDirectory;
		return true;
	case 'x':
		out = CommandId::SaveAndQuit;
		return true;
	case 'y':
		out = CommandId::Yank;
		return true;
	case '-':
		out = CommandId::UnindentRegion;
		return true;
	case '=':
		out = CommandId::IndentRegion;
		return true;
	case ';':
		out = CommandId::CommandPromptStart; // C-k ; : generic command prompt
		return true;
	default:
		break;
	}

	// 3) Non-control k-table (lowercased)
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
	case 'r':
		out = CommandId::RegexFindStart; // C-r regex search
		return true;
	case 't':
		out = CommandId::RegexpReplace; // C-t regex search & replace
		return true;
	case 'h':
		out = CommandId::SearchReplace; // C-h: search & replace
		return true;
	case 'l':
		out = CommandId::Refresh;
		return true;
	case 'g':
		out = CommandId::Refresh;
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