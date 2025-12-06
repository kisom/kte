# KTE Rust Rewrite - Complete Specification

## Overview

This document provides a complete specification for rewriting **kte** (
Kyle's Text Editor) from C++17/20 to Rust. The rewrite aims to preserve
all existing functionality while leveraging Rust's memory safety, modern
type system, and ecosystem.

### Goals

1. **Memory Safety**: Eliminate undefined behavior and memory bugs
   through Rust's ownership system
2. **Maintainability**: Clearer error handling with `Result<T, E>` and
   better code organization
3. **Performance**: Match or exceed C++ performance using zero-cost
   abstractions
4. **Feature Parity**: Preserve all existing features, keybindings, and
   user experience
5. **Modern Infrastructure**: Leverage Rust ecosystem (cargo, crates.io,
   robust testing)

### Non-Goals

- Changing the UI/UX or keybinding scheme
- Adding new features during the initial rewrite
- Supporting platforms not currently supported
- Breaking compatibility with existing configuration expectations

## Architecture Overview

The editor follows a layered architecture:

```
┌─────────────────────────────────────────────┐
│           Frontend Layer                     │
│  (Terminal/GUI - ncurses/crossterm/egui)    │
├─────────────────────────────────────────────┤
│           Application Layer                  │
│  (Editor state, Command dispatch)           │
├─────────────────────────────────────────────┤
│           Buffer Layer                       │
│  (Text storage, Undo, Syntax)               │
├─────────────────────────────────────────────┤
│           Infrastructure Layer               │
│  (I/O, Swap files, Search)                  │
└─────────────────────────────────────────────┘
```

## Core Data Structures

### 1. Text Storage Layer

#### 1.1 PieceTable - Core Text Storage

**Current C++ Implementation:**

- Each buffer contains a single `PieceTable` as the source of truth
- A `Line` cache is rebuilt from the `PieceTable` for compatibility with
  existing code
- Operations work on the unified `PieceTable` content

**Rust Design:**

The new design uses a **single piece table per buffer** that stores all
text content. The piece table tracks newlines to provide efficient
line-based operations while maintaining a unified text representation.

```rust
pub struct PieceTable {
    // Immutable original content (loaded from file)
    original: Vec<u8>,
    
    // Append-only buffer for all edits
    add: Vec<u8>,
    
    // Sequence of pieces describing the logical text
    pieces: Vec<Piece>,
    
    // Line index: maps line numbers to (piece_idx, offset_in_piece, byte_offset_in_buffer)
    // Rebuilt when needed for efficient line-based access
    line_index: Vec<LineInfo>,
    line_index_dirty: bool,
    
    // Total size in bytes
    total_size: usize,
}

#[derive(Clone, Copy)]
struct Piece {
    source: PieceSource,
    start: usize,   // byte offset in source buffer
    len: usize,     // length in bytes
}

#[derive(Clone, Copy)]
struct LineInfo {
    piece_idx: usize,      // which piece contains the start of this line
    offset_in_piece: usize, // byte offset within that piece
    byte_offset: usize,     // absolute byte offset from buffer start
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum PieceSource {
    Original,
    Add,
}

impl PieceTable {
    pub fn new() -> Self;
    pub fn from_bytes(data: &[u8]) -> Self;
    
    // Core editing operations (byte-based)
    pub fn insert(&mut self, byte_offset: usize, text: &str);
    pub fn delete(&mut self, byte_offset: usize, len: usize);
    pub fn clear(&mut self);
    
    // Line-based operations
    pub fn line_count(&self) -> usize;
    pub fn line(&self, line_num: usize) -> Option<String>;
    pub fn line_range(&self, line_num: usize) -> Option<(usize, usize)>; // returns (start_byte, end_byte)
    pub fn insert_at_line(&mut self, row: usize, col: usize, text: &str);
    pub fn delete_at_line(&mut self, row: usize, col: usize, len: usize);
    
    // Queries
    pub fn byte_len(&self) -> usize;
    pub fn to_string(&self) -> String;
    pub fn get_range(&self, start: usize, len: usize) -> String;
    
    // Position conversion
    pub fn byte_offset_to_line_col(&self, byte_offset: usize) -> (usize, usize);
    pub fn line_col_to_byte_offset(&self, row: usize, col: usize) -> usize;
    
    // Internal maintenance
    fn rebuild_line_index(&mut self);
    fn invalidate_line_index(&mut self);
}
```

**Implementation Notes:**

- **Line Index**: The `line_index` is a cached structure mapping line
  numbers to positions in the piece table. It's rebuilt lazily when
  line-based operations are performed after edits.
- **Newline Tracking**: The line index is constructed by scanning pieces
  for `\n` characters.
- **Insert Operation**: Splits the piece at the insertion point and
  inserts a new piece referencing the `add` buffer.
- **Delete Operation**: Splits pieces at deletion boundaries and removes
  or truncates pieces as needed.
- **Memory Efficiency**: The `add` buffer never shrinks (append-only),
  but the piece table can reuse ranges efficiently.
- **Performance**: Line-based access is O(1) with valid index, O(n) to
  rebuild index. Edits are O(pieces) which is typically small.
- **UTF-8**: All operations respect UTF-8 boundaries. Column positions
  are byte-based within lines.

### 2. Buffer Management

#### 2.1 Buffer

**Current C++ Implementation:**

- Vector of `Line` objects (`rows_`)
- Cursor state: `curx_`, `cury_`, `rx_` (render x for tabs)
- Viewport: `rowoffs_`, `coloffs_`
- Mark: `mark_set_`, `mark_curx_`, `mark_cury_`
- Metadata: `filename_`, `is_file_backed_`, `dirty_`, `read_only_`
- Version counter for change tracking
- Owned `UndoSystem` and `UndoTree`
- Optional `HighlighterEngine`
- Raw pointer to `SwapRecorder`

**Rust Design:**

```rust
pub struct Buffer {
    // Text content - single piece table for entire buffer
    content: PieceTable,
    
    // Cursor state
    cursor: Cursor,
    
    // Viewport
    viewport: Viewport,
    
    // Mark (region selection)
    mark: Option<Position>,
    
    // File metadata
    metadata: BufferMetadata,
    
    // Version tracking
    version: u64,
    
    // Undo system
    undo: UndoSystem,
    
    // Syntax highlighting
    highlighter: Option<Box<dyn HighlighterEngine>>,
    
    // Swap file recording (weak reference or Option<Arc<Mutex<SwapManager>>>)
    swap_recorder: Option<Weak<Mutex<SwapRecorder>>>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Position {
    pub row: usize,
    pub col: usize,
}

#[derive(Clone, Copy, Debug)]
pub struct Cursor {
    pub x: usize,       // column in buffer (not visual)
    pub y: usize,       // row
    pub rx: usize,      // rendered x (accounts for tabs)
}

#[derive(Clone, Copy, Debug)]
pub struct Viewport {
    pub row_offset: usize,
    pub col_offset: usize,
}

pub struct BufferMetadata {
    pub filename: Option<PathBuf>,
    pub is_file_backed: bool,
    pub dirty: bool,
    pub read_only: bool,
    pub filetype: Option<String>,
    pub syntax_enabled: bool,
}

impl Buffer {
    pub fn new() -> Self;
    pub fn from_file(path: impl AsRef<Path>) -> Result<Self, BufferError>;
    
    // File I/O
    pub fn open(&mut self, path: impl AsRef<Path>) -> Result<(), BufferError>;
    pub fn save(&self) -> Result<(), BufferError>;
    pub fn save_as(&mut self, path: impl AsRef<Path>) -> Result<(), BufferError>;
    
    // Cursor management
    pub fn cursor(&self) -> Cursor;
    pub fn set_cursor(&mut self, x: usize, y: usize);
    pub fn set_render_x(&mut self, rx: usize);
    
    // Viewport
    pub fn viewport(&self) -> Viewport;
    pub fn set_viewport(&mut self, row_offset: usize, col_offset: usize);
    
    // Mark (region)
    pub fn mark(&self) -> Option<Position>;
    pub fn set_mark(&mut self, pos: Position);
    pub fn clear_mark(&mut self);
    
    // Content access
    pub fn line_count(&self) -> usize;
    pub fn line(&self, line_num: usize) -> Option<String>;
    pub fn content(&self) -> &PieceTable;
    pub fn content_mut(&mut self) -> &mut PieceTable;
    
    // Metadata
    pub fn filename(&self) -> Option<&Path>;
    pub fn is_dirty(&self) -> bool;
    pub fn set_dirty(&mut self, dirty: bool);
    pub fn is_read_only(&self) -> bool;
    pub fn set_read_only(&mut self, read_only: bool);
    pub fn toggle_read_only(&mut self);
    
    // Version tracking
    pub fn version(&self) -> u64;
    fn increment_version(&mut self);
    
    // Raw editing operations (used by undo system)
    // These delegate to the piece table's line-based operations
    pub fn insert_text(&mut self, row: usize, col: usize, text: &str);
    pub fn delete_text(&mut self, row: usize, col: usize, len: usize);
    pub fn split_line(&mut self, row: usize, col: usize);
    pub fn join_lines(&mut self, row: usize);
    pub fn insert_row(&mut self, row: usize, text: &str);
    pub fn delete_row(&mut self, row: usize);
    
    // Undo system access
    pub fn undo(&mut self) -> &mut UndoSystem;
    
    // Syntax highlighting
    pub fn set_syntax_enabled(&mut self, enabled: bool);
    pub fn set_filetype(&mut self, filetype: String);
    pub fn highlighter(&self) -> Option<&dyn HighlighterEngine>;
    pub fn highlighter_mut(&mut self) -> Option<&mut Box<dyn HighlighterEngine>>;
    pub fn ensure_highlighter(&mut self);
    
    // Conversion
    pub fn to_string(&self) -> String;
}

#[derive(Debug, thiserror::Error)]
pub enum BufferError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    
    #[error("UTF-8 error: {0}")]
    Utf8(#[from] std::string::FromUtf8Error),
    
    #[error("No filename specified")]
    NoFilename,
    
    #[error("Buffer is read-only")]
    ReadOnly,
}
```

**Implementation Notes:**

- Use `PathBuf` instead of `String` for filenames
- Separate concerns with nested structs (Cursor, Viewport, Metadata)
- `thiserror` crate for error types
- Version increments on any content change
- All text editing operations delegate to `PieceTable` methods
- `line_count()` returns `self.content.line_count()`
- `line(n)` returns `self.content.line(n)`
- `insert_text()`, `delete_text()`, etc. call corresponding PieceTable
  methods and increment version
- Copy/clone of Buffer requires cloning the PieceTable (efficient due to
  shared `original` buffer)

### 3. Undo System

#### 3.1 UndoNode and UndoTree

**Current C++ Implementation:**

- `UndoNode`: struct with type, row, col, text, child (timeline), next (
  redo branch)
- `UndoTree`: struct with root, current, saved, pending pointers
- `UndoType`: enum (Insert, Delete, Paste, Newline, DeleteRow)
- Manual memory management with raw pointers

**Rust Design:**

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UndoType {
    Insert,
    Delete,
    Paste,
    Newline,
    DeleteRow,
}

pub struct UndoNode {
    undo_type: UndoType,
    row: usize,
    col: usize,
    text: String,
    
    // Tree structure using indices instead of raw pointers
    child: Option<NodeId>,  // next in current timeline
    next: Option<NodeId>,   // redo branch sibling
}

type NodeId = usize;

pub struct UndoTree {
    nodes: Vec<UndoNode>,
    root: Option<NodeId>,
    current: Option<NodeId>,
    saved: Option<NodeId>,
    pending: Option<NodeId>,
}

impl UndoTree {
    pub fn new() -> Self;
    
    pub fn allocate_node(&mut self, node: UndoNode) -> NodeId;
    pub fn get_node(&self, id: NodeId) -> Option<&UndoNode>;
    pub fn get_node_mut(&mut self, id: NodeId) -> Option<&mut UndoNode>;
    
    pub fn root(&self) -> Option<NodeId>;
    pub fn current(&self) -> Option<NodeId>;
    pub fn saved(&self) -> Option<NodeId>;
    pub fn pending(&self) -> Option<NodeId>;
    
    pub fn set_root(&mut self, id: Option<NodeId>);
    pub fn set_current(&mut self, id: Option<NodeId>);
    pub fn set_saved(&mut self, id: Option<NodeId>);
    pub fn set_pending(&mut self, id: Option<NodeId>);
    
    pub fn clear(&mut self);
}
```

**Implementation Notes:**

- Use index-based tree instead of raw pointers for memory safety
- Consider `slotmap` or `generational-arena` crates for stable IDs with
  recycling
- Nodes are never deleted individually, only entire branches
- Alternative: use `Rc<RefCell<UndoNode>>` for pointer-like semantics

#### 3.2 UndoSystem

**Current C++ Implementation:**

- Owns reference to `Buffer` and `UndoTree`
- Transaction-based: `Begin()`, `Append()`, `commit()`
- Applies inverse operations on undo/redo
- Tracks saved state for dirty flag
- Debug logging with `KTE_UNDO_DEBUG`

**Rust Design:**

```rust
pub struct UndoSystem {
    tree: UndoTree,
    // No direct buffer reference - operations passed back to caller
}

impl UndoSystem {
    pub fn new() -> Self;
    
    // Transaction management
    pub fn begin(&mut self, undo_type: UndoType);
    pub fn append(&mut self, ch: char);
    pub fn append_str(&mut self, text: &str);
    pub fn commit(&mut self, row: usize, col: usize);
    pub fn discard_pending(&mut self);
    
    // Undo/Redo operations return the operation to apply
    pub fn undo(&mut self) -> Option<UndoOperation>;
    pub fn redo(&mut self) -> Option<UndoOperation>;
    
    // Saved state management
    pub fn mark_saved(&mut self);
    pub fn is_dirty(&self) -> bool;
    
    pub fn clear(&mut self);
}

#[derive(Debug, Clone)]
pub enum UndoOperation {
    Insert { row: usize, col: usize, text: String },
    Delete { row: usize, col: usize, len: usize },
    SplitLine { row: usize, col: usize },
    JoinLines { row: usize },
    InsertRow { row: usize, text: String },
    DeleteRow { row: usize },
}

impl UndoOperation {
    /// Returns the inverse operation
    pub fn inverse(&self, buffer: &Buffer) -> Self;
    
    /// Apply this operation to a buffer
    pub fn apply(&self, buffer: &mut Buffer);
}
```

**Implementation Notes:**

- Separate undo logic from buffer mutations for cleaner architecture
- `UndoOperation` enum represents all reversible operations
- Buffer doesn't need mutable reference to undo system during apply
- Consider logging with `tracing` crate instead of compile-time flags
- Transaction batching: accumulate text in pending node, commit
  atomically

## Application Layer

### 4. Editor State

**Current C++ Implementation:**

- Manages vector of `Buffer` objects
- Tracks current buffer index
- UI state: dimensions (rows/cols), mode, status message, status
  timestamp
- Kill ring: vector of strings with push/append/prepend operations
- Universal argument: uarg, ucount
- Search state: active, query, match position, origin position, search
  index
- Prompt state: kind, label, text, pending overwrite path
- Flags: quit_requested, quit_confirm_pending, close_confirm_pending,
  etc.
- Owned `SwapManager`
- GUI-specific: file picker visibility/directory, replace find/with temp

**Rust Design:**

```rust
pub struct Editor {
    // Buffer management
    buffers: Vec<Buffer>,
    current_buffer_index: usize,
    
    // UI dimensions
    dimensions: Dimensions,
    
    // Mode and state
    mode: EditorMode,
    status: StatusLine,
    
    // Kill ring (clipboard)
    kill_ring: KillRing,
    
    // Universal argument
    universal_arg: UniversalArg,
    
    // Search state
    search: SearchState,
    
    // Prompt state
    prompt: Option<PromptState>,
    
    // Quit/close state
    quit_requested: bool,
    quit_confirm_pending: bool,
    close_confirm_pending: bool,
    close_after_save: bool,
    
    // Swap file manager
    swap_manager: Arc<Mutex<SwapManager>>,
    
    // GUI-specific state
    file_picker: FilePickerState,
    replace_state: ReplaceState,
}

#[derive(Debug, Clone, Copy)]
pub struct Dimensions {
    pub rows: usize,
    pub cols: usize,
}

impl Dimensions {
    pub fn content_rows(&self) -> usize {
        self.rows.saturating_sub(2)  // Reserve for status + message line
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EditorMode {
    Normal,
    KCommand,      // After C-k prefix
    EscCommand,    // After ESC prefix
    UniversalArg,  // During C-u sequence
}

pub struct StatusLine {
    message: String,
    timestamp: std::time::Instant,
}

impl StatusLine {
    pub fn set(&mut self, message: impl Into<String>);
    pub fn get(&self) -> &str;
    pub fn elapsed(&self) -> std::time::Duration;
}

pub struct KillRing {
    entries: Vec<String>,
    max_entries: usize,
    kill_chain: bool,
    no_kill: bool,
}

impl KillRing {
    pub fn new() -> Self;
    pub fn push(&mut self, text: String);
    pub fn append(&mut self, text: &str);
    pub fn prepend(&mut self, text: &str);
    pub fn head(&self) -> Option<&str>;
    pub fn clear(&mut self);
    pub fn set_chain(&mut self, enabled: bool);
    pub fn set_no_kill(&mut self, enabled: bool);
}

pub struct UniversalArg {
    value: i32,
    count: i32,
}

impl UniversalArg {
    pub fn new() -> Self;
    pub fn start(&mut self);
    pub fn add_digit(&mut self, digit: i32);
    pub fn get(&self) -> i32;
    pub fn clear(&mut self);
}

pub struct SearchState {
    active: bool,
    query: String,
    match_pos: Option<MatchPosition>,
    origin: Option<SearchOrigin>,
    index: i32,
}

#[derive(Debug, Clone, Copy)]
pub struct MatchPosition {
    pub row: usize,
    pub col: usize,
    pub len: usize,
}

#[derive(Debug, Clone, Copy)]
pub struct SearchOrigin {
    pub x: usize,
    pub y: usize,
    pub row_offset: usize,
    pub col_offset: usize,
}

impl SearchState {
    pub fn new() -> Self;
    pub fn activate(&mut self, query: String);
    pub fn deactivate(&mut self);
    pub fn set_match(&mut self, pos: MatchPosition);
    pub fn set_origin(&mut self, origin: SearchOrigin);
    pub fn clear_origin(&mut self);
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PromptKind {
    SaveAs,
    Open,
    BufferSwitch,
    Find,
    RegexFind,
    RegexpReplace,
    SearchReplace,
    JumpToLine,
    Command,
    ChangeDirectory,
    ThemeByName,
    FontByName,
    FontSetSize,
    BackgroundSet,
    GenericCommand,
}

pub struct PromptState {
    kind: PromptKind,
    label: String,
    text: String,
    pending_overwrite: Option<PathBuf>,
}

impl PromptState {
    pub fn new(kind: PromptKind, label: String, initial: String) -> Self;
}

pub struct FilePickerState {
    visible: bool,
    directory: PathBuf,
}

pub struct ReplaceState {
    find_tmp: String,
    with_tmp: String,
}

impl Editor {
    pub fn new() -> Self;
    
    // Dimensions
    pub fn set_dimensions(&mut self, rows: usize, cols: usize);
    pub fn dimensions(&self) -> Dimensions;
    
    // Buffer management
    pub fn buffer_count(&self) -> usize;
    pub fn current_buffer_index(&self) -> usize;
    pub fn current_buffer(&self) -> &Buffer;
    pub fn current_buffer_mut(&mut self) -> &mut Buffer;
    pub fn buffers(&self) -> &[Buffer];
    pub fn buffers_mut(&mut self) -> &mut Vec<Buffer>;
    
    pub fn add_buffer(&mut self, buffer: Buffer) -> usize;
    pub fn open_file(&mut self, path: impl AsRef<Path>) -> Result<usize, EditorError>;
    pub fn switch_to(&mut self, index: usize) -> bool;
    pub fn close_buffer(&mut self, index: usize) -> bool;
    pub fn next_buffer(&mut self);
    pub fn prev_buffer(&mut self);
    
    pub fn display_name(&self, buffer: &Buffer) -> String;
    
    // Mode management
    pub fn mode(&self) -> EditorMode;
    pub fn set_mode(&mut self, mode: EditorMode);
    
    // Status line
    pub fn set_status(&mut self, message: impl Into<String>);
    pub fn status(&self) -> &str;
    
    // Kill ring
    pub fn kill_ring(&self) -> &KillRing;
    pub fn kill_ring_mut(&mut self) -> &mut KillRing;
    
    // Universal argument
    pub fn universal_arg(&self) -> &UniversalArg;
    pub fn universal_arg_mut(&mut self) -> &mut UniversalArg;
    
    // Search
    pub fn search(&self) -> &SearchState;
    pub fn search_mut(&mut self) -> &mut SearchState;
    
    // Prompt
    pub fn start_prompt(&mut self, kind: PromptKind, label: String, initial: String);
    pub fn cancel_prompt(&mut self);
    pub fn accept_prompt(&mut self) -> Option<(PromptKind, String)>;
    pub fn prompt(&self) -> Option<&PromptState>;
    pub fn prompt_mut(&mut self) -> Option<&mut PromptState>;
    
    // Quit/close state
    pub fn set_quit_requested(&mut self, requested: bool);
    pub fn quit_requested(&self) -> bool;
    
    // Swap manager
    pub fn swap_manager(&self) -> Arc<Mutex<SwapManager>>;
    
    // Reset
    pub fn reset(&mut self);
}

#[derive(Debug, thiserror::Error)]
pub enum EditorError {
    #[error("Buffer error: {0}")]
    Buffer(#[from] BufferError),
    
    #[error("No such buffer: {0}")]
    NoSuchBuffer(usize),
    
    #[error("Cannot close last buffer")]
    CannotCloseLastBuffer,
}
```

**Implementation Notes:**

- Use nested structs for logical grouping
- `Arc<Mutex<SwapManager>>` for shared ownership with buffers
- Consider `parking_lot::Mutex` for better performance
- Status timestamp for message timeout handling
- Kill ring size limit (default 64 entries)

### 5. Command System

**Current C++ Implementation:**

- `CommandId` enum with ~50 command variants
- `Command` struct: id, name, help, handler function, isPublic,
  repeatable flags
- `CommandContext`: editor reference, arg string, count
- `CommandRegistry`: global static storage, lookup by ID or name
- `InstallDefaultCommands()`: registers all built-in commands
- `Execute()`: dispatch by ID or name

**Rust Design:**

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum CommandId {
    // File operations
    Save,
    SaveAs,
    OpenFile,
    ReloadBuffer,
    
    // Buffer management
    BufferSwitch,
    BufferClose,
    BufferNext,
    BufferPrev,
    
    // Quit
    Quit,
    QuitNow,
    SaveAndQuit,
    
    // Editing
    InsertText,
    Newline,
    Backspace,
    DeleteChar,
    KillToEOL,
    KillLine,
    Yank,
    DeleteWordPrev,
    DeleteWordNext,
    
    // Navigation
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    MoveHome,
    MoveEnd,
    PageUp,
    PageDown,
    ScrollUp,
    ScrollDown,
    WordPrev,
    WordNext,
    MoveFileStart,
    MoveFileEnd,
    MoveCursorTo,
    JumpToLine,
    CenterOnCursor,
    
    // Mark/Region
    ToggleMark,
    JumpToMark,
    KillRegion,
    CopyRegion,
    MarkAllAndJumpEnd,
    IndentRegion,
    UnindentRegion,
    ReflowParagraph,
    
    // Kill ring
    FlushKillRing,
    
    // Search
    FindStart,
    RegexFindStart,
    RegexpReplace,
    SearchReplace,
    
    // Undo/Redo
    Undo,
    Redo,
    
    // UI
    Refresh,
    ShowHelp,
    ShowWorkingDirectory,
    ChangeWorkingDirectory,
    
    // Syntax
    ToggleReadOnly,
    Syntax,
    SetOption,
    
    // Prompts
    KPrefix,
    CommandPromptStart,
    
    // GUI-specific
    VisualFilePickerToggle,
    VisualFontPickerToggle,
    ThemeNext,
    ThemePrev,
    ThemeSetByName,
    FontSetByName,
    FontSetSize,
    BackgroundSet,
    
    // Meta/Internal
    UArgStatus,
    UnknownKCommand,
    UnknownEscCommand,
}

pub struct CommandContext<'a> {
    pub editor: &'a mut Editor,
    pub arg: Option<String>,
    pub count: Option<i32>,
}

pub type CommandHandler = fn(&mut CommandContext) -> Result<(), CommandError>;

pub struct Command {
    pub id: CommandId,
    pub name: &'static str,
    pub help: &'static str,
    pub handler: CommandHandler,
    pub is_public: bool,
    pub repeatable: bool,
}

pub struct CommandRegistry {
    commands: HashMap<CommandId, Command>,
    by_name: HashMap<String, CommandId>,
}

impl CommandRegistry {
    pub fn new() -> Self;
    
    pub fn register(&mut self, command: Command);
    pub fn register_all(&mut self);
    
    pub fn find_by_id(&self, id: CommandId) -> Option<&Command>;
    pub fn find_by_name(&self, name: &str) -> Option<&Command>;
    pub fn all_commands(&self) -> impl Iterator<Item = &Command>;
    pub fn public_commands(&self) -> impl Iterator<Item = &Command>;
    
    pub fn execute(&self, editor: &mut Editor, id: CommandId, arg: Option<String>, count: Option<i32>) 
        -> Result<(), CommandError>;
    pub fn execute_by_name(&self, editor: &mut Editor, name: &str, arg: Option<String>, count: Option<i32>) 
        -> Result<(), CommandError>;
}

#[derive(Debug, thiserror::Error)]
pub enum CommandError {
    #[error("Unknown command: {0}")]
    UnknownCommand(String),
    
    #[error("Buffer error: {0}")]
    Buffer(#[from] BufferError),
    
    #[error("Editor error: {0}")]
    Editor(#[from] EditorError),
    
    #[error("Invalid argument: {0}")]
    InvalidArgument(String),
    
    #[error("Command failed: {0}")]
    Failed(String),
}
```

**Implementation Notes:**

- Use `HashMap` for O(1) command lookup
- Static string slices for command names/help
- Function pointers for handlers (simpler than trait objects for this
  use case)
- Consider `once_cell::Lazy` for global registry initialization
- Command repeat logic: multiply count by operation (e.g., 10 ×
  MoveRight)
- Separate command handler implementations into modules by category

## Frontend Layer

### 6. Frontend Abstraction

**Current C++ Implementation:**

- `Frontend` trait with Init/Step/Shutdown lifecycle
- Three implementations: Terminal (ncurses), ImGui (SDL2/OpenGL), Qt
- Step() does one iteration: poll input, dispatch commands, render
- Each frontend owns InputHandler and Renderer instances

**Rust Design:**

```rust
pub trait Frontend {
    /// Initialize the frontend (window, terminal, etc.)
    fn init(&mut self, editor: &mut Editor) -> Result<(), FrontendError>;
    
    /// Execute one iteration: poll input, dispatch commands, render
    /// Returns `false` when the frontend should exit
    fn step(&mut self, editor: &mut Editor, registry: &CommandRegistry) -> Result<bool, FrontendError>;
    
    /// Cleanup and shutdown
    fn shutdown(&mut self) -> Result<(), FrontendError>;
}

#[derive(Debug, thiserror::Error)]
pub enum FrontendError {
    #[error("Initialization failed: {0}")]
    InitFailed(String),
    
    #[error("Rendering failed: {0}")]
    RenderFailed(String),
    
    #[error("Input error: {0}")]
    InputError(String),
    
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}
```

**Implementation Notes:**

- Use trait objects (`Box<dyn Frontend>`) for runtime selection
- Feature flags: `terminal-frontend`, `gui-frontend`
- Each frontend type in separate module

### 7. Input Handling

**Current C++ Implementation:**

- `InputHandler` trait with Poll() returning `MappedInput`
- `MappedInput`: hasCommand bool, CommandId, arg string, count
- Terminal: ncurses-based, translates keys to commands
- ImGui/Qt: event-based, handles text input differently
- Keymap lookups via `KKeymap` functions (KCommand, Ctrl, Esc)

**Rust Design:**

```rust
pub trait InputHandler {
    /// Attach editor for state-dependent input handling
    fn attach(&mut self, editor: &Editor);
    
    /// Poll for input and map to command
    /// Returns None if no input available (non-blocking)
    fn poll(&mut self) -> Result<Option<MappedInput>, InputError>;
}

#[derive(Debug, Clone)]
pub struct MappedInput {
    pub command: CommandId,
    pub arg: Option<String>,
    pub count: Option<i32>,
}

#[derive(Debug, thiserror::Error)]
pub enum InputError {
    #[error("Input read failed: {0}")]
    ReadFailed(String),
    
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}

// Keymap lookup functions
pub struct Keymap;

impl Keymap {
    /// Lookup k-command (after C-k prefix)
    pub fn lookup_k_command(key: char, ctrl: bool) -> Option<CommandId>;
    
    /// Lookup control chord (C-x)
    pub fn lookup_ctrl_command(key: char) -> Option<CommandId>;
    
    /// Lookup ESC/meta command (ESC x)
    pub fn lookup_esc_command(key: char) -> Option<CommandId>;
    
    /// Normalize key to lowercase ASCII
    pub fn normalize_key(key: char) -> char;
}
```

**Terminal Input Handler (using crossterm):**

```rust
pub struct TerminalInputHandler {
    editor_ref: Option<*const Editor>,  // Weak reference for state queries
    mode_stack: Vec<InputMode>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum InputMode {
    Normal,
    KCommand,
    EscCommand,
}

impl InputHandler for TerminalInputHandler {
    fn attach(&mut self, editor: &Editor) {
        self.editor_ref = Some(editor as *const Editor);
    }
    
    fn poll(&mut self) -> Result<Option<MappedInput>, InputError> {
        // Implementation using crossterm::event::poll() and read()
    }
}
```

**Implementation Notes:**

- Use `crossterm` crate for terminal input (cross-platform, no ncurses
  dependency)
- Terminal handler maintains input mode state
- GUI handlers can use event system directly
- Keymap implemented as match statements for clarity and performance

### 8. Rendering

**Current C++ Implementation:**

- `Renderer` trait with single Draw(Editor&) method
- Terminal: ncurses-based, manual cursor positioning
- ImGui/Qt: retained mode GUI, draw calls within Begin/End blocks
- Syntax highlighting integrated via Buffer::Highlighter()

**Rust Design:**

```rust
pub trait Renderer {
    /// Render the editor state to the display
    fn draw(&mut self, editor: &Editor) -> Result<(), RenderError>;
}

#[derive(Debug, thiserror::Error)]
pub enum RenderError {
    #[error("Draw failed: {0}")]
    DrawFailed(String),
    
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}
```

**Terminal Renderer (using crossterm):**

```rust
pub struct TerminalRenderer {
    // Cached rendering state
    last_status: String,
    color_support: ColorSupport,
}

#[derive(Debug, Clone, Copy)]
enum ColorSupport {
    None,
    Ansi16,
    Ansi256,
    TrueColor,
}

impl TerminalRenderer {
    pub fn new() -> Result<Self, RenderError>;
    
    fn draw_buffer(&mut self, buffer: &Buffer, viewport: Viewport, dimensions: Dimensions) -> Result<(), RenderError>;
    fn draw_status_line(&mut self, editor: &Editor) -> Result<(), RenderError>;
    fn draw_message_line(&mut self, editor: &Editor) -> Result<(), RenderError>;
    fn draw_prompt(&mut self, prompt: &PromptState) -> Result<(), RenderError>;
    
    fn apply_syntax_colors(&self, token_kind: TokenKind) -> crossterm::style::Color;
}

impl Renderer for TerminalRenderer {
    fn draw(&mut self, editor: &Editor) -> Result<(), RenderError> {
        // Clear screen, draw buffer content, status, message, prompt
    }
}
```

**Implementation Notes:**

- `crossterm` for terminal rendering (replaces ncurses)
- Detect color support level via terminal capabilities
- Buffer rendering: iterate visible lines, apply syntax highlighting
- Status line: show mode, filename, position, dirty flag
- Message line: show status messages or prompt
- Consider `tui-rs` / `ratatui` for higher-level terminal UI
  abstractions

### 9. Frontend Implementations

#### 9.1 Terminal Frontend

```rust
pub struct TerminalFrontend {
    input_handler: TerminalInputHandler,
    renderer: TerminalRenderer,
    initialized: bool,
}

impl Frontend for TerminalFrontend {
    fn init(&mut self, editor: &mut Editor) -> Result<(), FrontendError> {
        // Enter raw mode, enable mouse, alternate screen
        crossterm::terminal::enable_raw_mode()?;
        crossterm::execute!(
            std::io::stdout(),
            crossterm::terminal::EnterAlternateScreen,
            crossterm::event::EnableMouseCapture
        )?;
        
        // Detect terminal size
        let (cols, rows) = crossterm::terminal::size()?;
        editor.set_dimensions(rows as usize, cols as usize);
        
        self.initialized = true;
        Ok(())
    }
    
    fn step(&mut self, editor: &mut Editor, registry: &CommandRegistry) -> Result<bool, FrontendError> {
        // Poll input with timeout
        if crossterm::event::poll(Duration::from_millis(100))? {
            if let Some(mapped) = self.input_handler.poll()? {
                registry.execute(editor, mapped.command, mapped.arg, mapped.count)?;
            }
        }
        
        // Render
        self.renderer.draw(editor)?;
        
        Ok(!editor.quit_requested())
    }
    
    fn shutdown(&mut self) -> Result<(), FrontendError> {
        if self.initialized {
            crossterm::execute!(
                std::io::stdout(),
                crossterm::terminal::LeaveAlternateScreen,
                crossterm::event::DisableMouseCapture
            )?;
            crossterm::terminal::disable_raw_mode()?;
        }
        Ok(())
    }
}
```

#### 9.2 GUI Frontend (egui-based)

For GUI, consider replacing ImGui/Qt with `egui` (pure Rust, immediate
mode):

```rust
#[cfg(feature = "gui-frontend")]
pub struct GuiFrontend {
    event_loop: Option<winit::event_loop::EventLoop<()>>,
    window: Option<winit::window::Window>,
    egui_ctx: egui::Context,
    egui_state: egui_winit::State,
    // ... rendering state
}

impl Frontend for GuiFrontend {
    fn init(&mut self, editor: &mut Editor) -> Result<(), FrontendError> {
        // Create window, initialize egui
    }
    
    fn step(&mut self, editor: &mut Editor, registry: &CommandRegistry) -> Result<bool, FrontendError> {
        // Process events, update egui, dispatch commands, render
    }
    
    fn shutdown(&mut self) -> Result<(), FrontendError> {
        // Cleanup window
    }
}
```

**Implementation Notes:**

- `egui` for immediate-mode GUI (simpler than ImGui integration)
- `winit` for window management
- `wgpu` or `glow` for rendering backend
- Alternatively, keep SDL2 + ImGui with `imgui-rs` bindings
- GUI font rendering: `egui` built-in or `fontdue` crate

## Infrastructure Layer

### 10. Swap File System (Crash Recovery)

**Current C++ Implementation:**

- `SwapManager`: manages sidecar `.swp` files for each buffer
- Background writer thread with flush/fsync intervals
- Records operations: Insert, Delete, Split, Join
- Per-buffer journal context with file handle, timestamps
- CRC32 checksums for integrity
- Suspend mechanism for internal operations

**Rust Design:**

```rust
pub struct SwapManager {
    config: SwapConfig,
    journals: HashMap<BufferId, JournalContext>,
    queue: Arc<Mutex<Vec<PendingOperation>>>,
    worker: Option<JoinHandle<()>>,
    shutdown: Arc<AtomicBool>,
}

#[derive(Clone)]
pub struct SwapConfig {
    pub flush_interval_ms: u64,
    pub fsync_interval_ms: u64,
}

impl Default for SwapConfig {
    fn default() -> Self {
        Self {
            flush_interval_ms: 200,
            fsync_interval_ms: 1000,
        }
    }
}

type BufferId = usize;  // Could be buffer pointer or unique ID

struct JournalContext {
    path: PathBuf,
    file: std::fs::File,
    header_written: bool,
    suspended: bool,
    last_flush: Instant,
    last_fsync: Instant,
}

#[derive(Clone, Copy, Debug)]
#[repr(u8)]
pub enum SwapRecordType {
    Insert = 1,
    Delete = 2,
    Split = 3,
    Join = 4,
    Meta = 0xF0,
    Checkpoint = 0xFE,
}

struct PendingOperation {
    buffer_id: BufferId,
    record_type: SwapRecordType,
    payload: Vec<u8>,
    urgent: bool,
}

impl SwapManager {
    pub fn new() -> Self;
    pub fn with_config(config: SwapConfig) -> Self;
    
    // Buffer lifecycle
    pub fn attach(&mut self, buffer_id: BufferId, filename: &Path) -> Result<(), SwapError>;
    pub fn detach(&mut self, buffer_id: BufferId);
    pub fn notify_filename_changed(&mut self, buffer_id: BufferId, new_path: &Path);
    
    // Record operations
    pub fn record_insert(&self, buffer_id: BufferId, row: usize, col: usize, text: &str);
    pub fn record_delete(&self, buffer_id: BufferId, row: usize, col: usize, len: usize);
    pub fn record_split(&self, buffer_id: BufferId, row: usize, col: usize);
    pub fn record_join(&self, buffer_id: BufferId, row: usize);
    
    // Suspend/resume
    pub fn set_suspended(&mut self, buffer_id: BufferId, suspended: bool);
    
    // RAII guard
    pub fn suspend_guard(&mut self, buffer_id: BufferId) -> SuspendGuard;
    
    // Shutdown
    pub fn shutdown(&mut self);
}

pub struct SuspendGuard<'a> {
    manager: &'a mut SwapManager,
    buffer_id: BufferId,
    previous: bool,
}

impl Drop for SuspendGuard<'_> {
    fn drop(&mut self) {
        self.manager.set_suspended(self.buffer_id, self.previous);
    }
}

#[derive(Debug, thiserror::Error)]
pub enum SwapError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    
    #[error("Failed to create swap directory: {0}")]
    DirectoryCreation(String),
    
    #[error("Checksum mismatch")]
    ChecksumMismatch,
}

// Recovery functions
pub fn recover_from_swap(swap_path: &Path) -> Result<Buffer, SwapError>;
pub fn list_swap_files(directory: &Path) -> Vec<PathBuf>;
```

**Implementation Notes:**

- Use `std::thread` with `crossbeam-channel` for background worker
- Swap file path: `~/.local/share/kte/swap/<hash>.swp` or similar
- File format: header (magic, version) + records (type, length, payload,
  CRC32)
- Varint encoding for compact integers
- Recovery tool as separate binary or subcommand
- Consider `tokio` for async I/O if performance critical

### 11. Search

**Current C++ Implementation:**

- Incremental search with origin tracking
- Two search modes: plain text, regex
- `OptimizedSearch`: Boyer-Moore-Horspool algorithm for fast text search
- Regex via C++ `<regex>` or external library

**Rust Design:**

```rust
pub struct SearchEngine {
    algorithm: SearchAlgorithm,
}

pub enum SearchAlgorithm {
    Plain(PlainSearch),
    Regex(RegexSearch),
}

pub struct PlainSearch {
    // Boyer-Moore-Horspool bad character table
    bad_char_table: [usize; 256],
    pattern: Vec<u8>,
    case_sensitive: bool,
}

impl PlainSearch {
    pub fn new(pattern: &str, case_sensitive: bool) -> Self;
    
    /// Search in a single line, returns byte offset if found
    pub fn find_in_line(&self, line: &str, start: usize) -> Option<usize>;
    
    /// Search backward in a line
    pub fn rfind_in_line(&self, line: &str, end: usize) -> Option<usize>;
}

pub struct RegexSearch {
    pattern: regex::Regex,
}

impl RegexSearch {
    pub fn new(pattern: &str) -> Result<Self, regex::Error>;
    
    pub fn find_in_line(&self, line: &str, start: usize) -> Option<(usize, usize)>;
}

impl SearchEngine {
    pub fn new_plain(pattern: &str, case_sensitive: bool) -> Self;
    pub fn new_regex(pattern: &str) -> Result<Self, regex::Error>;
    
    /// Search forward from position
    pub fn find(&self, buffer: &Buffer, start: Position) -> Option<SearchMatch>;
    
    /// Search backward from position
    pub fn rfind(&self, buffer: &Buffer, start: Position) -> Option<SearchMatch>;
    
    /// Find all matches in buffer (for replace all)
    pub fn find_all(&self, buffer: &Buffer) -> Vec<SearchMatch>;
}

#[derive(Debug, Clone, Copy)]
pub struct SearchMatch {
    pub row: usize,
    pub col: usize,
    pub len: usize,
}
```

**Implementation Notes:**

- Use `regex` crate for regex support (fast, well-tested)
- Boyer-Moore-Horspool for plain text (faster than naive for large
  patterns)
- Case-insensitive search: lowercase both pattern and text
- UTF-8 aware: work with character boundaries
- Consider `aho-corasick` for multi-pattern search
- Incremental search state managed in Editor

### 12. Syntax Highlighting

**Current C++ Implementation:**

- `HighlighterEngine`: manages background worker, caches line highlights
- Language-specific highlighters: Go, Shell, Markdown, Lisp, SQL, Forth,
  JSON
- Optional TreeSitter support with `KTE_ENABLE_TREESITTER`
- `TokenKind` enum with 14 token types
- `LineHighlight`: vector of spans with version tracking
- Background thread for prefetching viewport

**Rust Design:**

```rust
pub trait Highlighter: Send + Sync {
    /// Returns the language name
    fn language(&self) -> &str;
    
    /// Highlight a single line, returns spans
    fn highlight_line(&self, line: &str, state: Option<&dyn Any>) -> (Vec<HighlightSpan>, Option<Box<dyn Any>>);
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TokenKind {
    Default,
    Keyword,
    Type,
    String,
    Char,
    Comment,
    Number,
    Preproc,
    Constant,
    Function,
    Operator,
    Punctuation,
    Identifier,
    Whitespace,
    Error,
}

#[derive(Debug, Clone)]
pub struct HighlightSpan {
    pub col_start: usize,  // inclusive
    pub col_end: usize,    // exclusive
    pub kind: TokenKind,
}

#[derive(Clone)]
pub struct LineHighlight {
    pub spans: Vec<HighlightSpan>,
    pub version: u64,
}

pub struct HighlighterEngine {
    highlighter: Box<dyn Highlighter>,
    cache: Arc<Mutex<HighlightCache>>,
    worker: Option<JoinHandle<()>>,
    shutdown: Arc<AtomicBool>,
}

struct HighlightCache {
    lines: HashMap<usize, LineHighlight>,
    max_entries: usize,
}

impl HighlighterEngine {
    pub fn new(highlighter: Box<dyn Highlighter>) -> Self;
    
    /// Get highlighted line (may return cached or compute)
    pub fn get_line(&self, buffer: &Buffer, row: usize, version: u64) -> LineHighlight;
    
    /// Prefetch lines for viewport (spawns background work)
    pub fn prefetch_viewport(&self, buffer: &Buffer, start_row: usize, count: usize, version: u64);
    
    /// Clear cache
    pub fn invalidate(&mut self);
    
    /// Shutdown background worker
    pub fn shutdown(&mut self);
}

// Registry for language detection
pub struct HighlighterRegistry;

impl HighlighterRegistry {
    /// Create highlighter for file extension or filetype
    pub fn create_for(filetype: &str) -> Option<Box<dyn Highlighter>>;
    
    /// Detect filetype from filename
    pub fn detect_filetype(filename: &Path) -> Option<String>;
}

// Built-in highlighters
pub struct RustHighlighter;
pub struct CHighlighter;
pub struct PythonHighlighter;
pub struct MarkdownHighlighter;
// ... etc.

// Null highlighter (no-op)
pub struct NullHighlighter;

impl Highlighter for NullHighlighter {
    fn language(&self) -> &str { "none" }
    fn highlight_line(&self, _line: &str, _state: Option<&dyn Any>) -> (Vec<HighlightSpan>, Option<Box<dyn Any>>) {
        (vec![], None)
    }
}
```

**Optional TreeSitter Integration:**

```rust
#[cfg(feature = "treesitter")]
pub struct TreeSitterHighlighter {
    language: tree_sitter::Language,
    parser: tree_sitter::Parser,
    query: tree_sitter::Query,
}

#[cfg(feature = "treesitter")]
impl Highlighter for TreeSitterHighlighter {
    fn language(&self) -> &str;
    fn highlight_line(&self, line: &str, state: Option<&dyn Any>) -> (Vec<HighlightSpan>, Option<Box<dyn Any>>);
}
```

**Implementation Notes:**

- Use `lazy_static` or `once_cell` for registry initialization
- Background worker uses `crossbeam-channel` or `tokio`
- Cache with LRU eviction (use `lru` crate)
- State-based highlighting: pass opaque state between lines for
  multi-line constructs
- TreeSitter languages: `tree-sitter-rust`, `tree-sitter-c`, etc.
- Simple regex-based highlighters for most languages (simpler than full
  parsing)
- Color themes: separate module mapping TokenKind to RGB/ANSI colors

## Keybindings Specification

### Complete Keybinding Map

The editor preserves the existing Wordstar/VDE/emacs-inspired
keybindings from `ke.md`:

#### K-Commands (C-k prefix)

| Key           | Command            | Description                              |
|---------------|--------------------|------------------------------------------|
| C-k BACKSPACE | DeleteToLineStart  | Delete from cursor to beginning of line  |
| C-k SPACE     | ToggleMark         | Toggle mark at cursor                    |
| C-k -         | UnindentRegion     | Unindent region (if mark set)            |
| C-k =         | IndentRegion       | Indent region (if mark set)              |
| C-k a         | MarkAllAndJumpEnd  | Set mark at start, jump to end           |
| C-k b         | BufferSwitch       | Switch to another buffer                 |
| C-k c         | BufferClose        | Close current buffer                     |
| C-k d         | KillToEOL          | Delete from cursor to end of line        |
| C-k C-d       | KillLine           | Delete entire line                       |
| C-k e         | OpenFile           | Edit/open file                           |
| C-k f         | FlushKillRing      | Clear kill ring                          |
| C-k g         | JumpToLine         | Go to specific line number               |
| C-k h         | ShowHelp           | Show help text                           |
| C-k j         | JumpToMark         | Jump to mark position                    |
| C-k k         | CenterOnCursor     | Center viewport on cursor                |
| C-k l         | ReloadBuffer       | Reload buffer from disk                  |
| C-k p         | BufferNext         | Switch to next buffer                    |
| C-k q         | Quit               | Exit editor (with confirmation if dirty) |
| C-k C-q       | QuitNow            | Exit immediately without confirmation    |
| C-k r         | Redo               | Redo changes                             |
| C-k s         | Save               | Save file                                |
| C-k u         | Undo               | Undo changes                             |
| C-k x         | SaveAndQuit        | Save and exit                            |
| C-k C-x       | SaveAndQuit        | Save and exit (alternate)                |
| C-k y         | Yank               | Insert from kill ring                    |
| C-k '         | ToggleReadOnly     | Toggle read-only mode                    |
| C-k ;         | CommandPromptStart | Generic command prompt                   |
| C-k \\        | (Debug)            | Dump core (debug builds only)            |

#### Control Chords

| Key | Command        | Description                     |
|-----|----------------|---------------------------------|
| C-a | MoveHome       | Move to beginning of line       |
| C-b | MoveLeft       | Move backward one character     |
| C-d | DeleteChar     | Delete character at cursor      |
| C-e | MoveEnd        | Move to end of line             |
| C-f | MoveRight      | Move forward one character      |
| C-g | Cancel         | Cancel current operation/prompt |
| C-h | Backspace      | Delete character before cursor  |
| C-l | Refresh        | Refresh display                 |
| C-n | MoveDown       | Move to next line               |
| C-p | MoveUp         | Move to previous line           |
| C-r | RegexFindStart | Regex search                    |
| C-s | FindStart      | Incremental search              |
| C-t | RegexpReplace  | Search and replace              |
| C-u | UArgStart      | Begin universal argument        |
| C-v | PageDown       | Page down                       |
| C-w | KillRegion     | Kill region (if mark set)       |
| C-y | Yank           | Yank from kill ring             |

#### ESC/Meta Commands

| Key           | Command         | Description                      |
|---------------|-----------------|----------------------------------|
| ESC BACKSPACE | DeleteWordPrev  | Delete previous word             |
| ESC b         | WordPrev        | Move to previous word            |
| ESC d         | DeleteWordNext  | Delete next word                 |
| ESC f         | WordNext        | Move to next word                |
| ESC q         | ReflowParagraph | Reflow paragraph to column width |
| ESC v         | PageUp          | Page up                          |
| ESC w         | CopyRegion      | Copy region to kill ring         |
| ESC <         | MoveFileStart   | Move to beginning of file        |
| ESC >         | MoveFileEnd     | Move to end of file              |

#### Special Keys

| Key         | Command          | Description                    |
|-------------|------------------|--------------------------------|
| Backspace   | Backspace        | Delete character before cursor |
| Delete      | DeleteChar       | Delete character at cursor     |
| Enter       | Newline          | Insert newline                 |
| Tab         | InsertText("\t") | Insert tab character           |
| Home        | MoveHome         | Move to beginning of line      |
| End         | MoveEnd          | Move to end of line            |
| Page Up     | PageUp           | Scroll up one page             |
| Page Down   | PageDown         | Scroll down one page           |
| Arrow Up    | MoveUp           | Move up one line               |
| Arrow Down  | MoveDown         | Move down one line             |
| Arrow Left  | MoveLeft         | Move left one character        |
| Arrow Right | MoveRight        | Move right one character       |

### Keymap Implementation

```rust
// In src/keymap.rs
pub fn lookup_k_command(key: char, ctrl: bool) -> Option<CommandId> {
    match (key, ctrl) {
        (' ', false) => Some(CommandId::ToggleMark),
        ('-', false) => Some(CommandId::UnindentRegion),
        ('=', false) => Some(CommandId::IndentRegion),
        ('a', false) => Some(CommandId::MarkAllAndJumpEnd),
        ('b', false) => Some(CommandId::BufferSwitch),
        ('c', false) => Some(CommandId::BufferClose),
        ('d', false) => Some(CommandId::KillToEOL),
        ('d', true) => Some(CommandId::KillLine),
        ('e', false) => Some(CommandId::OpenFile),
        ('f', false) => Some(CommandId::FlushKillRing),
        ('g', false) => Some(CommandId::JumpToLine),
        ('h', false) => Some(CommandId::ShowHelp),
        ('j', false) => Some(CommandId::JumpToMark),
        ('k', false) => Some(CommandId::CenterOnCursor),
        ('l', false) => Some(CommandId::ReloadBuffer),
        ('p', false) => Some(CommandId::BufferNext),
        ('q', false) => Some(CommandId::Quit),
        ('q', true) => Some(CommandId::QuitNow),
        ('r', false) => Some(CommandId::Redo),
        ('s', false) | ('s', true) => Some(CommandId::Save),
        ('u', false) => Some(CommandId::Undo),
        ('x', false) | ('x', true) => Some(CommandId::SaveAndQuit),
        ('y', false) => Some(CommandId::Yank),
        ('\'', false) => Some(CommandId::ToggleReadOnly),
        (';', false) => Some(CommandId::CommandPromptStart),
        ('\x08', false) => Some(CommandId::DeleteToLineStart), // Backspace
        _ => None,
    }
}

pub fn lookup_ctrl_command(key: char) -> Option<CommandId> {
    match key {
        'a' => Some(CommandId::MoveHome),
        'b' => Some(CommandId::MoveLeft),
        'd' => Some(CommandId::DeleteChar),
        'e' => Some(CommandId::MoveEnd),
        'f' => Some(CommandId::MoveRight),
        'g' => Some(CommandId::Cancel),
        'h' => Some(CommandId::Backspace),
        'l' => Some(CommandId::Refresh),
        'n' => Some(CommandId::MoveDown),
        'p' => Some(CommandId::MoveUp),
        'r' => Some(CommandId::RegexFindStart),
        's' => Some(CommandId::FindStart),
        't' => Some(CommandId::RegexpReplace),
        'u' => Some(CommandId::UArgStart),
        'v' => Some(CommandId::PageDown),
        'w' => Some(CommandId::KillRegion),
        'y' => Some(CommandId::Yank),
        _ => None,
    }
}

pub fn lookup_esc_command(key: char) -> Option<CommandId> {
    match key {
        '\x08' => Some(CommandId::DeleteWordPrev), // Backspace
        'b' => Some(CommandId::WordPrev),
        'd' => Some(CommandId::DeleteWordNext),
        'f' => Some(CommandId::WordNext),
        'q' => Some(CommandId::ReflowParagraph),
        'v' => Some(CommandId::PageUp),
        'w' => Some(CommandId::CopyRegion),
        '<' => Some(CommandId::MoveFileStart),
        '>' => Some(CommandId::MoveFileEnd),
        _ => None,
    }
}
```

## Project Structure

### Cargo Workspace Layout

```
kte-rust/
├── Cargo.toml                 # Workspace root
├── README.md
├── LICENSE
├── docs/
│   ├── architecture.md
│   ├── keybindings.md
│   └── contributing.md
├── kte-core/                  # Core library
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── buffer/
│       │   ├── mod.rs
│       │   ├── buffer.rs
│       │   ├── line.rs
│       │   ├── gap_buffer.rs
│       │   └── piece_table.rs
│       ├── undo/
│       │   ├── mod.rs
│       │   ├── undo_system.rs
│       │   ├── undo_tree.rs
│       │   └── undo_node.rs
│       ├── editor/
│       │   ├── mod.rs
│       │   ├── editor.rs
│       │   ├── kill_ring.rs
│       │   ├── search_state.rs
│       │   └── prompt.rs
│       ├── command/
│       │   ├── mod.rs
│       │   ├── command.rs
│       │   ├── registry.rs
│       │   └── handlers/
│       │       ├── mod.rs
│       │       ├── editing.rs
│       │       ├── navigation.rs
│       │       ├── file.rs
│       │       └── buffer.rs
│       ├── syntax/
│       │   ├── mod.rs
│       │   ├── highlighter.rs
│       │   ├── engine.rs
│       │   ├── registry.rs
│       │   └── languages/
│       │       ├── mod.rs
│       │       ├── rust.rs
│       │       ├── c.rs
│       │       ├── python.rs
│       │       └── markdown.rs
│       ├── search/
│       │   ├── mod.rs
│       │   ├── engine.rs
│       │   ├── plain.rs
│       │   └── regex.rs
│       ├── swap/
│       │   ├── mod.rs
│       │   ├── manager.rs
│       │   └── recovery.rs
│       └── util/
│           ├── mod.rs
│           └── position.rs
├── kte-frontend/              # Frontend abstractions
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── frontend.rs
│       ├── input.rs
│       ├── renderer.rs
│       └── keymap.rs
├── kte-terminal/              # Terminal frontend implementation
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── terminal.rs
│       ├── input.rs
│       └── renderer.rs
├── kte-gui/                   # GUI frontend (optional)
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── gui.rs
│       ├── input.rs
│       └── renderer.rs
├── kte/                       # Main binary
│   ├── Cargo.toml
│   └── src/
│       └── main.rs
└── tests/
    ├── integration/
    │   ├── buffer_tests.rs
    │   ├── undo_tests.rs
    │   ├── search_tests.rs
    │   └── command_tests.rs
    └── fixtures/
        └── test_files/
```

### Cargo.toml (Workspace Root)

```toml
[workspace]
members = [
    "kte-core",
    "kte-frontend",
    "kte-terminal",
    "kte-gui",
    "kte",
]
resolver = "2"

[workspace.package]
version = "2.0.0"
edition = "2021"
authors = ["Kyle Isom <kyle@imap.cc>"]
license = "MIT OR Apache-2.0"
repository = "https://github.com/kisom/kte"

[workspace.dependencies]
# Error handling
thiserror = "1.0"
anyhow = "1.0"

# Collections
smallvec = "1.11"

# Async/threading
crossbeam-channel = "0.5"
parking_lot = "0.12"

# Terminal UI
crossterm = "0.27"

# GUI (optional)
egui = "0.24"
winit = "0.29"

# Regex
regex = "1.10"

# Syntax highlighting
tree-sitter = "0.20"

# Utilities
once_cell = "1.19"
lru = "0.12"

# Serialization (for config)
serde = { version = "1.0", features = ["derive"] }
toml = "0.8"
```

### kte-core/Cargo.toml

```toml
[package]
name = "kte-core"
version.workspace = true
edition.workspace = true
authors.workspace = true
license.workspace = true

[dependencies]
thiserror.workspace = true
smallvec.workspace = true
crossbeam-channel.workspace = true
parking_lot.workspace = true
regex.workspace = true
once_cell.workspace = true
lru.workspace = true

# Optional TreeSitter support
tree-sitter = { workspace = true, optional = true }

[features]
default = ["piece-table"]
piece-table = []
gap-buffer = []
treesitter = ["dep:tree-sitter"]
```

### kte-terminal/Cargo.toml

```toml
[package]
name = "kte-terminal"
version.workspace = true
edition.workspace = true

[dependencies]
kte-core = { path = "../kte-core" }
kte-frontend = { path = "../kte-frontend" }
crossterm.workspace = true
thiserror.workspace = true
anyhow.workspace = true
```

### kte/Cargo.toml (Main Binary)

```toml
[package]
name = "kte"
version.workspace = true
edition.workspace = true
authors.workspace = true
license.workspace = true

[[bin]]
name = "kte"
path = "src/main.rs"

[dependencies]
kte-core = { path = "../kte-core" }
kte-frontend = { path = "../kte-frontend" }
kte-terminal = { path = "../kte-terminal" }
kte-gui = { path = "../kte-gui", optional = true }

anyhow.workspace = true
clap = { version = "4.4", features = ["derive"] }

[features]
default = ["terminal"]
terminal = []
gui = ["dep:kte-gui"]
```

## Implementation Phases

### Phase 1: Foundation (Weeks 1-3)

**Goal**: Establish core data structures and basic buffer operations

**Tasks**:

1. Set up Cargo workspace structure
2. Implement `PieceTable` with:
    - Basic piece management (insert, delete at byte offsets)
    - Line indexing for efficient line-based operations
    - Line count tracking and line extraction
    - Position conversion (byte offset ↔ line/col)
3. Implement `Buffer` with:
    - Single PieceTable for all text content
    - Cursor and viewport state
    - File I/O (load/save)
    - Line-based editing operations (insert_text, delete_text,
      split_line, join_lines)
4. Write comprehensive unit tests for PieceTable and Buffer operations
5. Set up CI/CD (GitHub Actions: build, test, clippy, fmt)

**Deliverables**:

- `kte-core` crate with working PieceTable and Buffer implementation
- Test coverage >80% for buffer operations
- Documentation for public APIs

### Phase 2: Undo System (Weeks 4-5)

**Goal**: Implement complete undo/redo with tree structure

**Tasks**:

1. Implement `UndoNode` and `UndoTree` with index-based storage
2. Implement `UndoSystem` with transaction support
3. Integrate undo recording with buffer operations
4. Implement saved state tracking for dirty flag
5. Write undo/redo tests including branching scenarios

**Deliverables**:

- Working undo/redo system
- Integration with buffer edit operations
- Test coverage for complex undo scenarios (branches, saved state)

### Phase 3: Editor State & Commands (Weeks 6-8)

**Goal**: Build application layer with command dispatch

**Tasks**:

1. Implement `Editor` state management
2. Implement `CommandRegistry` and command infrastructure
3. Implement core command handlers:
    - Navigation commands (move, page, scroll, jump)
    - Editing commands (insert, delete, kill, yank)
    - File commands (open, save, reload)
    - Buffer commands (switch, close, next/prev)
4. Implement kill ring with chain/append/prepend
5. Implement mark/region support
6. Implement universal argument system
7. Write command integration tests

**Deliverables**:

- Complete `Editor` with all state management
- ~50 command handlers implemented
- Command registry with lookup
- Integration tests for command execution

### Phase 4: Terminal Frontend (Weeks 9-11)

**Goal**: Create working terminal UI with crossterm

**Tasks**:

1. Implement `Frontend`, `InputHandler`, `Renderer` traits in
   `kte-frontend`
2. Implement `TerminalInputHandler`:
    - Key event translation
    - Mode state management (Normal, KCommand, EscCommand)
    - Keymap integration
3. Implement `TerminalRenderer`:
    - Buffer content rendering
    - Status line
    - Message line
    - Prompt rendering
    - Color support detection
4. Implement `TerminalFrontend` lifecycle
5. Create main binary with CLI argument parsing
6. Test on Linux, macOS, Windows

**Deliverables**:

- Working terminal frontend
- Complete keybinding support
- Cross-platform terminal UI
- Basic usability for editing

### Phase 5: Search & Replace (Weeks 12-13)

**Goal**: Implement incremental search and regex support

**Tasks**:

1. Implement `PlainSearch` with Boyer-Moore-Horspool
2. Implement `RegexSearch` with regex crate
3. Implement `SearchEngine` with forward/backward search
4. Integrate search state into Editor
5. Implement incremental search UI
6. Implement search & replace (single and all)
7. Write search correctness tests

**Deliverables**:

- Working incremental search (C-s)
- Regex search (C-r)
- Search and replace
- Test suite for search algorithms

### Phase 6: Syntax Highlighting (Weeks 14-16)

**Goal**: Add syntax highlighting with background worker

**Tasks**:

1. Implement `Highlighter` trait and `TokenKind` enum
2. Implement `HighlighterEngine` with caching
3. Implement background worker for viewport prefetching
4. Implement language-specific highlighters:
    - Rust (regex-based)
    - C/C++
    - Python
    - Markdown
    - Shell
5. Implement `HighlighterRegistry` with filetype detection
6. Integrate syntax highlighting into terminal renderer
7. Optional: Add TreeSitter support behind feature flag

**Deliverables**:

- Syntax highlighting for 5+ languages
- Background worker for async highlighting
- LRU cache with invalidation
- Color theme support

### Phase 7: Swap Files (Weeks 17-18)

**Goal**: Implement crash recovery system

**Tasks**:

1. Implement `SwapManager` with background writer thread
2. Implement journal file format (header, records, CRC32)
3. Integrate swap recording with buffer operations
4. Implement recovery tool/subcommand
5. Implement suspend mechanism for internal operations
6. Write swap correctness and recovery tests

**Deliverables**:

- Working swap file system
- Recovery mechanism
- Integration with buffer lifecycle
- Test suite including recovery scenarios

### Phase 8: Advanced Features (Weeks 19-20)

**Goal**: Implement remaining editor features

**Tasks**:

1. Implement region operations:
    - Indent/unindent region
    - Reflow paragraph
    - Case conversion
2. Implement word navigation and deletion
3. Implement prompt system for all prompt types
4. Implement working directory operations
5. Implement help text display
6. Polish status messages and error reporting

**Deliverables**:

- All documented commands implemented
- Complete prompt system
- Help system
- Professional error messages

### Phase 9: GUI Frontend (Optional, Weeks 21-24)

**Goal**: Add optional GUI frontend with egui

**Tasks**:

1. Implement `GuiFrontend` with egui + winit
2. Implement GUI input handler (keyboard, mouse)
3. Implement GUI renderer (text, menus, dialogs)
4. Implement file picker dialog
5. Implement font selection
6. Implement theme system
7. Test on multiple platforms

**Deliverables**:

- Working GUI frontend
- File picker, font picker
- Visual themes
- Mouse support

### Phase 10: Polish & Release (Weeks 25-26)

**Goal**: Prepare for production release

**Tasks**:

1. Performance profiling and optimization
2. Memory usage analysis
3. Comprehensive documentation:
    - User manual
    - Developer guide
    - Architecture documentation
4. Release preparation:
    - Version tagging
    - Release notes
    - Binary distribution (GitHub releases)
    - Package manager submissions (cargo, brew, apt, etc.)
5. Final testing on all platforms
6. Create migration guide from C++ version

**Deliverables**:

- Optimized binary (< 5MB stripped)
- Complete documentation
- Release v2.0.0
- Distribution packages

## Testing Strategy

### Unit Tests

**Coverage Target**: >80% for all modules

**Key Areas**:

- **PieceTable**: Insert, delete at various offsets; line indexing;
  position conversion
- **Buffer Operations**: Insert, delete, split, join at boundaries and
  middle
- **Undo System**: Single operations, batching, branching, saved state
- **Search**: Plain and regex, forward/backward, case sensitivity
- **Command Handlers**: Each command with various inputs
- **Syntax Highlighting**: Token parsing for each language

**Test Organization**:

```rust
// In kte-core/src/buffer/buffer.rs
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_insert_text_single_line() { /* ... */ }
    
    #[test]
    fn test_insert_text_with_newlines() { /* ... */ }
    
    #[test]
    fn test_delete_across_lines() { /* ... */ }
    
    // ... more tests
}
```

### Integration Tests

**Location**: `tests/integration/`

**Test Scenarios**:

1. **Buffer Lifecycle**: Open file → edit → save → reload → verify
2. **Undo/Redo**: Complex edit sequence → undo all → redo all → verify
3. **Search**: Large file search performance and correctness
4. **Command Dispatch**: End-to-end command execution with editor state
5. **Multi-Buffer**: Switch between buffers, close, reopen
6. **Swap Recovery**: Simulate crash → recover → verify buffer state

**Example**:

```rust
// tests/integration/buffer_tests.rs
#[test]
fn test_file_roundtrip() {
    let temp = tempfile::NamedTempFile::new().unwrap();
    let content = "line 1\nline 2\nline 3\n";
    
    // Write
    let mut buf = Buffer::new();
    buf.insert_text(0, 0, content);
    buf.save_as(temp.path()).unwrap();
    
    // Read
    let buf2 = Buffer::from_file(temp.path()).unwrap();
    assert_eq!(buf2.to_string(), content);
}
```

### Property-Based Tests

**Using**: `proptest` or `quickcheck` crate

**Properties to Test**:

1. **Buffer Invariants**: After any sequence of operations, buffer is
   valid UTF-8
2. **Undo Reversibility**: `edit → undo` restores original state
3. **Search Consistency**: Finding pattern always returns valid
   positions
4. **Piece Table Correctness**: Materialized content matches logical
   content

**Example**:

```rust
use proptest::prelude::*;

proptest! {
    #[test]
    fn test_undo_reversibility(
        initial in ".*",
        insert_text in ".*",
        insert_pos in 0..100usize
    ) {
        let mut buf = Buffer::new();
        buf.insert_text(0, 0, &initial);
        let snapshot = buf.to_string();
        
        buf.undo().begin(UndoType::Insert);
        buf.insert_text(0, insert_pos.min(initial.len()), &insert_text);
        buf.undo().commit(0, insert_pos.min(initial.len()));
        
        buf.undo().undo();
        assert_eq!(buf.to_string(), snapshot);
    }
}
```

### Benchmarks

**Using**: `criterion` crate

**Benchmark Suites**:

1. **Buffer Operations**: Insert, delete on various sizes (1KB, 100KB,
   10MB)
2. **Search**: Plain and regex search in large files
3. **Syntax Highlighting**: Highlight large files (1000+ lines)
4. **Undo Operations**: Undo/redo with large history
5. **File I/O**: Load and save large files

**Example**:

```rust
use criterion::{black_box, criterion_group, criterion_main, Criterion};

fn bench_buffer_insert(c: &mut Criterion) {
    let mut group = c.benchmark_group("buffer_insert");
    
    for size in [100, 1000, 10000].iter() {
        group.bench_with_input(
            BenchmarkId::from_parameter(size),
            size,
            |b, &size| {
                b.iter(|| {
                    let mut buf = Buffer::new();
                    for i in 0..size {
                        buf.insert_text(i, 0, "x");
                    }
                    black_box(buf);
                });
            },
        );
    }
    group.finish();
}

criterion_group!(benches, bench_buffer_insert);
criterion_main!(benches);
```

### Manual Testing Checklist

**Before Each Release**:

- [ ] Open various file types (text, code, binary)
- [ ] Edit large files (>10MB)
- [ ] Test all keybindings in terminal
- [ ] Test all keybindings in GUI (if applicable)
- [ ] Test undo/redo with complex scenarios
- [ ] Test search and replace with edge cases
- [ ] Test syntax highlighting for all supported languages
- [ ] Test buffer switching and closing
- [ ] Test crash recovery (kill process, recover)
- [ ] Test on Linux, macOS, Windows
- [ ] Test in various terminals (xterm, alacritty, iTerm2, Windows
  Terminal)
- [ ] Test with files in various encodings (UTF-8, Latin-1, etc.)
- [ ] Test with very long lines (>10000 chars)
- [ ] Test with files containing unusual characters
- [ ] Memory leak testing (long-running session)

### Continuous Integration

**GitHub Actions Workflow**:

```yaml
name: CI

on: [ push, pull_request ]

jobs:
  test:
    strategy:
      matrix:
        os: [ ubuntu-latest, macos-latest, windows-latest ]
        rust: [ stable, beta ]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v3
      - uses: dtolnay/rust-toolchain@master
        with:
          toolchain: ${{ matrix.rust }}
      - run: cargo build --all-features
      - run: cargo test --all-features
      - run: cargo bench --no-run

  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - uses: dtolnay/rust-toolchain@stable
        with:
          components: rustfmt, clippy
      - run: cargo fmt --all -- --check
      - run: cargo clippy --all-features -- -D warnings

  coverage:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - uses: dtolnay/rust-toolchain@stable
      - run: cargo install cargo-tarpaulin
      - run: cargo tarpaulin --all-features --workspace --out Xml
      - uses: codecov/codecov-action@v3
```

## Migration from C++ to Rust

### API Compatibility

The Rust rewrite is **not** intended to maintain binary or API
compatibility with the C++ version. However, it preserves:

- All keybindings
- File format compatibility (plain text files)
- Swap file format (with version detection)
- User experience and workflow

### Breaking Changes

1. **Configuration**: May use TOML instead of custom format
2. **Swap Files**: Format may differ (include version number for
   detection)
3. **Plugin System**: Not initially supported (future consideration)

### Migration Path for Users

1. **Gradual Adoption**: Keep C++ version installed alongside Rust
   version
2. **Data Migration**: Swap files from C++ version should be detectable
   and recoverable
3. **Documentation**: Provide side-by-side comparison guide
4. **Feedback Loop**: Issue tracker for feature parity requests

## Conclusion

This specification provides a complete roadmap for rewriting **kte**
from C++ to Rust. The rewrite preserves all existing functionality while
leveraging Rust's safety guarantees and modern ecosystem. The phased
approach allows for incremental development and testing, with clear
deliverables at each stage.

Key advantages of the Rust implementation:

- **Memory Safety**: No more segfaults or undefined behavior
- **Concurrency**: Safe background workers for highlighting and swap
  files
- **Ecosystem**: Rich crate ecosystem (crossterm, regex, tree-sitter,
  etc.)
- **Performance**: Zero-cost abstractions matching or exceeding C++
  performance
- **Maintainability**: Clear error handling, better type system,
  comprehensive testing

The estimated timeline is **26 weeks** for a complete implementation
including optional GUI frontend. A minimal viable editor (terminal only,
no syntax highlighting or swap files) could be completed in **8-10 weeks
**.

