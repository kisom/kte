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
		"  kte is Kyle's Text Editor. It keeps a small, fast core and uses a\n"
		"  WordStar/VDE-style command model with some emacs influences.\n"
		"\n"
		"K-commands (prefix C-k):\n"
		"  C-k '        Toggle read-only\n"
		"  C-k -        Unindent region (mark required)\n"
		"  C-k =        Indent region (mark required)\n"
		"  C-k ;        Command prompt (:\\ )\n"
		"  C-k C-d      Kill entire line\n"
		"  C-k C-q      Quit now (no confirm)\n"
		"  C-k C-x      Save and quit\n"
		"  C-k a        Mark start of file, jump to end\n"
		"  C-k b        Switch buffer\n"
		"  C-k c        Close current buffer\n"
		"  C-k d        Kill to end of line\n"
		"  C-k e        Open file (prompt)\n"
		"  C-k f        Flush kill ring\n"
		"  C-k g        Jump to line\n"
		"  C-k h        Show this help\n"
		"  C-k j        Jump to mark\n"
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
		"  C-k y        Yank\n"
		"\n"
		"ESC/Alt commands:\n"
		"  ESC <        Go to beginning of file\n"
		"  ESC >        Go to end of file\n"
		"  ESC m        Toggle mark\n"
		"  ESC w        Copy region to kill ring (Alt-w)\n"
		"  ESC b        Previous word\n"
		"  ESC f        Next word\n"
		"  ESC d        Delete next word (Alt-d)\n"
		"  ESC BACKSPACE Delete previous word (Alt-Backspace)\n"
		"  ESC q        Reflow paragraph\n"
		"\n"
		"Control keys:\n"
		"  C-a C-e      Line start / end\n"
		"  C-b C-f      Move left / right\n"
		"  C-n C-p      Move down / up\n"
		"  C-d          Delete char\n"
		"  C-w / C-y    Kill region / Yank\n"
		"  C-s          Incremental find\n"
		"  C-r          Regex search\n"
		"  C-t          Regex search & replace\n"
		"  C-h          Search & replace\n"
		"  C-l / C-g    Refresh / Cancel\n"
		"  C-u [digits] Universal argument (repeat count)\n"
		"\n"
		"Buffers:\n  +HELP+ is read-only. Press C-k ' to toggle; C-k h restores it.\n"
		"\n"
		"GUI appearance (command prompt):\n"
		"  : theme NAME         Set GUI theme (eink, everforest, gruvbox, kanagawa-paper, nord, old-book, plan9, solarized, zenburn)\n"
		"  : background MODE    Set background: light | dark (affects eink, gruvbox, solarized)\n"
	);
}