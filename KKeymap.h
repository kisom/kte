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

// Utility: normalize an int keycode to lowercased ASCII if it's in printable range.
inline int
KLowerAscii(const int key)
{
	if (key >= 'A' && key <= 'Z')
		return key + ('a' - 'A');
	return key;
}

#endif // KTE_KKEYMAP_H
