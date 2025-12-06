# PieceTable Migration Plan

## Executive Summary

This document outlines the plan to remove GapBuffer support from kte and
migrate to using a **single PieceTable per Buffer**, rather than the
current vector-of-Lines architecture where each Line contains either a
GapBuffer or PieceTable.

## Current Architecture Analysis

### Text Storage

**Current Implementation:**

- `Buffer` contains `std::vector<Line> rows_`
- Each `Line` wraps an `AppendBuffer` (type alias)
- `AppendBuffer` is either `GapBuffer` (default) or `PieceTable` (via
  `KTE_USE_PIECE_TABLE`)
- Each line is independently managed with its own buffer
- Operations are line-based with coordinate pairs (row, col)

**Key Files:**

- `Buffer.h/cc` - Buffer class with vector of Lines
- `AppendBuffer.h` - Type selector (GapBuffer vs PieceTable)
- `GapBuffer.h/cc` - Per-line gap buffer implementation
- `PieceTable.h/cc` - Per-line piece table implementation
- `UndoSystem.h/cc` - Records operations with (row, col, text)
- `UndoNode.h` - Undo operation types (Insert, Delete, Paste, Newline,
  DeleteRow)
- `Command.cc` - High-level editing commands

### Current Buffer API

**Low-level editing operations (used by UndoSystem):**

```cpp
void insert_text(int row, int col, std::string_view text);
void delete_text(int row, int col, std::size_t len);
void split_line(int row, int col);
void join_lines(int row);
void insert_row(int row, std::string_view text);
void delete_row(int row);
```

**Line access:**

```cpp
std::vector<Line> &Rows();
const std::vector<Line> &Rows() const;
```

**Line API (Buffer::Line):**

```cpp
std::size_t size() const;
const char *Data() const;
char operator[](std::size_t i) const;
std::string substr(std::size_t pos, std::size_t len) const;
std::size_t find(const std::string &needle, std::size_t pos) const;
void erase(std::size_t pos, std::size_t len);
void insert(std::size_t pos, const std::string &seg);
Line &operator+=(const Line &other);
Line &operator+=(const std::string &s);
```

### Current PieceTable Limitations

The existing `PieceTable` class only supports:

- `Append(char/string)` - add to end
- `Prepend(char/string)` - add to beginning
- `Clear()` - empty the buffer
- `Data()` / `Size()` - access content (materializes on demand)

**Missing capabilities needed for buffer-wide storage:**

- Insert at arbitrary byte position
- Delete at arbitrary byte position
- Line indexing and line-based queries
- Position conversion (byte offset ↔ line/col)
- Efficient line boundary tracking

## Target Architecture

### Design Overview

**Single PieceTable per Buffer:**

- `Buffer` contains one `PieceTable content_` (replaces
  `std::vector<Line> rows_`)
- Text stored as continuous byte sequence with `\n` as line separators
- Line index cached for efficient line-based operations
- All operations work on byte offsets internally
- Buffer provides line/column API as convenience layer

### Enhanced PieceTable Design

