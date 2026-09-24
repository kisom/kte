/*
 * Buffer.h - editor buffer representing an open document
 *
 * Buffer is the central document model in kte. Each Buffer represents one open file
 * or scratch document and manages:
 *
 * - Content storage: Uses PieceTable for efficient text operations
 * - Cursor state: Current position (curx_, cury_)
 * - Viewport: Scroll offsets (rowoffs_, coloffs_) for display
 * - File backing: Optional association with a file on disk
 * - Undo/Redo: Integrated UndoSystem for operation history
 * - Syntax highlighting: Optional HighlighterEngine for language-aware coloring
 * - Swap/crash recovery: Integration with SwapRecorder for journaling
 * - Dirty tracking: Modification state for save prompts
 *
 * Key concepts:
 *
 * 1. Cursor coordinates:
 *    - (curx_, cury_): Logical character position in the document
 *
 * 2. File backing:
 *    - Buffers can be file-backed (associated with a path) or scratch (unnamed)
 *    - File identity tracking detects external modifications
 *
 * 3. Content access:
 *    - Nrows(), GetLineString(): per-line access
 *    - GetLineView(): line access via string_view (zero-copy within a piece)
 *    - Direct PieceTable access for new editing operations
 */
#pragma once
#include <atomic>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <string_view>

#include "PieceTable.h"
#include "UndoSystem.h"
#include <cstdint>
#include "syntax/HighlighterEngine.h"
#include "Highlight.h"
#include <mutex>

// Edit mode determines which font class is used for a buffer.
enum class EditMode { Code, Writing };

// Detect edit mode from a filename's extension.
inline EditMode
DetectEditMode(const std::string &filename)
{
	std::string ext = std::filesystem::path(filename).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	static const std::unordered_set<std::string> writing_exts = {
		".txt", ".md", ".markdown", ".rst", ".org",
		".tex", ".adoc", ".asciidoc",
	};
	if (writing_exts.count(ext))
		return EditMode::Writing;
	return EditMode::Code;
}

// Forward declaration for swap journal integration
namespace kte {
class SwapRecorder;
}


class Buffer {
public:
	Buffer();

	Buffer(const Buffer &other);

	Buffer &operator=(const Buffer &other);

	Buffer(Buffer &&other) noexcept;

	Buffer &operator=(Buffer &&other) noexcept;

	explicit Buffer(const std::string &path);

	// File operations
	bool OpenFromFile(const std::string &path, std::string &err);

	bool Save(std::string &err) const; // saves to existing filename; returns false if not file-backed
	bool SaveAs(const std::string &path, std::string &err); // saves to path and makes buffer file-backed

	// External modification detection.
	// Returns true if the file on disk differs from the last observed identity recorded
	// on open/save.
	[[nodiscard]] bool ExternallyModifiedOnDisk() const;

	// Refresh the stored on-disk identity to match current stat (used after open/save).
	void RefreshOnDiskIdentity();

	// Accessors
	[[nodiscard]] std::size_t Curx() const
	{
		return curx_;
	}


	[[nodiscard]] std::size_t Cury() const
	{
		return cury_;
	}


	[[nodiscard]] std::size_t Nrows() const
	{
		return content_LineCount_();
	}


	[[nodiscard]] std::size_t Rowoffs() const
	{
		return rowoffs_;
	}


	[[nodiscard]] std::size_t Coloffs() const
	{
		return coloffs_;
	}


	// A line's text without its trailing newline.
	[[nodiscard]] std::string GetLineString(std::size_t row) const
	{
		return content_.GetLine(row);
	}


	[[nodiscard]] std::pair<std::size_t, std::size_t> GetLineRange(std::size_t row) const
	{
		return content_.GetLineRange(row);
	}


	// View of a line's raw bytes, including its trailing '\n' if any. No copy
	// when the line lies within one piece (an unedited stretch); a line that
	// straddles an edit materializes the whole buffer, so per-row loops over
	// an edited buffer should prefer GetLineString(). Invalid after the next
	// edit; views of different lines need not be contiguous.
	[[nodiscard]] std::string_view GetLineView(std::size_t row) const;

	// The whole text as one contiguous view (materializes an edited buffer).
	// Invalid after the next edit.
	[[nodiscard]] std::string_view ContentView() const
	{
		return content_.ContentView();
	}


