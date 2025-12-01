/*
 * HelpText.cc - embedded/customizable help content
 */

#include "HelpText.h"


std::string
HelpText::Text()
{
	// Customize the help text here. This string will be used by C-k h first.
	// You can keep it empty to fall back to the manpage or built-in defaults.
	// Note: keep newline characters as-is; the renderer splits lines on '\n'.

	return std::string(
		"KTE - Kyle's Text Editor\n\n"
		"About:\n"
		"  kte is Kyle's Text Editor and is probably ill-suited to everyone else. It was\n"
		"  inspired by Antirez' kilo text editor by way of someone's writeup of the\n"
		"  process of writing a text editor from scratch. It has keybindings inspired by\n"
		"  VDE (and the Wordstar family) and emacs; its spiritual parent is mg(1).\n"
		"\n"
		"Core keybindings:\n"
		"  C-k '        Toggle read-only\n"
		"  C-k -        Unindent region\n"
		"  C-k =        Indent region\n"
		"  C-k C-d      Kill entire line\n"
		"  C-k C-q      Quit now (no confirm)\n"
		"  C-k a        Mark all and jump to end\n"
		"  C-k b        Switch buffer\n"
		"  C-k c        Close current buffer\n"
		"  C-k d        Kill to end of line\n"
		"  C-k e        Open file (prompt)\n"
		"  C-k g        Jump to line\n"
		"  C-k h        Show this help\n"
		"  C-k l        Reload buffer from disk\n"
		"  C-k n        Previous buffer\n"
		"  C-k o        Change working directory (prompt)\n"
		"  C-k p        Next buffer\n"
		"  C-k q        Quit (confirm if dirty)\n"
		"  C-k r        Redo\n"
		"  C-k s        Save buffer\n"
		"  C-k u        Undo\n"
		"  C-k v        Toggle visual file picker (GUI)\n"
		"  C-k w        Show working directory\n"
		"  C-k x        Save and quit\n"
		"\n"
		"ESC/Alt commands:\n"
		"  ESC q        Reflow paragraph\n"
		"  ESC BACKSPACE Delete previous word\n"
		"  ESC d        Delete next word\n"
		"  Alt-w        Copy region to kill ring\n\n"
		"Buffers:\n  +HELP+ is read-only. Press C-k ' to toggle if you need to edit; C-k h restores it.\n"
	);
}