```cpp
class PieceTable {
public:
    // Existing API (keep for compatibility if needed)
    void Append(const char *s, std::size_t len);
    void Prepend(const char *s, std::size_t len);
    void Clear();
    const char *Data() const;
    std::size_t Size() const;
    
    // NEW: Core byte-based editing operations
    void Insert(std::size_t byte_offset, const char *text, std::size_t len);
    void Delete(std::size_t byte_offset, std::size_t len);
    
    // NEW: Line-based queries
    std::size_t LineCount() const;
    std::string GetLine(std::size_t line_num) const;
    std::pair<std::size_t, std::size_t> GetLineRange(std::size_t line_num) const; // (start, end) byte offsets
    
    // NEW: Position conversion
    std::pair<std::size_t, std::size_t> ByteOffsetToLineCol(std::size_t byte_offset) const;
    std::size_t LineColToByteOffset(std::size_t row, std::size_t col) const;
    
    // NEW: Substring extraction
    std::string GetRange(std::size_t byte_offset, std::size_t len) const;
    
    // NEW: Search support
    std::size_t Find(const std::string &needle, std::size_t start_offset) const;

private:
    // Existing members
    std::string original_;
    std::string add_;
    std::vector<Piece> pieces_;
    mutable std::string materialized_;
    mutable bool dirty_;
    std::size_t total_size_;
    
    // NEW: Line index for efficient line operations
    struct LineInfo {
        std::size_t byte_offset;      // absolute byte offset from buffer start
        std::size_t piece_idx;        // which piece contains line start
        std::size_t offset_in_piece;  // byte offset within that piece
    };
    mutable std::vector<LineInfo> line_index_;
    mutable bool line_index_dirty_;
    
    // NEW: Line index management
    void RebuildLineIndex() const;
    void InvalidateLineIndex();
};
```

### Buffer API Changes

```cpp
class Buffer {
public:
    // NEW: Direct content access
    PieceTable &Content() { return content_; }
    const PieceTable &Content() const { return content_; }
    
    // MODIFIED: Keep existing API but implement via PieceTable
    void insert_text(int row, int col, std::string_view text);
    void delete_text(int row, int col, std::size_t len);
    void split_line(int row, int col);
    void join_lines(int row);
    void insert_row(int row, std::string_view text);
    void delete_row(int row);
    
    // MODIFIED: Line access - return line from PieceTable
    std::size_t Nrows() const { return content_.LineCount(); }
    std::string GetLine(std::size_t row) const { return content_.GetLine(row); }
    
    // REMOVED: Rows() - no longer have vector of Lines
    // std::vector<Line> &Rows();  // REMOVE
    
private:
    // REMOVED: std::vector<Line> rows_;
    // NEW: Single piece table for all content
    PieceTable content_;
    
    // Keep existing members
    std::size_t curx_, cury_, rx_;
    std::size_t nrows_;  // cached from content_.LineCount()
    std::size_t rowoffs_, coloffs_;
    std::string filename_;
    bool is_file_backed_;
    bool dirty_;
    bool read_only_;
    bool mark_set_;
    std::size_t mark_curx_, mark_cury_;
    std::unique_ptr<UndoTree> undo_tree_;
    std::unique_ptr<UndoSystem> undo_sys_;
    std::uint64_t version_;
    bool syntax_enabled_;
    std::string filetype_;
    std::unique_ptr<kte::HighlighterEngine> highlighter_;
    kte::SwapRecorder *swap_rec_;
};
```

## Migration Phases

### Phase 1: Extend PieceTable (Foundation)

**Goal:** Add buffer-wide capabilities to PieceTable without breaking
existing per-line usage.

**Tasks:**

1. Add line indexing infrastructure to PieceTable
    - Add `LineInfo` struct and `line_index_` member
    - Implement `RebuildLineIndex()` that scans pieces for '\n'
      characters
    - Implement `InvalidateLineIndex()` called by Insert/Delete

2. Implement core byte-based operations
    - `Insert(byte_offset, text, len)` - split piece at offset, insert
      new piece
    - `Delete(byte_offset, len)` - split pieces, remove/truncate as
      needed

3. Implement line-based query methods
    - `LineCount()` - return line_index_.size()
    - `GetLine(line_num)` - extract text between line boundaries
    - `GetLineRange(line_num)` - return (start, end) byte offsets

4. Implement position conversion
    - `ByteOffsetToLineCol(offset)` - binary search in line_index_
    - `LineColToByteOffset(row, col)` - lookup line start, add col

5. Implement utility methods
    - `GetRange(offset, len)` - extract substring
    - `Find(needle, start)` - search across pieces

**Testing:**

- Write unit tests for new PieceTable methods
- Test with multi-line content
- Verify line index correctness after edits
- Benchmark performance vs current line-based approach

