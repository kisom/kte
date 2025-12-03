## Updated Undo System Plan for kte/kge

After reviewing the existing codebase and your undo plan, I propose
the following refined approach that preserves your goals while making
it more suitable for implementation:

### Refined Data Structures

The proposed data structures are sound but need some refinements:

```c++
enum class UndoType : uint8_t {
    Insert,
    Delete,
    Paste,
    Newline,
    DeleteRow,
    // Future: IndentRegion, KillRegion, etc.
};

struct UndoNode {
    UndoType type;
    int row;
    int col;
    std::string text;
    std::unique_ptr<UndoNode> child = nullptr;   // next in timeline
    std::unique_ptr<UndoNode> next  = nullptr;   // redo branch
    UndoNode* parent = nullptr;  // weak pointer for navigation
};

struct UndoTree {
    std::unique_ptr<UndoNode> root;
    UndoNode* current = nullptr;
    UndoNode* saved   = nullptr;
    std::unique_ptr<UndoNode> pending = nullptr;
};
```

Key changes:

- Use `std::unique_ptr` for owned pointers to ensure proper RAII
- Add weak `parent` pointer for easier navigation
- This ensures memory safety without manual management

---

```markdown
# Undo System Implementation Roadmap for kte/kge

This is the complete implementation plan for the non-linear undo/redo
system for kte. This document serves as a detailed
specification for Junie to implement an undo system similar to emacs'
undo-tree.

## Overview

The goal is to implement a robust, memory-safe undo system where:

1. Each buffer has its own independent undo tree
2. Undo and redo are non-linear - typing after undo creates a branch
3. Operations are batched into word-level undo steps
4. The system is leak-proof and handles buffer closure gracefully

## Phase 1: Core Data Structures

### 1.1 UndoType enum (UndoNode.h)
```

cpp enum class UndoType : uint8_t { Insert, Delete, Paste, // can
reuse Insert if preferred Newline, DeleteRow, // Future extensions:
IndentRegion, KillRegion };

```
### 1.2 UndoNode struct (UndoNode.h)
```

cpp struct UndoNode { UndoType type; int row; // original cursor row
int col; // original cursor column (updated during batch) std::string
text; // the inserted or deleted text (full batch)
std::unique_ptr<UndoNode> child = nullptr; // next in current timeline
std::unique_ptr<UndoNode> next = nullptr; // redo branch (rarely used)
UndoNode* parent = nullptr; // weak pointer for navigation };

```
### 1.3 UndoTree struct (UndoTree.h)
```

cpp struct UndoTree { std::unique_ptr<UndoNode> root; // first edit
ever UndoNode* current = nullptr; // current state of buffer UndoNode*
saved = nullptr; // points to node matching last save
std::unique_ptr<UndoNode> pending = nullptr; // in-progress batch };

```
### 1.4 UndoSystem class (UndoSystem.h)
```

cpp class UndoSystem { private: std::unique_ptr<UndoTree> tree;

public: UndoSystem(); ~UndoSystem() = default;

    // Core batching API
    void begin(UndoType type, int row, int col);
    void append(char ch);
    void append(std::string_view text);
    void commit();

    // Undo/Redo operations
    void undo(class Buffer& buffer);
    void redo(class Buffer& buffer);

    // State management
    void mark_saved();
    void discard_pending();
    void clear();

    // Query methods
    bool can_undo() const;
    bool can_redo() const;
    bool is_dirty() const;

private: void apply_node(Buffer& buffer, const UndoNode* node, int
direction); bool should_batch_with_pending(UndoType type, int row, int
col) const; void attach_pending_to_current(); void
discard_redo_branches(); };

```
## Phase 2: Buffer Integration

### 2.1 Add undo system to Buffer class (Buffer.h)
Add to Buffer class:
```

cpp private: std::unique_ptr<UndoSystem> undo_system; bool
applying_undo = false; // prevent recursive undo during apply

public: // Raw operations (don't trigger undo) void
raw_insert_text(int row, int col, std::string_view text); void
raw_delete_text(int row, int col, size_t len); void raw_split_line(int
row, int col); void raw_join_lines(int row); void raw_insert_row(int
row, std::string_view text); void raw_delete_row(int row);

    // Undo/Redo public API
    void undo();
    void redo();
    bool can_undo() const;
    bool can_redo() const;
    void mark_saved();
    bool is_dirty() const;

```
### 2.2 Modify existing Buffer operations (Buffer.cc)
For each user-facing operation (`insert_char`, `delete_char`, etc.):

1. **Before performing operation**: Call `undo_system->commit()` if cursor moved
2. **Begin batching**: Call `undo_system->begin(type, row, col)`
3. **Record change**: Call `undo_system->append()` with the affected text
4. **Perform operation**: Execute the actual buffer modification
5. **Auto-commit conditions**: Commit on cursor movement, command execution

Example pattern:
```

cpp void Buffer::insert_char(char ch) { if (applying_undo) return; //
silent during undo application

    // Auto-commit if cursor moved significantly or type changed
    if (should_commit_before_insert()) {
        undo_system->commit();
    }

    undo_system->begin(UndoType::Insert, cursor_row, cursor_col);
    undo_system->append(ch);

    // Perform actual insertion
    raw_insert_text(cursor_row, cursor_col, std::string(1, ch));
    cursor_col++;

}

```
### 2.3 Commit triggers
Auto-commit `pending` operations when:
- Cursor moves (arrow keys, mouse click)
- Any command starts executing
- Buffer switching
- Before undo/redo operations
- Before file save/close

## Phase 3: UndoSystem Implementation

### 3.1 Core batching logic (UndoSystem.cc)
```

cpp void UndoSystem::begin(UndoType type, int row, int col) { if
(should_batch_with_pending(type, row, col)) { // Continue existing
batch return; }

    // Commit any existing pending operation
    if (pending) {
        commit();
    }

    // Create new pending node
    pending = std::make_unique<UndoNode>();
    pending->type = type;
    pending->row = row;
    pending->col = col;
    pending->text.clear();

}

bool UndoSystem::should_batch_with_pending(UndoType type, int row, int
col) const { if (!pending) return false; if (pending->type != type)
return false; if (pending->row != row) return false;

    // For Insert: check if we're continuing at the right position
    if (type == UndoType::Insert) {
        return (pending->col + pending->text.size()) == col;
    }

    // For Delete: check if we're continuing from the same position
    if (type == UndoType::Delete) {
        return pending->col == col;
    }

    return false;

}

```
### 3.2 Commit logic
```

cpp void UndoSystem::commit() { if (!pending || pending->text.empty())
{ pending.reset(); return; }

    // Discard any redo branches from current position
    discard_redo_branches();

    // Attach pending as child of current
    attach_pending_to_current();

    // Move current forward
    current = pending.release();
    if (current->parent) {
        current->parent->child.reset(current);
    }

    // Update saved pointer if we diverged
    if (saved && saved != current) {
        // Check if saved is still reachable from current
        if (!is_ancestor_of(current, saved)) {
            saved = nullptr;
        }
    }

}

```
### 3.3 Apply operations
```

cpp void UndoSystem::apply_node(Buffer& buffer, const UndoNode* node,
int direction) { if (!node) return;

    switch (node->type) {
        case UndoType::Insert:
            if (direction > 0) {  // redo
                buffer.raw_insert_text(node->row, node->col, node->text);
            } else {  // undo
                buffer.raw_delete_text(node->row, node->col, node->text.size());
            }
            break;

        case UndoType::Delete:
            if (direction > 0) {  // redo
                buffer.raw_delete_text(node->row, node->col, node->text.size());
            } else {  // undo
                buffer.raw_insert_text(node->row, node->col, node->text);
            }
            break;

        case UndoType::Newline:
            if (direction > 0) {  // redo
                buffer.raw_split_line(node->row, node->col);
            } else {  // undo
                buffer.raw_join_lines(node->row);
            }
            break;

        // Handle other types...
    }

}

```
## Phase 4: Command Integration

### 4.1 Add undo/redo commands (Command.cc)
Register the undo/redo commands in the command system:
```

cpp // In InstallDefaultCommands() CommandRegistry::Register({
CommandId::Undo, "undo", "Undo the last change", [](CommandContext&
ctx) { auto& editor = ctx.editor; auto* buffer =
editor.current_buffer(); if (buffer && buffer->can_undo()) {
buffer->undo(); return true; } return false; }, false // not public
command });

