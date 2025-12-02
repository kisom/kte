/*
 * Command.h - command model and registry for editor actions
 */
#ifndef KTE_COMMAND_H
#define KTE_COMMAND_H

#include <functional>
#include <string>
#include <vector>


class Editor;

// Identifiers for editor commands. This is intentionally small for now and
// will grow as features are implemented.
enum class CommandId {
	Save,
	SaveAs,
	// Placeholders for future commands from ke's model
	Quit,
	QuitNow, // immediate quit, no confirmation
	SaveAndQuit,
	Refresh, // force redraw
	KPrefix, // show "C-k _" prompt in status when entering k-command
	FindStart, // begin incremental search (placeholder)
	RegexFindStart, // begin regex search (C-r)
	RegexpReplace, // begin regex search & replace (C-t)
	SearchReplace, // begin search & replace (two-step prompt)
	OpenFileStart, // begin open-file prompt
	VisualFilePickerToggle,
	// Buffers
	BufferSwitchStart, // begin buffer switch prompt
	BufferClose,
	BufferNext,
	BufferPrev,
	// Editing
	InsertText, // arg: text to insert at cursor (UTF-8, no newlines)
	Newline, // insert a newline at cursor
	Backspace, // delete char before cursor (may join lines)
	DeleteChar, // delete char at cursor (may join lines)
	KillToEOL, // delete from cursor to end of line; if at EOL, delete newline
	KillLine, // delete the entire current line (including newline)
	Yank, // insert most recently killed text at cursor
	// Region/mark and file-boundary navigation
	MoveFileStart, // move to beginning of file
	MoveFileEnd, // move to end of file
	ToggleMark, // toggle mark at cursor
	JumpToMark, // jump to mark, set mark to previous cursor
	KillRegion, // kill region between mark and cursor (to kill ring)
	CopyRegion, // copy region to kill ring (Alt-w)
	FlushKillRing, // clear kill ring
	// Navigation (basic)
	MoveLeft,
	MoveRight,
	MoveUp,
	MoveDown,
	MoveHome,
	MoveEnd,
	PageUp,
	PageDown,
	ScrollUp, // scroll viewport up (towards beginning) without moving cursor
	ScrollDown, // scroll viewport down (towards end) without moving cursor
	WordPrev,
	WordNext,
	DeleteWordPrev, // delete previous word (ESC BACKSPACE)
	DeleteWordNext, // delete next word (ESC d)
	// Direct cursor placement
	MoveCursorTo, // arg: "y:x" (zero-based row:col)
	// Undo/Redo
	Undo,
	Redo,
	// UI/status helpers
	UArgStatus, // update status line during universal-argument collection
	// Themes (GUI)
	ThemeNext,
	ThemePrev,
	// Region formatting
	IndentRegion, // indent region (C-k =)
	UnindentRegion, // unindent region (C-k -)
	ReflowParagraph, // reflow paragraph to column width (ESC q)
	// Read-only buffers
	ToggleReadOnly, // toggle current buffer read-only (C-k ')
	// Buffer operations
	ReloadBuffer, // reload buffer from disk (C-k l)
	MarkAllAndJumpEnd, // set mark at beginning, jump to end (C-k a)
	// Direct navigation by line number
	JumpToLine, // prompt for line and jump (C-k g)
	ShowWorkingDirectory, // Display the current working directory in the editor message.
	ChangeWorkingDirectory, // Change the editor's current directory.
	// Help
	ShowHelp, // open +HELP+ buffer with manual text (C-k h)
	// Meta
	UnknownKCommand, // arg: single character that was not recognized after C-k
	// Generic command prompt
	CommandPromptStart, // begin generic command prompt (C-k ;)
	// Theme by name
	ThemeSetByName,
	// Background mode (GUI)
	BackgroundSet,
	// Syntax highlighting
	Syntax, // ":syntax on|off|reload"
	SetOption, // generic ":set key=value" (v1: filetype=<lang>)
};


// Context passed to command handlers.
struct CommandContext {
	Editor &editor;

	// Optional argument string (e.g., filename for SaveAs).
	std::string arg;

	// Optional repeat count (C-u support). 0 means not provided.
	int count = 0;
};


using CommandHandler = std::function<bool(CommandContext &)>; // return true on success


struct Command {
	CommandId id;
	std::string name; // stable, unique name (e.g., "save", "save-as")
	std::string help; // short help/description
	CommandHandler handler;
	// Public commands are exposed in the ": " prompt (C-k ;)
	bool isPublic = false;
};


// Simple global registry. Not thread-safe; suitable for this app.
class CommandRegistry {
public:
	static void Register(const Command &cmd);

	static const Command *FindById(CommandId id);

	static const Command *FindByName(const std::string &name);

	static const std::vector<Command> &All();

private:
	static std::vector<Command> &storage_();
};


// Built-in command installers
void InstallDefaultCommands();

// Dispatcher entry points for the input layer
// Returns true if the command executed successfully.
bool Execute(Editor &ed, CommandId id, const std::string &arg = std::string(), int count = 0);

bool Execute(Editor &ed, const std::string &name, const std::string &arg = std::string(), int count = 0);

#endif // KTE_COMMAND_H