**Estimated Effort:** 3-5 days

### Phase 2: Create Buffer Adapter Layer (Compatibility)

**Goal:** Create compatibility layer in Buffer to use PieceTable while
maintaining existing API.

**Tasks:**

1. Add `PieceTable content_` member to Buffer (alongside existing
   `rows_`)
2. Add compilation flag `KTE_USE_BUFFER_PIECE_TABLE` (like existing
   `KTE_USE_PIECE_TABLE`)
3. Implement Buffer methods to delegate to content_:
   ```cpp
   #ifdef KTE_USE_BUFFER_PIECE_TABLE
       void insert_text(int row, int col, std::string_view text) {
           std::size_t offset = content_.LineColToByteOffset(row, col);
           content_.Insert(offset, text.data(), text.size());
       }
       // ... similar for other methods
   #else
       // Existing line-based implementation
   #endif
   ```
4. Update file I/O to work with PieceTable
    - `OpenFromFile()` - load into content_ instead of rows_
    - `Save()` - serialize content_ instead of rows_
5. Update `AsString()` to materialize from content_

**Testing:**

- Run existing buffer correctness tests with new flag
- Verify undo/redo still works
- Test file I/O round-tripping
- Test with existing command operations

**Estimated Effort:** 3-4 days

### Phase 3: Migrate Command Layer (High-level Operations)

**Goal:** Update commands that directly access Rows() to use new API.

**Tasks:**

1. Audit all usages of `buf.Rows()` in Command.cc
2. Refactor helper functions:
    - `extract_region_text()` - use content_.GetRange()
    - `delete_region()` - convert to byte offsets, use content_.Delete()
    - `insert_text_at_cursor()` - convert position, use content_
      .Insert()
3. Update commands that iterate over lines:
    - Use `buf.GetLine(i)` instead of `buf.Rows()[i]`
    - Update line count queries to use `buf.Nrows()`
4. Update search/replace operations:
    - Modify `search_compute_matches()` to work with GetLine()
    - Update regex matching to work line-by-line or use content directly

**Testing:**

- Test all editing commands (insert, delete, newline, backspace)
- Test region operations (mark, copy, kill)
- Test search and replace
- Test word navigation and deletion
- Run through common editing workflows

**Estimated Effort:** 4-6 days

### Phase 4: Update Renderer and Frontend (Display)

**Goal:** Ensure all renderers work with new Buffer structure.

**Tasks:**

1. Audit renderer implementations:
    - `TerminalRenderer.cc`
    - `ImGuiRenderer.cc`
    - `QtRenderer.cc`
    - `TestRenderer.cc`
2. Update line access patterns:
    - Replace `buf.Rows()[y]` with `buf.GetLine(y)`
    - Handle string return instead of Line object
3. Update syntax highlighting integration:
    - Ensure HighlighterEngine works with GetLine()
    - Update any line-based caching

**Testing:**

- Test rendering in terminal
- Test ImGui frontend (if enabled)
- Test Qt frontend (if enabled)
- Verify syntax highlighting displays correctly
- Test scrolling and viewport updates

**Estimated Effort:** 2-3 days

### Phase 5: Remove Old Infrastructure (Cleanup) ✅ COMPLETED

**Goal:** Remove GapBuffer, AppendBuffer, and Line class completely.

**Status:** Completed on 2025-12-05

**Tasks:**

1. ✅ Remove conditional compilation:
    - Removed `#ifdef KTE_USE_BUFFER_PIECE_TABLE` (PieceTable is now the
      only way)
    - Removed `#ifdef KTE_USE_PIECE_TABLE`
    - Removed `AppendBuffer.h`
2. ✅ Delete obsolete code:
    - Deleted `GapBuffer.h/cc`
    - Line class now uses PieceTable internally (kept for API
      compatibility)
    - `rows_` kept as mutable cache rebuilt from `content_` PieceTable
