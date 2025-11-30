/*
 * Command.h - command model and registry for editor actions
 */
#ifndef KTE_COMMAND_H
#define KTE_COMMAND_H

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Editor;

// Identifiers for editor commands. This is intentionally small for now and
// will grow as features are implemented.
enum class CommandId {
    Save,
    SaveAs,
    // Placeholders for future commands from ke's model
    Quit,
    SaveAndQuit,
    Refresh,    // force redraw
    KPrefix,    // show "C-k _" prompt in status when entering k-command
    FindStart,  // begin incremental search (placeholder)
    // Editing
    InsertText,   // arg: text to insert at cursor (UTF-8, no newlines)
    Newline,      // insert a newline at cursor
    Backspace,    // delete char before cursor (may join lines)
    DeleteChar,   // delete char at cursor (may join lines)
    // Navigation (basic)
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    MoveHome,
    MoveEnd,
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
