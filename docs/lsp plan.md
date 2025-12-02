# LSP Support Implementation Plan for kte

## Executive Summary

This plan outlines a comprehensive approach to integrating Language Server Protocol (LSP) support into kte while
respecting its core architectural principles: **frontend/backend separation**, **testability**, and **dual terminal/GUI
support**.

---

## 1. Core Architecture

### 1.1 LSP Client Module Structure

```c++
// LspClient.h - Core LSP client abstraction
class LspClient {
public:
    virtual ~LspClient() = default;
    
    // Lifecycle
    virtual bool initialize(const std::string& rootPath) = 0;
    virtual void shutdown() = 0;
    
    // Document Synchronization
    virtual void didOpen(const std::string& uri, const std::string& languageId, 
                         int version, const std::string& text) = 0;
    virtual void didChange(const std::string& uri, int version, 
                           const std::vector<TextDocumentContentChangeEvent>& changes) = 0;
    virtual void didClose(const std::string& uri) = 0;
    virtual void didSave(const std::string& uri) = 0;
    
    // Language Features
    virtual void completion(const std::string& uri, Position pos, 
                           CompletionCallback callback) = 0;
    virtual void hover(const std::string& uri, Position pos, 
                      HoverCallback callback) = 0;
    virtual void definition(const std::string& uri, Position pos, 
                           LocationCallback callback) = 0;
    virtual void references(const std::string& uri, Position pos, 
                           LocationsCallback callback) = 0;
    virtual void diagnostics(DiagnosticsCallback callback) = 0;
    
    // Process Management
    virtual bool isRunning() const = 0;
    virtual std::string getServerName() const = 0;
};
```

### 1.2 Process-based LSP Implementation

```c++
// LspProcessClient.h - Manages LSP server subprocess
class LspProcessClient : public LspClient {
private:
    std::string serverCommand_;
    std::vector<std::string> serverArgs_;
    std::unique_ptr<Process> process_;
    std::unique_ptr<JsonRpcTransport> transport_;
    std::unordered_map<int, PendingRequest> pendingRequests_;
    int nextRequestId_ = 1;
    
    // Async I/O handling
    std::thread readerThread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    
public:
    LspProcessClient(const std::string& command, 
                     const std::vector<std::string>& args);
    // ... implementation of LspClient interface
};
```

### 1.3 JSON-RPC Transport Layer

```c++
// JsonRpcTransport.h
class JsonRpcTransport {
public:
    // Send a request and get the request ID
    int sendRequest(const std::string& method, const nlohmann::json& params);
    
    // Send a notification (no response expected)
    void sendNotification(const std::string& method, const nlohmann::json& params);
    
    // Read next message (blocking)
    std::optional<JsonRpcMessage> readMessage();
    
private:
    void writeMessage(const nlohmann::json& message);
    std::string readContentLength();
    
    int fdIn_;   // stdin to server
    int fdOut_;  // stdout from server
};
```

---

## 2. Incremental Document Updates

### 2.1 Change Tracking in Buffer

The key to efficient LSP integration is tracking changes incrementally. This integrates with the existing `Buffer`
class:

```c++
// TextDocumentContentChangeEvent.h
struct TextDocumentContentChangeEvent {
    std::optional<Range> range;     // If nullopt, entire document changed
    std::optional<int> rangeLength; // Deprecated but some servers use it
    std::string text;
};

// BufferChangeTracker.h - Integrates with Buffer to track changes
class BufferChangeTracker {
public:
    explicit BufferChangeTracker(Buffer* buffer);
    
    // Called by Buffer on each edit operation
    void recordInsertion(Position pos, const std::string& text);
    void recordDeletion(Range range, const std::string& deletedText);
    
    // Get accumulated changes since last sync
    std::vector<TextDocumentContentChangeEvent> getChanges();
    
    // Clear changes after sending to LSP
    void clearChanges();
    
    // Get current document version
    int getVersion() const { return version_; }
    
private:
    Buffer* buffer_;
    int version_ = 0;
    std::vector<TextDocumentContentChangeEvent> pendingChanges_;
    
    // Optional: Coalesce adjacent changes
    void coalesceChanges();
};
```

