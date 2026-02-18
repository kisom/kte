# Error Propagation Standardization Report

**Project:** kte (Kyle's Text Editor)  
**Date:** 2026-02-17  
**Auditor:** Error Propagation Standardization Review  
**Language:** C++20

---

## Executive Summary

This report documents the standardization of error propagation patterns
across the kte codebase. Following the implementation of centralized
error handling (ErrorHandler), this audit identifies inconsistencies in
error propagation and provides concrete remediation recommendations.

**Key Findings:**

- **Dominant Pattern**: `bool + std::string &err` is used consistently
  in Buffer and SwapManager for I/O operations
- **Inconsistencies**: PieceTable has no error reporting mechanism; some
  internal helpers lack error propagation
- **Standard Chosen**: `bool + std::string &err` pattern (C++20 project,
  std::expected not available)
- **Documentation**: Comprehensive error handling conventions added to
  DEVELOPER_GUIDE.md

**Overall Assessment**: The codebase has a **solid foundation** with the
`bool + err` pattern used consistently in critical I/O paths. Primary
gaps are in PieceTable memory allocation error handling and some
internal helper functions.

---

## 1. CURRENT STATE ANALYSIS

### 1.1 Error Propagation Patterns Found

#### Pattern 1: `bool + std::string &err` (Dominant)

**Usage**: File I/O, swap operations, resource allocation

**Examples**:

- `Buffer::OpenFromFile(const std::string &path, std::string &err)` (
  Buffer.h:72)
- `Buffer::Save(std::string &err)` (Buffer.h:74)
- `Buffer::SaveAs(const std::string &path, std::string &err)` (Buffer.h:
  75)
- `Editor::OpenFile(const std::string &path, std::string &err)` (
  Editor.h:536)
-
`SwapManager::ReplayFile(Buffer &buf, const std::string &swap_path, std::string &err)` (
Swap.h:104)
-
`SwapManager::open_ctx(JournalCtx &ctx, const std::string &path, std::string &err)` (
Swap.h:208)
-
`SwapManager::compact_to_checkpoint(JournalCtx &ctx, const std::vector<std::uint8_t> &chkpt_record, std::string &err)` (
Swap.h:212-213)

**Assessment**: ✅ **Excellent** - Consistent, well-implemented,
integrated with ErrorHandler

#### Pattern 2: `void` (State Changes)

**Usage**: Setters, cursor movement, flag toggles, internal state
modifications

**Examples**:

- `Buffer::SetCursor(std::size_t x, std::size_t y)` (Buffer.h:348)
- `Buffer::SetDirty(bool d)` (Buffer.h:368)
- `Buffer::SetMark(std::size_t x, std::size_t y)` (Buffer.h:387)
- `Buffer::insert_text(int row, int col, std::string_view text)` (
  Buffer.h:545)
- `Buffer::delete_text(int row, int col, std::size_t len)` (Buffer.h:
  547)
- `Editor::SetStatus(const std::string &msg)` (Editor.h:various)

**Assessment**: ✅ **Appropriate** - These operations are infallible
state changes

#### Pattern 3: `bool` without error parameter (Control Flow)

**Usage**: Validation checks, control flow decisions

**Examples**:

- `Editor::ProcessPendingOpens()` (Editor.h:544)
- `Editor::ResolveRecoveryPrompt(bool yes)` (Editor.h:558)
- `Editor::SwitchTo(std::size_t index)` (Editor.h:563)
- `Editor::CloseBuffer(std::size_t index)` (Editor.h:565)

**Assessment**: ✅ **Appropriate** - Success/failure is sufficient for
control flow

#### Pattern 4: No Error Reporting (PieceTable)

**Usage**: Memory allocation, text manipulation

**Examples**:

- `void PieceTable::Reserve(std::size_t newCapacity)` (PieceTable.h:71)
- `void PieceTable::Append(const char *s, std::size_t len)` (
  PieceTable.h:75)
-
`void PieceTable::Insert(std::size_t byte_offset, const char *text, std::size_t len)` (
PieceTable.h:118)
- `char *PieceTable::Data()` (PieceTable.h:89-93) - returns nullptr on
  allocation failure

**Assessment**: ⚠️ **Gap** - Memory allocation failures are not reported

---

## 2. STANDARDIZATION DECISION

### 2.1 Chosen Pattern: `bool + std::string &err`

**Rationale**:

1. **C++20 Project**: `std::expected` (C++23) is not available
2. **Existing Adoption**: Already used consistently in Buffer,
   SwapManager, Editor for I/O operations
3. **Clear Semantics**: `bool` return indicates success/failure, `err`
   provides details
4. **ErrorHandler Integration**: Works seamlessly with centralized error
   logging
5. **Zero Overhead**: No exceptions, no dynamic allocation for error
   paths
6. **Testability**: Easy to verify error messages in unit tests

**Alternative Considered**: `std::expected<T, std::string>` (C++23)

- **Rejected**: Requires C++23, would require major refactoring, not
  available in current toolchain

### 2.2 Pattern Selection Guidelines

| Operation Type      | Pattern                   | Example                                                                           |
|---------------------|---------------------------|-----------------------------------------------------------------------------------|
| File I/O            | `bool + std::string &err` | `Buffer::Save(std::string &err)`                                                  |
| Syscalls            | `bool + std::string &err` | `open_ctx(JournalCtx &ctx, const std::string &path, std::string &err)`            |
| Resource Allocation | `bool + std::string &err` | Future: `PieceTable::Reserve(std::size_t cap, std::string &err)`                  |
| Parsing/Validation  | `bool + std::string &err` | `SwapManager::ReplayFile(Buffer &buf, const std::string &path, std::string &err)` |
| State Changes       | `void`                    | `Buffer::SetCursor(std::size_t x, std::size_t y)`                                 |
| Control Flow        | `bool` (no err)           | `Editor::SwitchTo(std::size_t index)`                                             |

---

## 3. INCONSISTENCIES AND GAPS

### 3.1 PieceTable Memory Allocation (Severity: 6/10)

**Finding**: PieceTable methods that allocate memory (`Reserve`,
`Append`, `Insert`, `Data`) do not report allocation failures.

**Impact**:

- Memory allocation failures are silent
- `Data()` returns `nullptr` on failure, but callers may not check
- Large file operations could fail without user notification

**Evidence**:

```cpp
// PieceTable.h:71
void Reserve(std::size_t newCapacity);  // No error reporting

// PieceTable.h:89-93
char *Data();  // Returns nullptr on allocation failure
```

**Remediation Priority**: **Medium** - Memory allocation failures are
rare on modern systems, but should be handled for robustness

**Recommended Fix**:

**Option 1: Add error parameter to fallible operations** (Preferred)

```cpp
// PieceTable.h
bool Reserve(std::size_t newCapacity, std::string &err);
bool Append(const char *s, std::size_t len, std::string &err);
bool Insert(std::size_t byte_offset, const char *text, std::size_t len, std::string &err);

// Returns nullptr on failure; check with HasMaterializationError()
char *Data();
bool HasMaterializationError() const;
std::string GetMaterializationError() const;
```

**Option 2: Use exceptions for allocation failures** (Not recommended)

PieceTable could throw `std::bad_alloc` on allocation failures, but this
conflicts with the project's error handling philosophy and would require
exception handling throughout the codebase.

**Option 3: Status quo with improved documentation** (Minimal change)

Document that `Data()` can return `nullptr` and callers must check. Add
assertions in debug builds.

```cpp
// PieceTable.h
// Returns pointer to materialized buffer, or nullptr if materialization fails.
// Callers MUST check for nullptr before dereferencing.
char *Data();
```

**Recommendation**: **Option 3** for now (document + assertions), *
*Option 1** if memory allocation errors become a concern in production.

### 3.2 Internal Helper Functions (Severity: 4/10)

**Finding**: Some internal helper functions in Swap.cc and Buffer.cc use
`bool` returns without error parameters.

**Examples**:

```cpp
// Swap.cc:562
static bool ensure_parent_dir(const std::string &path);  // No error details

// Swap.cc:579
static bool write_header(int fd);  // No error details

// Buffer.cc:101
static bool write_all_fd(int fd, const char *data, std::size_t len, std::string &err);  // ✅ Good
```

**Impact**: Limited - These are internal helpers called by functions
that do report errors

**Remediation Priority**: **Low** - Callers already provide error
context

**Recommended Fix**: Add error parameters to internal helpers for
consistency

```cpp
// Swap.cc
static bool ensure_parent_dir(const std::string &path, std::string &err);
static bool write_header(int fd, std::string &err);
```

**Status**: **Deferred** - Low priority, callers already provide
adequate error context

### 3.3 Editor Control Flow Methods (Severity: 2/10)

**Finding**: Editor methods like `SwitchTo()`, `CloseBuffer()` return
`bool` without error details.

**Assessment**: ✅ **Appropriate** - These are control flow decisions
where success/failure is sufficient

**Remediation**: **None needed** - Current pattern is correct for this
use case

---

## 4. ERRORHANDLER INTEGRATION STATUS

### 4.1 Components with ErrorHandler Integration

✅ **Buffer** (Buffer.cc)

- `OpenFromFile()` - Reports file open, seek, read errors
- `Save()` - Reports write errors
- `SaveAs()` - Reports write errors

✅ **SwapManager** (Swap.cc)

- `report_error()` - All swap file errors reported
- Background thread errors captured and logged
- Errno captured for all syscalls

✅ **main** (main.cc)

- Top-level exception handler reports Critical errors
- Both `std::exception` and unknown exceptions captured

### 4.2 Components Without ErrorHandler Integration

⚠️ **PieceTable** (PieceTable.cc)

- No error reporting mechanism
- Memory allocation failures are silent

⚠️ **Editor** (Editor.cc)

- File operations delegate to Buffer (✅ covered)
- Control flow methods don't need error reporting (✅ appropriate)

⚠️ **Command** (Command.cc)

- Commands use `Editor::SetStatus()` for user-facing messages
- No ErrorHandler integration for command failures
- **Assessment**: Commands are user-initiated actions; status messages
  are appropriate

---

## 5. DOCUMENTATION STATUS

### 5.1 Error Handling Conventions (DEVELOPER_GUIDE.md)

✅ **Added comprehensive section** covering:

- Three standard error propagation patterns
- Pattern selection guidelines with decision tree
- ErrorHandler integration requirements
- Code examples for file I/O, syscalls, background threads, top-level
  handlers
- Anti-patterns and best practices
- Error log location and format
- Migration guide for updating existing code

**Location**: `docs/DEVELOPER_GUIDE.md` section 7

### 5.2 API Documentation

⚠️ **Gap**: Individual function documentation in headers could be
improved

**Recommendation**: Add brief comments to public APIs documenting error
behavior

```cpp
// Buffer.h
// Opens a file and loads its content into the buffer.
// Returns false on failure; err contains detailed error message.
// Errors are logged to ErrorHandler.
bool OpenFromFile(const std::string &path, std::string &err);
```

---

## 6. REMEDIATION RECOMMENDATIONS

### 6.1 High Priority (Severity 7-10)

**None identified** - Critical error handling gaps were addressed in
previous sessions:

- ✅ Top-level exception handler added (Severity 9/10)
- ✅ Background thread error reporting added (Severity 9/10)
- ✅ File I/O error checking added (Severity 8/10)
- ✅ Errno capture added to swap operations (Severity 7/10)
- ✅ Centralized error handling implemented (Severity 7/10)

### 6.2 Medium Priority (Severity 4-6)

#### 6.2.1 PieceTable Memory Allocation Error Handling (Severity: 6/10)

**Action**: Document that `Data()` can return `nullptr` and add debug
assertions

**Implementation**:

```cpp
// PieceTable.h
// Returns pointer to materialized buffer, or nullptr if materialization fails
// due to memory allocation error. Callers MUST check for nullptr.
char *Data();

// PieceTable.cc
char *PieceTable::Data() {
    materialize();
    assert(materialized_ != nullptr && "PieceTable materialization failed");
    return materialized_;
}
```

**Effort**: Low (documentation + assertions)  
**Risk**: Low (no API changes)  
**Timeline**: Next maintenance cycle

#### 6.2.2 Add Error Parameters to Internal Helpers (Severity: 4/10)

**Action**: Add `std::string &err` parameters to `ensure_parent_dir()`
and `write_header()`

**Implementation**:

```cpp
// Swap.cc
static bool ensure_parent_dir(const std::string &path, std::string &err) {
    try {
        fs::path p(path);
        fs::path dir = p.parent_path();
        if (dir.empty())
            return true;
        if (!fs::exists(dir))
            fs::create_directories(dir);
        return true;
    } catch (const std::exception &e) {
        err = std::string("Failed to create directory: ") + e.what();
        return false;
    } catch (...) {
        err = "Failed to create directory: unknown error";
        return false;
    }
}
```

**Effort**: Low (update 2 functions + call sites)  
**Risk**: Low (internal helpers only)  
**Timeline**: Next maintenance cycle

### 6.3 Low Priority (Severity 1-3)

#### 6.3.1 Add Function-Level Error Documentation (Severity: 3/10)

**Action**: Add brief comments to public APIs documenting error behavior

**Effort**: Medium (many functions to document)  
**Risk**: None (documentation only)  
**Timeline**: Ongoing as code is touched

#### 6.3.2 Add ErrorHandler Integration to Commands (Severity: 2/10)

**Action**: Consider logging command failures to ErrorHandler for
diagnostics

**Assessment**: **Not recommended** - Commands are user-initiated
actions; status messages are more appropriate than error logs

---

## 7. TESTING RECOMMENDATIONS

### 7.1 Error Handling Test Coverage

**Current State**:

- ✅ Swap file error handling tested (test_swap_edge_cases.cc)
- ✅ Buffer I/O error handling tested (test_buffer_io.cc)
- ⚠️ PieceTable allocation failure testing missing

**Recommendations**:

1. **Add PieceTable allocation failure tests** (if Option 1 from 3.1 is
   implemented)
2. **Add ErrorHandler query tests** - Verify error logging and retrieval
3. **Add errno capture tests** - Verify errno is captured correctly in
   syscall failures

### 7.2 Test Examples

```cpp
// test_error_handler.cc
TEST(ErrorHandler, LogsErrorsWithContext) {
    ErrorHandler::Instance().Error("TestComponent", "Test error", "test.txt");
    EXPECT_TRUE(ErrorHandler::Instance().HasErrors());
    EXPECT_EQ(ErrorHandler::Instance().GetErrorCount(), 1);
    std::string last = ErrorHandler::Instance().GetLastError();
    EXPECT_TRUE(last.find("Test error") != std::string::npos);
    EXPECT_TRUE(last.find("test.txt") != std::string::npos);
}

// test_piece_table.cc (if Option 1 implemented)
TEST(PieceTable, ReportsAllocationFailure) {
    PieceTable pt;
    std::string err;
    // Attempt to allocate huge buffer
    bool ok = pt.Reserve(SIZE_MAX, err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
}
```

---

## 8. MIGRATION CHECKLIST

For developers updating existing code to follow error handling
conventions:

- [ ] Identify all error-prone operations (file I/O, syscalls,
  allocations)
- [ ] Add `std::string &err` parameter if not present
- [ ] Clear `err` at function start: `err.clear();`
- [ ] Capture `errno` immediately after syscall failures:
  `int saved_errno = errno;`
- [ ] Build detailed error messages with context (paths, operation
  details)
- [ ] Call `ErrorHandler::Instance().Error()` at all error sites
- [ ] Return `false` on failure, `true` on success
- [ ] Update all call sites to handle the error parameter
- [ ] Write unit tests that verify error handling
- [ ] Update function documentation to describe error behavior

---

## 9. SUMMARY AND NEXT STEPS

### 9.1 Achievements

✅ **Standardized on `bool + std::string &err` pattern** for error-prone
operations  
✅ **Documented comprehensive error handling conventions** in
DEVELOPER_GUIDE.md  
✅ **Identified and prioritized remaining gaps** (PieceTable, internal
helpers)  
✅ **Integrated ErrorHandler** into Buffer, SwapManager, and main  
✅ **Established clear pattern selection guidelines** for future
development

### 9.2 Remaining Work

**Medium Priority**:

1. Document PieceTable `Data()` nullptr behavior and add assertions
2. Add error parameters to internal helper functions

**Low Priority**:

3. Add function-level error documentation to public APIs
4. Add ErrorHandler query tests

### 9.3 Conclusion

The kte codebase has achieved **strong error handling consistency** with
the `bool + std::string &err` pattern used uniformly across critical I/O
paths. The centralized ErrorHandler provides comprehensive logging and
UI integration. Remaining gaps are minor and primarily affect edge
cases (memory allocation failures) that are rare in practice.

**Overall Grade**: **B+ (8.5/10)**

**Strengths**:

- Consistent error propagation in Buffer and SwapManager
- Comprehensive ErrorHandler integration
- Excellent documentation in DEVELOPER_GUIDE.md
- Errno capture for all syscalls
- Top-level exception handling

**Areas for Improvement**:

- PieceTable memory allocation error handling
- Internal helper function error propagation
- Function-level API documentation

The error handling infrastructure is **production-ready** and provides a
solid foundation for reliable operation and debugging.