	// A copy of the whole text, assembled from the pieces (no materialized
	// copy is kept).
	[[nodiscard]] std::string Bytes() const;


	[[nodiscard]] const std::string &Filename() const
	{
		return filename_;
	}


	// Set a virtual (non file-backed) display name for this buffer, e.g. "+HELP+"
	// This does not mark the buffer as file-backed.
	void SetVirtualName(const std::string &name)
	{
		filename_       = name;
		is_file_backed_ = false;
		is_virtual_     = true;
	}


	[[nodiscard]] bool IsFileBacked() const
	{
		return is_file_backed_;
	}


	// A virtual buffer (e.g. "+HELP+") has a display name that is not a path:
	// it is never journaled, and saving it asks for a file name.
	[[nodiscard]] bool IsVirtual() const
	{
		return is_virtual_;
	}


	// Identity of this document, stable for its lifetime, including when the
	// Buffer object is moved (e.g. by the editor's buffer vector). Copies get
	// a new id. Use it, not the object's address, to key per-buffer caches.
	[[nodiscard]] std::uint64_t Id() const
	{
		return id_;
	}


	[[nodiscard]] bool Dirty() const
	{
		return dirty_;
	}


	// Read-only flag
	[[nodiscard]] bool IsReadOnly() const
	{
		return read_only_;
	}


	void SetReadOnly(bool ro)
	{
		read_only_ = ro;
	}


	void ToggleReadOnly()
	{
		read_only_ = !read_only_;
	}


	void SetCursor(const std::size_t x, const std::size_t y)
	{
		curx_ = x;
		cury_ = y;
	}


	void SetOffsets(const std::size_t row, const std::size_t col)
	{
		rowoffs_ = row;
		coloffs_ = col;
	}


	void SetDirty(bool d)
	{
		// The version tracks content, not the dirty flag: every text mutation
		// bumps it (see edited_at_), including undo/redo back to the saved state.
		dirty_ = d;
	}


	// Whole-buffer content change (load, reload, replace): bump the version
	// and drop all cached highlighting.
	void MarkContentChanged()
	{
		edited_at_(0);
	}


	// Mark support
	void ClearMark()
	{
		mark_set_ = false;
	}


	void SetMark(const std::size_t x, const std::size_t y)
	{
		mark_set_  = true;
		mark_curx_ = x;
		mark_cury_ = y;
	}


	[[nodiscard]] bool MarkSet() const
	{
		return mark_set_;
	}


	[[nodiscard]] std::size_t MarkCurx() const
	{
		return mark_curx_;
	}


	[[nodiscard]] std::size_t MarkCury() const
	{
		return mark_cury_;
	}


	// Visual-line selection support (multicursor/visual mode)
	void VisualLineClear()
	{
		visual_line_active_ = false;
	}


	void VisualLineStart()
	{
		visual_line_active_   = true;
		visual_line_anchor_y_ = cury_;
		visual_line_active_y_ = cury_;
	}


	void VisualLineToggle()
	{
		if (visual_line_active_)
			VisualLineClear();
		else
			VisualLineStart();
	}


	[[nodiscard]] bool VisualLineActive() const
	{
		return visual_line_active_;
	}


	void VisualLineSetActiveY(std::size_t y)
	{
		visual_line_active_y_ = y;
	}


	// The selected rows, clamped to the buffer: they are stored as row numbers
	// and go stale when lines are removed, and commands must never act on rows
	// past the end (edits there were misplaced and could not be undone).
	[[nodiscard]] std::size_t VisualLineStartY() const
	{
		const std::size_t y = visual_line_anchor_y_ < visual_line_active_y_ ? visual_line_anchor_y_
		                                                                      : visual_line_active_y_;
		return clamp_row_(y);
	}


	[[nodiscard]] std::size_t VisualLineEndY() const
	{
		const std::size_t y = visual_line_anchor_y_ < visual_line_active_y_ ? visual_line_active_y_
		                                                                      : visual_line_anchor_y_;
		return clamp_row_(y);
	}


