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
	OpenFileStart, // begin open-file prompt
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
	WordPrev,
	WordNext,
	// Direct cursor placement
	MoveCursorTo, // arg: "y:x" (zero-based row:col)
	// Undo/Redo
	Undo,
	Redo,
	// Meta
	UnknownKCommand, // arg: single character that was not recognized after C-k
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