### 2.2 Integration with Buffer Operations

```c++
// Buffer.h additions
class Buffer {
    // ... existing code ...
    
    // LSP integration
    void setChangeTracker(std::unique_ptr<BufferChangeTracker> tracker);
    BufferChangeTracker* getChangeTracker() { return changeTracker_.get(); }
    
    // These methods should call tracker when present
    void insertText(Position pos, const std::string& text);
    void deleteRange(Range range);
    
private:
    std::unique_ptr<BufferChangeTracker> changeTracker_;
};
```

### 2.3 Sync Strategy Selection

```c++
// LspSyncMode.h
enum class LspSyncMode {
    None,        // No sync
    Full,        // Send full document on each change
    Incremental  // Send only changes (preferred)
};

// Determined during server capability negotiation
LspSyncMode negotiateSyncMode(const ServerCapabilities& caps);
```

---

## 3. Diagnostics Display System

### 3.1 Diagnostic Data Model

```c++
// Diagnostic.h
enum class DiagnosticSeverity {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4
};

struct Diagnostic {
    Range range;
    DiagnosticSeverity severity;
    std::optional<std::string> code;
    std::optional<std::string> source;
    std::string message;
    std::vector<DiagnosticRelatedInformation> relatedInfo;
};

// DiagnosticStore.h - Central storage for diagnostics
class DiagnosticStore {
public:
    void setDiagnostics(const std::string& uri, 
                        std::vector<Diagnostic> diagnostics);
    const std::vector<Diagnostic>& getDiagnostics(const std::string& uri) const;
    std::vector<Diagnostic> getDiagnosticsAtLine(const std::string& uri, 
                                                  int line) const;
    std::optional<Diagnostic> getDiagnosticAtPosition(const std::string& uri,
                                                       Position pos) const;
    void clear(const std::string& uri);
    void clearAll();
    
    // Statistics
    int getErrorCount(const std::string& uri) const;
    int getWarningCount(const std::string& uri) const;
    
private:
    std::unordered_map<std::string, std::vector<Diagnostic>> diagnostics_;
};
```

### 3.2 Frontend-Agnostic Diagnostic Display Interface

Following kte's existing abstraction pattern with `Frontend`, `Renderer`, and `InputHandler`:

```c++
// DiagnosticDisplay.h - Abstract interface for showing diagnostics
class DiagnosticDisplay {
public:
    virtual ~DiagnosticDisplay() = default;
    
    // Update the diagnostic indicators for a buffer
    virtual void updateDiagnostics(const std::string& uri, 
                                   const std::vector<Diagnostic>& diagnostics) = 0;
    
    // Show inline diagnostic at cursor position
    virtual void showInlineDiagnostic(const Diagnostic& diagnostic) = 0;
    
    // Show diagnostic list/panel
    virtual void showDiagnosticList(const std::vector<Diagnostic>& diagnostics) = 0;
    virtual void hideDiagnosticList() = 0;
    
    // Status bar summary
    virtual void updateStatusBar(int errorCount, int warningCount) = 0;
};
```

### 3.3 Terminal Diagnostic Display

```c++
// TerminalDiagnosticDisplay.h
class TerminalDiagnosticDisplay : public DiagnosticDisplay {
public:
    explicit TerminalDiagnosticDisplay(TerminalRenderer* renderer);
    
    void updateDiagnostics(const std::string& uri, 
                          const std::vector<Diagnostic>& diagnostics) override;
    void showInlineDiagnostic(const Diagnostic& diagnostic) override;
    void showDiagnosticList(const std::vector<Diagnostic>& diagnostics) override;
    void hideDiagnosticList() override;
    void updateStatusBar(int errorCount, int warningCount) override;
    
private:
    TerminalRenderer* renderer_;
    
    // Terminal-specific display strategies
    void renderGutterMarkers(const std::vector<Diagnostic>& diagnostics);
    void renderUnderlines(const std::vector<Diagnostic>& diagnostics);
    void renderVirtualText(const Diagnostic& diagnostic);
};
```

**Terminal Display Strategies:**

