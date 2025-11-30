This is a design for a non-linear undo/redo system for kte. The design must be identical in behavior and correctness
to the proven kte editor undo system.

### Core Requirements

1. Each open buffer has its own completely independent undo tree.
2. Undo and redo must be non-linear: typing after undo creates a branch; old redo branches are discarded.
3. Typing, backspacing, and pasting are batched into word-level undo steps.
4. Undo/redo must never create new undo nodes while applying an undo/redo (silent, low-level apply).
5. The system must be memory-safe and leak-proof even if the user types and immediately closes the buffer.

### Data Structures

```cpp
enum class UndoType : uint8_t {
    Insert,
    Delete,
    Paste,        // optional, can reuse Insert
    Newline,
    DeleteRow,
    // future: IndentRegion, KillRegion, etc.
};

struct UndoNode {
    UndoType type;
    int row;           // original cursor row
    int col;           // original cursor column (updated during batch)
    std::string text;  // the inserted or deleted text (full batch)
    UndoNode* child = nullptr;   // next in current timeline
    UndoNode* next  = nullptr;   // redo branch (rarely used)
    // no parent pointer needed — we walk from root
};

struct UndoTree {
    UndoNode* root    = nullptr;  // first edit ever
    UndoNode* current = nullptr;  // current state of buffer
    UndoNode* saved   = nullptr;  // points to node matching last save (for dirty flag)
    UndoNode* pending = nullptr;  // in-progress batch (detached)
};
```

Each `Buffer` owns one `std::unique_ptr<UndoTree>`.

### Core API (must implement exactly)

```cpp
class UndoSystem {
public:
    void Begin(UndoType type);
    void Append(char ch);
    void Append(std::string_view text);
    void commit();                     // called on cursor move, commands, etc.

    void undo();                       // Ctrl+Z
    void redo();                       // Ctrl+Y or Ctrl+Shift+Z

    void mark_saved();                 // after successful save
    void discard_pending();            // before closing buffer or loading new file
    void clear();                      // new file / reset

private:
    void apply(const UndoNode* node, int direction);  // +1 = redo, -1 = undo
    void free_node(UndoNode* node);
    void free_branch(UndoNode* node);  // frees redo siblings only
};
```

### Critical Invariants and Rules

1. `begin()` must reuse `pending` if:
    - same type
    - same row
    - `pending->col + pending->text.size() == current_cursor_col`
      → otherwise `commit()` old and create new

2. `pending` is detached — never linked until `commit()`

3. `commit()`:
    - discards redo branches (`current->child`)
    - attaches `pending` as `current->child`
    - advances `current`
    - clears `pending`
    - if diverged from `saved`, null it

4. `apply()` must use low-level buffer operations:
    - Never call public insert/delete/newline
    - Use raw `buffer.insert_text(row, col, text)` and `buffer.delete_text(row, col, len)`
    - These must not trigger undo

5. `undo()`:
    - move current to parent
    - apply(current, -1)

6. `redo()`:
    - move current to child
    - apply(current, +1)

7. `discard_pending()` must be called in:
    - buffer close
    - file reload
    - new file
    - any destructive operation

### Example Flow: Typing "hello"

```text
begin(Insert) → pending = new node, col=0
append('h') → pending->text = "h", pending->col = 1
append('e') → "he", col = 2
...
commit() on arrow key → pending becomes current->child, current advances
```

One undo step removes all of "hello".

### Required Helper in Buffer Class

```cpp
class Buffer {
    void insert_text(int row, int col, std::string_view text);   // raw, no undo
    void delete_text(int row, int col, size_t len);              // raw, no undo
    void split_line(int row, int col);                           // raw newline
    void join_lines(int row);                                    // raw join
    void insert_row(int row, std::string_view text);             // raw
    void delete_row(int row);                                    // raw
};
```

### Tasks for Agent

1. Implement `UndoNode`, `UndoTree`, and `UndoSystem` class exactly as specified.
2. Add `std::unique_ptr<UndoTree> undo;` to `Buffer`.
3. Modify `insert_char`, `delete_char`, `paste`, `newline` to use `undo.begin()/append()/commit()`.
4. Add `undo.commit()` at start of all cursor movement and command functions.
5. Implement `apply()` using only `Buffer`'s raw methods.
6. Add `undo.discard_pending()` in all buffer reset/close paths.
7. Add `Ctrl+Z` → `buffer.undo()`, `Ctrl+Y` → `buffer.redo()`.

This design is used in production editors and is considered the gold standard for small, correct, non-linear undo in
C/C++. Implement it faithfully.
