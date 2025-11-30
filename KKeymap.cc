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
			out = CommandId::Quit;
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