1. **Gutter markers**: Show `E` (error), `W` (warning), `I` (info), `H` (hint) in left gutter
2. **Underlines**: Use terminal underline/curly underline capabilities (where supported)
3. **Virtual text**: Display diagnostic message at end of line (configurable)
4. **Status line**: `[E:3 W:5]` summary
5. **Message line**: Full diagnostic on cursor line shown in bottom bar

```
1 │ fn main() {
E 2 │     let x: i32 = "hello";
  3 │ }                        
──────────────────────────────────────
error[E0308]: mismatched types       
expected `i32`, found `&str`         
[E:1 W:0] main.rs
```

### 3.4 GUI Diagnostic Display

```c++
// GUIDiagnosticDisplay.h
class GUIDiagnosticDisplay : public DiagnosticDisplay {
public:
    explicit GUIDiagnosticDisplay(GUIRenderer* renderer, GUITheme* theme);
    
    void updateDiagnostics(const std::string& uri, 
                          const std::vector<Diagnostic>& diagnostics) override;
    void showInlineDiagnostic(const Diagnostic& diagnostic) override;
    void showDiagnosticList(const std::vector<Diagnostic>& diagnostics) override;
    void hideDiagnosticList() override;
    void updateStatusBar(int errorCount, int warningCount) override;
    
private:
    GUIRenderer* renderer_;
    GUITheme* theme_;
    
    // GUI-specific display
    void renderWavyUnderlines(const std::vector<Diagnostic>& diagnostics);
    void renderTooltip(Position pos, const Diagnostic& diagnostic);
    void renderDiagnosticPanel();
};
```

**GUI Display Features:**

1. **Wavy underlines**: Classic IDE-style (red for errors, yellow for warnings, etc.)
2. **Gutter icons**: Colored icons/dots in the gutter
3. **Hover tooltips**: Rich tooltips on hover showing full diagnostic
4. **Diagnostic panel**: Bottom panel with clickable diagnostic list
5. **Minimap markers**: Colored marks on the minimap (if present)

---

## 4. LspManager - Central Coordination

```c++
// LspManager.h
class LspManager {
public:
    explicit LspManager(Editor* editor, DiagnosticDisplay* display);
    
    // Server management
    void registerServer(const std::string& languageId, 
                       const LspServerConfig& config);
    bool startServerForBuffer(Buffer* buffer);
    void stopServer(const std::string& languageId);
    void stopAllServers();
    
    // Document sync
    void onBufferOpened(Buffer* buffer);
    void onBufferChanged(Buffer* buffer);
    void onBufferClosed(Buffer* buffer);
    void onBufferSaved(Buffer* buffer);
    
    // Feature requests
    void requestCompletion(Buffer* buffer, Position pos, 
                          CompletionCallback callback);
    void requestHover(Buffer* buffer, Position pos, 
                     HoverCallback callback);
    void requestDefinition(Buffer* buffer, Position pos, 
                          LocationCallback callback);
    
    // Configuration
    void setDebugLogging(bool enabled);
    
private:
    Editor* editor_;
    DiagnosticDisplay* display_;
    DiagnosticStore diagnosticStore_;
    std::unordered_map<std::string, std::unique_ptr<LspClient>> servers_;
    std::unordered_map<std::string, LspServerConfig> serverConfigs_;
    
    void handleDiagnostics(const std::string& uri, 
                          const std::vector<Diagnostic>& diagnostics);
    std::string getLanguageId(Buffer* buffer);
    std::string getUri(Buffer* buffer);
};
```

---

## 5. Configuration

```c++
// LspServerConfig.h
struct LspServerConfig {
    std::string command;
    std::vector<std::string> args;
    std::vector<std::string> filePatterns;  // e.g., {"*.rs", "*.toml"}
    std::string rootPatterns;               // e.g., "Cargo.toml"
    LspSyncMode preferredSyncMode = LspSyncMode::Incremental;
    bool autostart = true;
    std::unordered_map<std::string, nlohmann::json> initializationOptions;
    std::unordered_map<std::string, nlohmann::json> settings;
};

// Default configurations
std::vector<LspServerConfig> getDefaultServerConfigs() {
    return {
        {
            .command = "rust-analyzer",
            .filePatterns = {"*.rs"},
            .rootPatterns = "Cargo.toml"
        },
        {
            .command = "clangd",
            .args = {"--background-index"},
            .filePatterns = {"*.c", "*.cc", "*.cpp", "*.h", "*.hpp"},
            .rootPatterns = "compile_commands.json"
        },
        {
            .command = "gopls",
            .filePatterns = {"*.go"},
            .rootPatterns = "go.mod"
        },
        // ... more servers
    };
}
```

