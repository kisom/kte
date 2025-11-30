# TestFrontend - Headless Frontend for Testing

## Overview

`TestFrontend` is a headless implementation of the `Frontend` interface designed to facilitate programmatic testing of editor features. It allows you to queue commands and text input manually, execute them step-by-step, and inspect the editor/buffer state.

## Components

### TestInputHandler
A programmable input handler that uses a queue-based system:
- `QueueCommand(CommandId id, const std::string &arg = "", int count = 0)` - Queue a specific command
- `QueueText(const std::string &text)` - Queue text for insertion (character by character)
- `Poll(MappedInput &out)` - Returns queued commands one at a time
- `IsEmpty()` - Check if the input queue is empty

### TestRenderer
A minimal no-op renderer for testing:
- `Draw(Editor &ed)` - No-op implementation, just increments draw counter
- `GetDrawCount()` - Returns the number of times Draw() was called
- `ResetDrawCount()` - Resets the draw counter

### TestFrontend
The main frontend class that integrates TestInputHandler and TestRenderer:
- `Init(Editor &ed)` - Initializes the frontend (sets editor dimensions to 24x80)
- `Step(Editor &ed, bool &running)` - Processes one command from the queue and renders
- `Shutdown()` - Cleanup (no-op for TestFrontend)
- `Input()` - Access the TestInputHandler
- `Renderer()` - Access the TestRenderer

## Usage Example

```cpp
#include "Editor.h"
#include "TestFrontend.h"
#include "Command.h"

int main() {
    // IMPORTANT: Install default commands first!
    InstallDefaultCommands();
    
    Editor editor;
    TestFrontend frontend;
    
    // Initialize
    frontend.Init(editor);
    
    // Setup: create a buffer (open a file or add buffer)
    std::string err;
    if (!editor.OpenFile("/tmp/test.txt", err)) {
        return 1;
    }
    
    // Queue some commands
    frontend.Input().QueueText("Hello");
    frontend.Input().QueueCommand(CommandId::Newline);
    frontend.Input().QueueText("World");
    
    // Execute queued commands
    bool running = true;
    while (!frontend.Input().IsEmpty() && running) {
        frontend.Step(editor, running);
    }
    
    // Inspect results
    Buffer *buf = editor.CurrentBuffer();
    if (buf && buf->Rows().size() >= 2) {
        std::cout << "Line 1: " << std::string(buf->Rows()[0]) << "\n";
        std::cout << "Line 2: " << std::string(buf->Rows()[1]) << "\n";
    }
    
    frontend.Shutdown();
    return 0;
}
```

## Key Features

1. **Programmable Input**: Queue any sequence of commands or text programmatically
2. **Step-by-Step Execution**: Run the editor one command at a time
3. **State Inspection**: Access and verify editor/buffer state between commands
4. **No UI Dependencies**: Headless operation, no terminal or GUI required
5. **Integration Testing**: Test command sequences, undo/redo, multi-line editing, etc.

## Available Commands

All commands from `CommandId` enum can be queued, including:
- `CommandId::InsertText` - Insert text (use `QueueText()` helper)
- `CommandId::Newline` - Insert newline
- `CommandId::Backspace` - Delete character before cursor  
- `CommandId::DeleteChar` - Delete character at cursor
- `CommandId::MoveLeft`, `MoveRight`, `MoveUp`, `MoveDown` - Cursor movement
- `CommandId::Undo`, `CommandId::Redo` - Undo/redo operations
- `CommandId::Save`, `CommandId::Quit` - File operations
- And many more (see Command.h)

## Integration

TestFrontend is built into both `kte` and `kge` executables as part of the common source files. You can create standalone test programs by linking against the same source files and ncurses.

## Notes

- Always call `InstallDefaultCommands()` before using any commands
- Buffer must be initialized (via `OpenFile()` or `AddBuffer()`) before queuing edit commands
- Undo/redo requires the buffer to have an UndoSystem attached
- The test frontend sets editor dimensions to 24x80 by default
