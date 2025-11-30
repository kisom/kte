#include "KKeymap.h"


auto
KLookupKCommand(const int ascii_key, const bool ctrl, CommandId &out) -> bool
{
	// Normalize to lowercase letter if applicable
	int k = KLowerAscii(ascii_key);

	if (ctrl) {
		switch (k) {
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
		case 's':
			out = CommandId::Save;
			return true; // C-k s
		case 'e':
			out = CommandId::OpenFileStart;
			return true; // C-k e (open file)
		case 'x':
			out = CommandId::SaveAndQuit;
			return true; // C-k x
		case 'q':
			out = CommandId::Quit;
			return true; // C-k q
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