---

## 6. Implementation Phases

### Phase 1: Foundation (2-3 weeks)

- [ ] JSON-RPC transport layer
- [ ] Process management for LSP servers
- [ ] Basic `LspClient` with initialize/shutdown
- [ ] `textDocument/didOpen`, `textDocument/didClose` (full sync)

### Phase 2: Incremental Sync (1-2 weeks)

- [ ] `BufferChangeTracker` integration with `Buffer`
- [ ] `textDocument/didChange` with incremental updates
- [ ] Change coalescing for rapid edits
- [ ] Version tracking

### Phase 3: Diagnostics (2-3 weeks)

- [ ] `DiagnosticStore` implementation
- [ ] `TerminalDiagnosticDisplay` with gutter markers & status line
- [ ] `GUIDiagnosticDisplay` with wavy underlines & tooltips
- [ ] `textDocument/publishDiagnostics` handling

### Phase 4: Language Features (3-4 weeks)

- [ ] Completion (`textDocument/completion`)
- [ ] Hover (`textDocument/hover`)
- [ ] Go to definition (`textDocument/definition`)
- [ ] Find references (`textDocument/references`)
- [ ] Code actions (`textDocument/codeAction`)

### Phase 5: Polish & Advanced Features (2-3 weeks)

- [ ] Multiple server support
- [ ] Server auto-detection
- [ ] Configuration file support
- [ ] Workspace symbol search
- [ ] Rename refactoring

---

## 7. Alignment with kte Core Principles

### 7.1 Frontend/Backend Separation

- LSP logic is completely separate from display
- `DiagnosticDisplay` interface allows identical behavior across Terminal/GUI
- Follows existing pattern: `Renderer`, `InputHandler`, `Frontend`

### 7.2 Testability

- `LspClient` is abstract, enabling `MockLspClient` for testing
- `DiagnosticDisplay` can be mocked for testing diagnostic flow
- Change tracking can be unit tested in isolation

### 7.3 Performance

- Incremental sync minimizes data sent to LSP servers
- Async message handling doesn't block UI
- Diagnostic rendering is batched

### 7.4 Simplicity

- Minimal dependencies (nlohmann/json for JSON handling)
- Self-contained process management
- Clear separation of concerns

---

## 8. File Organization

```
kte/
├── lsp/
│   ├── LspClient.h
│   ├── LspProcessClient.h
│   ├── LspProcessClient.cc
│   ├── LspManager.h
│   ├── LspManager.cc
│   ├── LspServerConfig.h
│   ├── JsonRpcTransport.h
│   ├── JsonRpcTransport.cc
│   ├── LspTypes.h           # Position, Range, Location, etc.
│   ├── Diagnostic.h
│   ├── DiagnosticStore.h
│   ├── DiagnosticStore.cc
│   └── BufferChangeTracker.h
├── diagnostic/
│   ├── DiagnosticDisplay.h
│   ├── TerminalDiagnosticDisplay.h
│   ├── TerminalDiagnosticDisplay.cc
│   ├── GUIDiagnosticDisplay.h
│   └── GUIDiagnosticDisplay.cc
```

---

## 9. Dependencies

- **nlohmann/json**: JSON parsing/serialization (header-only)
- **POSIX/Windows process APIs**: For spawning LSP servers
- Existing kte infrastructure: `Buffer`, `Renderer`, `Frontend`, etc.

---

This plan provides a solid foundation for LSP support while maintaining kte's clean architecture. The key insight is
that LSP is fundamentally a backend feature that should be displayed through the existing frontend abstraction layer,
ensuring consistent behavior across terminal and GUI modes.