CommandRegistry::Register({ CommandId::Redo, "redo", "Redo the last
undone change", [](CommandContext& ctx) { auto& editor = ctx.editor;
auto* buffer = editor.current_buffer(); if (buffer &&
buffer->can_redo()) { buffer->redo(); return true; } return false; },
false // not public command });

```
### 4.2 Update keybinding handlers
Ensure the input handlers map `C-k u` to `CommandId::Undo` and `C-k r`
to `CommandId::Redo`.

## Phase 5: Memory Management and Edge Cases

### 5.1 Buffer lifecycle management
- **Constructor**: Initialize `undo_system = std::make_unique<UndoSystem>()`
- **Destructor**: `undo_system.reset()` (automatic)
- **File reload**: Call `undo_system->clear()` before loading
- **New file**: Call `undo_system->clear()`
- **Close buffer**: Call `undo_system->discard_pending()` then let destructor handle cleanup

### 5.2 Save state tracking
- **After successful save**: Call `buffer->mark_saved()`
- **For dirty flag**: Use `buffer->is_dirty()`

### 5.3 Edge case handling
- Prevent undo during undo application (`applying_undo` flag)
- Handle empty operations gracefully
- Ensure cursor positioning after undo/redo
- Test memory leaks with rapid typing + buffer close

## Phase 6: Testing

### 6.1 Unit tests (test_undo.cc)
Create comprehensive tests covering:
- Basic typing and undo
- Word-level batching
- Non-linear undo (type, undo, type different text)
- Memory leak testing
- Save state tracking
- Edge cases (empty buffers, large operations)

### 6.2 Integration tests
- Test with all buffer implementations (GapBuffer, PieceTable)
- Test with GUI and Terminal frontends
- Test rapid typing + immediate buffer close
- Test file reload during pending operations

## Implementation Priority

1. **Phase 1**: Implement core data structures
2. **Phase 2**: Add Buffer integration points
3. **Phase 3**: Implement UndoSystem methods
4. **Phase 4**: Wire up commands and keybindings
5. **Phase 5**: Handle edge cases and memory management
6. **Phase 6**: Comprehensive testing

## Critical Success Criteria

- ✅ No memory leaks even with rapid typing + buffer close
- ✅ Batching works correctly (word-level undo steps)
- ✅ Non-linear undo creates branches correctly
- ✅ Save state tracking works properly
- ✅ Silent operations during undo application
- ✅ Clean integration with existing Buffer operations

This roadmap provides Junie with a complete, step-by-step implementation plan that preserves the original design goals while ensuring robust, memory-safe implementation.
```

This roadmap refines your original plan by:

1. **Memory Safety**: Uses `std::unique_ptr` for automatic memory
   management
2. **Clear Implementation Steps**: Breaks down into logical phases
3. **Integration Points**: Clearly identifies where to hook into
   existing code
4. **Edge Case Handling**: Addresses buffer lifecycle and error
   conditions
5. **Testing Strategy**: Ensures robust validation

The core design remains faithful to your emacs-style undo tree vision
while being practical for implementation by Junie.
