/*
 * KKeymap.h - mapping for k-command (C-k prefix) keys to CommandId
 */
#ifndef KTE_KKEYMAP_H
#define KTE_KKEYMAP_H

#include "Command.h"

// Lookup the command to execute after a C-k prefix.
// Parameters:
//  - ascii_key: ASCII code of the key, preferably lowercased if it's a letter.
//  - ctrl: whether Control modifier was held for this key (e.g., C-k C-x).
// Returns true and sets out if a mapping exists; false otherwise.
bool KLookupKCommand(int ascii_key, bool ctrl, CommandId &out);

// Lookup direct Control-chord commands (e.g., C-n, C-p, C-f, ...).
// ascii_key should be the lowercase ASCII of the letter (e.g., 'n' for C-n).
bool KLookupCtrlCommand(int ascii_key, CommandId &out);

// Lookup ESC/Meta + key commands (e.g., ESC f/b).
// ascii_key should be the lowercase ASCII of the letter.
bool KLookupEscCommand(int ascii_key, CommandId &out);

// Utility: normalize an int keycode to lowercased ASCII if it's in printable range.
inline int
KLowerAscii(const int key)
{
	if (key >= 'A' && key <= 'Z')
		return key + ('a' - 'A');
	return key;
}

#endif // KTE_KKEYMAP_H