	// In visual-line (multi-cursor) mode, the UI should highlight only the per-line
	// cursor "spot" (Curx clamped to each line length), not the entire line.
	[[nodiscard]] bool VisualLineSpotSelected(std::size_t y, std::size_t sx) const
	{
		if (!visual_line_active_)
			return false;
		if (y < VisualLineStartY() || y > VisualLineEndY())
			return false;
		std::string_view ln = GetLineView(y);
		// `GetLineView()` returns the raw range, which may include a trailing '\n'.
		if (!ln.empty() && ln.back() == '\n')
			ln.remove_suffix(1);
		const std::size_t spot = std::min(Curx(), ln.size());
		return sx == spot;
	}


	[[nodiscard]] std::string AsString() const;

private:
	[[nodiscard]] std::size_t clamp_row_(std::size_t y) const
	{
		const std::size_t n = Nrows();
		return (n == 0) ? 0 : (y < n ? y : n - 1);
	}

public:

	// Syntax highlighting integration (per-buffer)
	[[nodiscard]] std::uint64_t Version() const
	{
		return version_;
	}


	// Edit mode (code vs writing)
	[[nodiscard]] EditMode GetEditMode() const
	{
		return edit_mode_;
	}


	void SetEditMode(EditMode m)
	{
		edit_mode_ = m;
		edit_mode_detected_ = true;
	}


	void ToggleEditMode()
	{
		edit_mode_ = (edit_mode_ == EditMode::Code)
			             ? EditMode::Writing
			             : EditMode::Code;
		edit_mode_detected_ = true;
	}


	[[nodiscard]] bool EditModeDetected() const
	{
		return edit_mode_detected_;
	}


	void SetSyntaxEnabled(bool on)
	{
		syntax_enabled_ = on;
	}


	[[nodiscard]] bool SyntaxEnabled() const
	{
		return syntax_enabled_;
	}


	// Marks that the user explicitly set syntax state via a command (:syntax
	// on/off, :set filetype=...), as opposed to it being auto-applied from
	// GUIConfig. Frontends that re-apply the config-driven default every
	// frame (e.g. ImGuiFrontend::apply_syntax_to_buffer) must check this
	// first, mirroring EditModeDetected()'s "don't stomp a manual toggle"
	// pattern - otherwise a manual :syntax off is silently undone on the very
	// next frame.
	void SetSyntaxUserOverride(bool on)
	{
		syntax_user_override_ = on;
	}


	[[nodiscard]] bool SyntaxUserOverride() const
	{
		return syntax_user_override_;
	}


	void SetFiletype(const std::string &ft)
	{
		filetype_ = ft;
	}


	[[nodiscard]] const std::string &Filetype() const
	{
		return filetype_;
	}


	[[nodiscard]] kte::HighlighterEngine *Highlighter()
	{
		return highlighter_.get();
	}


	[[nodiscard]] const kte::HighlighterEngine *Highlighter() const
	{
		return highlighter_.get();
	}


	void EnsureHighlighter()
	{
		if (!highlighter_)
			highlighter_ = std::make_unique<kte::HighlighterEngine>();
	}


	// Swap journal integration (set by Editor)
	void SetSwapRecorder(kte::SwapRecorder *rec)
	{
		swap_rec_ = rec;
	}


	[[nodiscard]] kte::SwapRecorder *SwapRecorder() const
	{
		return swap_rec_;
	}


	// Raw, low-level editing APIs used by UndoSystem apply().
	// These must NOT trigger undo recording. They also do not move the cursor.
	void insert_text(int row, int col, std::string_view text);

	void delete_text(int row, int col, std::size_t len);

	void split_line(int row, int col);

	void join_lines(int row);

	void insert_row(int row, std::string_view text);

	void delete_row(int row);

	// Insert the bytes of storage spans (see TextSpan.h) at (row, col)
	// without copying them into storage again. Used by undo for large texts.
	void insert_spans(int row, int col, const std::vector<TextSpan> &spans);

	// Replace the entire buffer content with raw bytes.
	// Intended for crash recovery (swap replay) and test harnesses.
	// This does not trigger swap or undo recording, and clears undo history
	// (recorded against the previous content, which it also references).
	void replace_all_bytes(std::string_view bytes);

	// ===== Span access for undo (see TextSpan.h) =====
	[[nodiscard]] std::vector<TextSpan> SpansAt(int row, int col, std::size_t len) const;

