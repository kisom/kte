#include "KKeymap.h"


auto
KLookupKCommand(const int ascii_key, const bool ctrl, CommandId &out) -> bool
{
	// Normalize to lowercase letter if applicable
	int k = KLowerAscii(ascii_key);

	if (ctrl) {
		switch (k) {
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
			break;
		}
	} else {
		switch (k) {
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
		default:
			break;
		}
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
	default:
		break;
	}
	return false;
}