3. ✅ Update CMakeLists.txt:
    - Removed GapBuffer from sources
    - Removed AppendBuffer.h from headers
    - Removed KTE_USE_PIECE_TABLE and KTE_USE_BUFFER_PIECE_TABLE options
4. ✅ Clean up includes and dependencies
5. ✅ Update documentation

**Testing:**

- Full regression test suite
- Verify clean compilation
- Check for any lingering references

**Estimated Effort:** 1-2 days

### Phase 6: Performance Optimization (Polish)

**Goal:** Optimize the new implementation for real-world usage.

**Tasks:**

1. Profile common operations:
    - Measure line access patterns
    - Identify hot paths in editing
    - Benchmark against old implementation
2. Optimize line index:
    - Consider incremental updates instead of full rebuild
    - Tune rebuild threshold
    - Cache frequently accessed lines
3. Optimize piece table:
    - Tune piece coalescing heuristics
    - Consider piece count limits and consolidation
4. Memory optimization:
    - Review materialization frequency
    - Consider lazy materialization strategies
    - Profile memory usage on large files

**Testing:**

- Benchmark suite with various file sizes
- Memory profiling
- Real-world usage testing

**Estimated Effort:** 3-5 days

## Files Requiring Modification

### Core Files (Must Change)

- `PieceTable.h/cc` - Add new methods (Phase 1)
- `Buffer.h/cc` - Replace rows_ with content_ (Phase 2)
- `Command.cc` - Update line access (Phase 3)
- `UndoSystem.cc` - May need updates for new Buffer API

### Renderer Files (Will Change)

- `TerminalRenderer.cc` - Update line access (Phase 4)
- `ImGuiRenderer.cc` - Update line access (Phase 4)
- `QtRenderer.cc` - Update line access (Phase 4)
- `TestRenderer.cc` - Update line access (Phase 4)

### Files Removed (Phase 5 - Completed)

- `GapBuffer.h/cc` - ✅ Deleted
- `AppendBuffer.h` - ✅ Deleted
- `test_buffer_correctness.cc` - ✅ Deleted (obsolete GapBuffer
  comparison test)
- `bench/BufferBench.cc` - ✅ Deleted (obsolete GapBuffer benchmarks)
- `bench/PerformanceSuite.cc` - ✅ Deleted (obsolete GapBuffer
  benchmarks)
- `Buffer::Line` class - ✅ Updated to use PieceTable internally (kept
  for API compatibility)

### Build Files

- `CMakeLists.txt` - Update sources (Phase 5)

### Documentation

- `README.md` - Update architecture notes
- `docs/` - Update any architectural documentation
- `REWRITE.md` - Note C++ now matches Rust design

## Testing Strategy

### Unit Tests

- **PieceTable Tests:** New file `test_piece_table.cc`
    - Test Insert/Delete at various positions
    - Test line indexing correctness
    - Test position conversion
    - Test with edge cases (empty, single line, large files)

- **Buffer Tests:** Extend `test_buffer_correctness.cc`
    - Test new Buffer API with PieceTable backend
    - Test file I/O round-tripping
    - Test multi-line operations

### Integration Tests

- **Undo Tests:** `test_undo.cc` should still pass
    - Verify undo/redo across all operation types
    - Test undo tree navigation

- **Search Tests:** `test_search_correctness.cc` should still pass
    - Verify search across multiple lines
    - Test regex search

### Manual Testing

- Load and edit large files (>10MB)
- Perform complex editing sequences
- Test all keybindings and commands
- Verify syntax highlighting
- Test crash recovery (swap files)

### Regression Testing

- All existing tests must pass with new implementation
- No observable behavior changes for users
- Performance should be comparable or better

## Risk Assessment

### High Risk

- **Undo System Integration:** Undo records operations with
  row/col/text. Need to ensure compatibility or refactor.
    - *Mitigation:* Carefully preserve undo semantics, extensive testing