	// Spans removed by the most recent deletion, if it was exactly `len`
	// bytes at (row, col) (see PieceTable::TakeDeletedSpans).
	[[nodiscard]] std::vector<TextSpan> TakeDeletedSpans(int row, int col, std::size_t len);

	[[nodiscard]] bool SpansEqual(const std::vector<TextSpan> &spans, std::string_view text) const
	{
		return content_.SpansEqual(spans, text);
	}


	template<typename Fn>
	void VisitSpans(const std::vector<TextSpan> &spans, Fn &&fn) const
	{
		content_.VisitSpans(spans, std::forward<Fn>(fn));
	}


	// Total size of the content in bytes.
	[[nodiscard]] std::size_t ContentBytes() const
	{
		return content_.Size();
	}


	// Byte offset of (row, col) in the content (clamped), and back.
	[[nodiscard]] std::size_t RowColToOffset(std::size_t row, std::size_t col) const
	{
		return content_.LineColToByteOffset(row, col);
	}


	[[nodiscard]] std::pair<std::size_t, std::size_t> OffsetToRowCol(std::size_t off) const
	{
		return content_.ByteOffsetToLineCol(off);
	}

	// Undo system accessors (created per-buffer)
	[[nodiscard]] UndoSystem *Undo();

	[[nodiscard]] const UndoSystem *Undo() const;

#if defined(KTE_TESTS)
	// Test-only: return the raw buffer bytes (including newlines) as a string.

	// Every line without its newline (tests only).
	[[nodiscard]] std::vector<std::string> LinesForTests() const;

	[[nodiscard]] std::string BytesForTests() const;
#endif

private:
	struct FileIdentity {
		bool valid             = false;
		std::uint64_t mtime_ns = 0;
		std::uint64_t size     = 0;
		std::uint64_t dev      = 0;
		std::uint64_t ino      = 0;
	};

	[[nodiscard]] static bool stat_identity(const std::string &path, FileIdentity &out);

	[[nodiscard]] bool current_disk_identity(FileIdentity &out) const;

	mutable FileIdentity on_disk_identity_{};

	// State mirroring original C struct (without undo_tree)
	std::size_t curx_    = 0, cury_ = 0; // cursor position in characters
	std::size_t rowoffs_ = 0, coloffs_ = 0; // viewport offsets
	// PieceTable is the source of truth.
	PieceTable content_{};

	// Helper to query content_.LineCount() while keeping header minimal
	std::size_t content_LineCount_() const;

	std::string filename_;
	bool is_file_backed_              = false;
	bool is_virtual_                  = false;
	std::uint64_t id_                 = NextBufferId();

	static std::uint64_t NextBufferId()
	{
		static std::atomic<std::uint64_t> next{1};
		return next.fetch_add(1);
	}
	bool dirty_                       = false;
	bool read_only_                   = false;
	bool mark_set_                    = false;
	std::size_t mark_curx_            = 0, mark_cury_ = 0;
	bool visual_line_active_          = false;
	std::size_t visual_line_anchor_y_ = 0;
	std::size_t visual_line_active_y_ = 0;

	// Per-buffer undo state
	std::unique_ptr<struct UndoTree> undo_tree_;
	std::unique_ptr<UndoSystem> undo_sys_;

	// Edit mode (code vs writing)
	EditMode edit_mode_ = EditMode::Code;
	bool edit_mode_detected_ = false; // true after initial auto-detection

	// Syntax/highlighting state
	std::uint64_t version_ = 0; // increment on edits

	// Record a content change starting at `row`: bump the version and let the
	// highlighter drop cached rows from `row` down (rows above are unaffected).
	void edited_at_(int row)
	{
		++version_;
		if (highlighter_)
			highlighter_->OnEdit(row < 0 ? 0 : row, version_);
	}
	bool syntax_enabled_   = true;
	bool syntax_user_override_ = false; // true once user explicitly set syntax state via a command
	std::string filetype_;
	std::unique_ptr<kte::HighlighterEngine> highlighter_;
	// Non-owning pointer to swap recorder managed by Editor/SwapManager
	kte::SwapRecorder *swap_rec_ = nullptr;
};