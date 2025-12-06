/*
 * Buffer.h - editor buffer representing an open document
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <string_view>

#include "PieceTable.h"
#include "UndoSystem.h"
#include <cstdint>
#include <memory>
#include "syntax/HighlighterEngine.h"
#include "Highlight.h"

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

	// Accessors
	[[nodiscard]] std::size_t Curx() const
	{
		return curx_;
	}


	[[nodiscard]] std::size_t Cury() const
	{
		return cury_;
	}


	[[nodiscard]] std::size_t Rx() const
	{
		return rx_;
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


	// Line wrapper backed by PieceTable
	class Line {
	public:
		Line() = default;


		explicit Line(const char *s)
		{
			assign_from(s ? std::string(s) : std::string());
		}


		explicit Line(const std::string &s)
		{
			assign_from(s);
		}


		Line(const Line &other) = default;

		Line &operator=(const Line &other) = default;

		Line(Line &&other) noexcept = default;

		Line &operator=(Line &&other) noexcept = default;

		// capacity helpers
		void Clear()
		{
			buf_.Clear();
		}


		// size/access
		[[nodiscard]] std::size_t size() const
		{
			return buf_.Size();
		}


		[[nodiscard]] bool empty() const
		{
			return size() == 0;
		}


		// read-only raw view
		[[nodiscard]] const char *Data() const
		{
			return buf_.Data();
		}


		[[nodiscard]] std::size_t Size() const
		{
			return buf_.Size();
		}


		// element access (read-only)
		[[nodiscard]] char operator[](std::size_t i) const
		{
			const char *d = buf_.Data();
			return (i < buf_.Size() && d) ? d[i] : '\0';
		}


		// conversions
		explicit operator std::string() const
		{
			return {buf_.Data() ? buf_.Data() : "", buf_.Size()};
		}


		// string-like API used by command/renderer layers (implemented via materialization for now)
		[[nodiscard]] std::string substr(std::size_t pos) const
		{
			const std::size_t n = buf_.Size();
			if (pos >= n)
				return {};
			return {buf_.Data() + pos, n - pos};
		}


		[[nodiscard]] std::string substr(std::size_t pos, std::size_t len) const
		{
			const std::size_t n = buf_.Size();
			if (pos >= n)
				return {};
			const std::size_t take = (pos + len > n) ? (n - pos) : len;
			return {buf_.Data() + pos, take};
		}


		// minimal find() to support search within a line
		[[nodiscard]] std::size_t find(const std::string &needle, const std::size_t pos = 0) const
		{
			// Materialize to std::string for now; Line is backed by PieceTable
			const auto s = static_cast<std::string>(*this);
			return s.find(needle, pos);
		}


		void erase(std::size_t pos)
		{
			// erase to end
			material_edit([&](std::string &s) {
				if (pos < s.size())
					s.erase(pos);
			});
		}


		void erase(std::size_t pos, std::size_t len)
		{
			material_edit([&](std::string &s) {
				if (pos < s.size())
					s.erase(pos, len);
			});
		}


		void insert(std::size_t pos, const std::string &seg)
		{
			material_edit([&](std::string &s) {
				if (pos > s.size())
					pos = s.size();
				s.insert(pos, seg);
			});
		}


		Line &operator+=(const Line &other)
		{
			buf_.Append(other.buf_.Data(), other.buf_.Size());
			return *this;
		}


		Line &operator+=(const std::string &s)
		{
			buf_.Append(s.data(), s.size());
			return *this;
		}


		Line &operator=(const std::string &s)
		{
			assign_from(s);
			return *this;
		}

	private:
		void assign_from(const std::string &s)
		{
			buf_.Clear();
			if (!s.empty())
				buf_.Append(s.data(), s.size());
		}


		template<typename F>
		void material_edit(F fn)
		{
			std::string tmp = static_cast<std::string>(*this);
			fn(tmp);
			assign_from(tmp);
		}


		PieceTable buf_;
	};


	[[nodiscard]] const std::vector<Line> &Rows() const
	{
		ensure_rows_cache();
		return rows_;
	}


	[[nodiscard]] std::vector<Line> &Rows()
	{
		ensure_rows_cache();
		return rows_;
	}


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
	}


	[[nodiscard]] bool IsFileBacked() const
	{
		return is_file_backed_;
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


	void SetRenderX(const std::size_t rx)
	{
		rx_ = rx;
	}


	void SetOffsets(const std::size_t row, const std::size_t col)
	{
		rowoffs_ = row;
		coloffs_ = col;
	}


	void SetDirty(bool d)
	{
		dirty_ = d;
		if (d) {
			++version_;
			if (highlighter_) {
				highlighter_->InvalidateFrom(0);
			}
		}
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


	[[nodiscard]] std::string AsString() const;

	// Syntax highlighting integration (per-buffer)
	[[nodiscard]] std::uint64_t Version() const
	{
		return version_;
	}


	void SetSyntaxEnabled(bool on)
	{
		syntax_enabled_ = on;
	}


	[[nodiscard]] bool SyntaxEnabled() const
	{
		return syntax_enabled_;
	}


	void SetFiletype(const std::string &ft)
	{
		filetype_ = ft;
	}


	[[nodiscard]] const std::string &Filetype() const
	{
		return filetype_;
	}


	kte::HighlighterEngine *Highlighter()
	{
		return highlighter_.get();
	}


	const kte::HighlighterEngine *Highlighter() const
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


	// Raw, low-level editing APIs used by UndoSystem apply().
	// These must NOT trigger undo recording. They also do not move the cursor.
	void insert_text(int row, int col, std::string_view text);

	void delete_text(int row, int col, std::size_t len);

	void split_line(int row, int col);

	void join_lines(int row);

	void insert_row(int row, std::string_view text);

	void delete_row(int row);

	// Undo system accessors (created per-buffer)
	UndoSystem *Undo();

	[[nodiscard]] const UndoSystem *Undo() const;

private:
	// State mirroring original C struct (without undo_tree)
	std::size_t curx_    = 0, cury_ = 0; // cursor position in characters
	std::size_t rx_      = 0; // render x (tabs expanded)
	std::size_t nrows_   = 0; // number of rows
	std::size_t rowoffs_ = 0, coloffs_ = 0; // viewport offsets
	mutable std::vector<Line> rows_; // materialized cache of rows (without trailing newlines)
	// PieceTable is the source of truth.
	PieceTable content_{};
	mutable bool rows_cache_dirty_ = true; // invalidate on edits / I/O

	// Helper to rebuild rows_ from content_
	void ensure_rows_cache() const;

	// Helper to query content_.LineCount() while keeping header minimal
	std::size_t content_LineCount_() const;

	std::string filename_;
	bool is_file_backed_   = false;
	bool dirty_            = false;
	bool read_only_        = false;
	bool mark_set_         = false;
	std::size_t mark_curx_ = 0, mark_cury_ = 0;

	// Per-buffer undo state
	std::unique_ptr<struct UndoTree> undo_tree_;
	std::unique_ptr<UndoSystem> undo_sys_;

	// Syntax/highlighting state
	std::uint64_t version_ = 0; // increment on edits
	bool syntax_enabled_   = true;
	std::string filetype_;
	std::unique_ptr<kte::HighlighterEngine> highlighter_;
	// Non-owning pointer to swap recorder managed by Editor/SwapManager
	kte::SwapRecorder *swap_rec_ = nullptr;
};