- **Performance Regression:** Line index rebuilding could be expensive
  on large files.
    - *Mitigation:* Profile early, optimize incrementally, consider
      caching strategies

### Medium Risk

- **Syntax Highlighting:** Highlighters may depend on line-based access
  patterns.
    - *Mitigation:* Review highlighter integration, test thoroughly

- **Renderer Updates:** Multiple renderers need updating, risk of
  inconsistency.
    - *Mitigation:* Update all renderers in same phase, test each

### Low Risk

- **Search/Replace:** Should work naturally with new GetLine() API.
    - *Mitigation:* Test thoroughly with existing test suite

## Success Criteria

### Functional Requirements

- ✓ All existing tests pass
- ✓ All commands work identically to before
- ✓ File I/O works correctly
- ✓ Undo/redo functionality preserved
- ✓ Syntax highlighting works
- ✓ All frontends (terminal, ImGui, Qt) work

### Code Quality

- ✓ GapBuffer completely removed
- ✓ No conditional compilation for buffer type
- ✓ Clean, maintainable code
- ✓ Good test coverage for new PieceTable methods

### Performance

- ✓ Editing operations at least as fast as current
- ✓ Line access within 2x of current performance
- ✓ Memory usage reasonable (no excessive materialization)
- ✓ Large file handling acceptable (tested up to 100MB)

## Timeline Estimate

| Phase                      | Duration       | Dependencies |
|----------------------------|----------------|--------------|
| Phase 1: Extend PieceTable | 3-5 days       | None         |
| Phase 2: Buffer Adapter    | 3-4 days       | Phase 1      |
| Phase 3: Command Layer     | 4-6 days       | Phase 2      |
| Phase 4: Renderer Updates  | 2-3 days       | Phase 3      |
| Phase 5: Cleanup           | 1-2 days       | Phase 4      |
| Phase 6: Optimization      | 3-5 days       | Phase 5      |
| **Total**                  | **16-25 days** |              |

**Note:** Timeline assumes one developer working full-time. Actual
duration may vary based on:

- Unforeseen integration issues
- Performance optimization needs
- Testing thoroughness
- Code review iterations

## Alternatives Considered

### Alternative 1: Keep Line-based but unify GapBuffer/PieceTable

- Keep vector of Lines, but make each Line always use PieceTable
- Remove GapBuffer, remove AppendBuffer selector
- **Pros:** Smaller change, less risk
- **Cons:** Doesn't achieve architectural goal, still have per-line
  overhead

### Alternative 2: Hybrid approach

- Use PieceTable for buffer, but maintain materialized Line objects as
  cache
- **Pros:** Easier migration, maintains some compatibility
- **Cons:** Complex dual representation, cache invalidation issues

### Alternative 3: Complete rewrite

- Follow REWRITE.md exactly, implement in Rust
- **Pros:** Modern language, better architecture
- **Cons:** Much larger effort, different project

## Recommendation

**Proceed with planned migration** (single PieceTable per Buffer)
because:

1. Aligns with long-term architecture vision (REWRITE.md)
2. Removes unnecessary per-line buffer overhead
3. Simplifies codebase (one text representation)
4. Enables future optimizations (better undo, swap files, etc.)
5. Reasonable effort (16-25 days) for significant improvement

**Suggested Approach:**

- Start with Phase 1 (extend PieceTable) in isolated branch
- Thoroughly test new PieceTable functionality
- Proceed incrementally through phases
- Maintain working editor at end of each phase
- Merge to main after Phase 4 (before cleanup) to get testing
- Complete Phase 5-6 based on feedback

## References

- `REWRITE.md` - Rust architecture specification (lines 54-157)
- Current buffer implementation: `Buffer.h/cc`
- Current piece table: `PieceTable.h/cc`
- Undo system: `UndoSystem.h/cc`, `UndoNode.h`
- Commands: `Command.cc`
