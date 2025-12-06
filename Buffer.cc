#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <limits>
#include <cerrno>
#include <cstring>
#include <string_view>

#include "Buffer.h"
#include "UndoSystem.h"
#include "UndoTree.h"
// For reconstructing highlighter state on copies
#include "syntax/HighlighterRegistry.h"
#include "syntax/NullHighlighter.h"


Buffer::Buffer()
{
	// Initialize undo system per buffer
	undo_tree_ = std::make_unique<UndoTree>();
	undo_sys_  = std::make_unique<UndoSystem>(*this, *undo_tree_);
}


Buffer::Buffer(const std::string &path)
{
	std::string err;
	OpenFromFile(path, err);
}


// Copy constructor/assignment: perform a deep copy of core fields; reinitialize undo for the new buffer.
Buffer::Buffer(const Buffer &other)
{
	curx_             = other.curx_;
	cury_             = other.cury_;
	rx_               = other.rx_;
	nrows_            = other.nrows_;
	rowoffs_          = other.rowoffs_;
	coloffs_          = other.coloffs_;
	rows_             = other.rows_;
	content_          = other.content_;
	rows_cache_dirty_ = other.rows_cache_dirty_;
	filename_         = other.filename_;
	is_file_backed_   = other.is_file_backed_;
	dirty_            = other.dirty_;
	read_only_        = other.read_only_;
	mark_set_         = other.mark_set_;
	mark_curx_        = other.mark_curx_;
	mark_cury_        = other.mark_cury_;
	// Copy syntax/highlighting flags
	version_        = other.version_;
	syntax_enabled_ = other.syntax_enabled_;
	filetype_       = other.filetype_;
	// Fresh undo system for the copy
	undo_tree_ = std::make_unique<UndoTree>();
	undo_sys_  = std::make_unique<UndoSystem>(*this, *undo_tree_);

	// Recreate a highlighter engine for this copy based on filetype/syntax state
	if (syntax_enabled_) {
		// Allocate engine and install an appropriate highlighter
		highlighter_ = std::make_unique<kte::HighlighterEngine>();
		if (!filetype_.empty()) {
			auto hl = kte::HighlighterRegistry::CreateFor(filetype_);
			if (hl) {
				highlighter_->SetHighlighter(std::move(hl));
			} else {
				// Unsupported filetype -> NullHighlighter keeps syntax pipeline active
				highlighter_->SetHighlighter(std::make_unique<kte::NullHighlighter>());
			}
		} else {
			// No filetype -> keep syntax enabled but use NullHighlighter
			highlighter_->SetHighlighter(std::make_unique<kte::NullHighlighter>());
		}
		// Fresh engine has empty caches; nothing to invalidate
	}
}


Buffer &
Buffer::operator=(const Buffer &other)
{
	if (this == &other)
		return *this;
	curx_             = other.curx_;
	cury_             = other.cury_;
	rx_               = other.rx_;
	nrows_            = other.nrows_;
	rowoffs_          = other.rowoffs_;
	coloffs_          = other.coloffs_;
	rows_             = other.rows_;
	content_          = other.content_;
	rows_cache_dirty_ = other.rows_cache_dirty_;
	filename_         = other.filename_;
	is_file_backed_   = other.is_file_backed_;
	dirty_            = other.dirty_;
	read_only_        = other.read_only_;
	mark_set_         = other.mark_set_;
	mark_curx_        = other.mark_curx_;
	mark_cury_        = other.mark_cury_;
	version_          = other.version_;
	syntax_enabled_   = other.syntax_enabled_;
	filetype_         = other.filetype_;
	// Recreate undo system for this instance
	undo_tree_ = std::make_unique<UndoTree>();
	undo_sys_  = std::make_unique<UndoSystem>(*this, *undo_tree_);

	// Recreate highlighter engine consistent with syntax settings
	highlighter_.reset();
	if (syntax_enabled_) {
		highlighter_ = std::make_unique<kte::HighlighterEngine>();
		if (!filetype_.empty()) {
			auto hl = kte::HighlighterRegistry::CreateFor(filetype_);
			if (hl) {
				highlighter_->SetHighlighter(std::move(hl));
			} else {
				highlighter_->SetHighlighter(std::make_unique<kte::NullHighlighter>());
			}
		} else {
			highlighter_->SetHighlighter(std::make_unique<kte::NullHighlighter>());
		}
	}
	return *this;
}


// Move constructor: move all fields and update UndoSystem's buffer reference
Buffer::Buffer(Buffer &&other) noexcept
	: curx_(other.curx_),
	  cury_(other.cury_),
	  rx_(other.rx_),
	  nrows_(other.nrows_),
	  rowoffs_(other.rowoffs_),
	  coloffs_(other.coloffs_),
	  rows_(std::move(other.rows_)),
	  filename_(std::move(other.filename_)),
	  is_file_backed_(other.is_file_backed_),
	  dirty_(other.dirty_),
	  read_only_(other.read_only_),
	  mark_set_(other.mark_set_),
	  mark_curx_(other.mark_curx_),
	  mark_cury_(other.mark_cury_),
	  undo_tree_(std::move(other.undo_tree_)),
	  undo_sys_(std::move(other.undo_sys_))
{
	// Move syntax/highlighting state
	version_          = other.version_;
	syntax_enabled_   = other.syntax_enabled_;
	filetype_         = std::move(other.filetype_);
	highlighter_      = std::move(other.highlighter_);
	content_          = std::move(other.content_);
	rows_cache_dirty_ = other.rows_cache_dirty_;
	// Update UndoSystem's buffer reference to point to this object
	if (undo_sys_) {
		undo_sys_->UpdateBufferReference(*this);
	}
}


// Move assignment: move all fields and update UndoSystem's buffer reference
Buffer &
Buffer::operator=(Buffer &&other) noexcept
{
	if (this == &other)
		return *this;

	curx_           = other.curx_;
	cury_           = other.cury_;
	rx_             = other.rx_;
	nrows_          = other.nrows_;
	rowoffs_        = other.rowoffs_;
	coloffs_        = other.coloffs_;
	rows_           = std::move(other.rows_);
	filename_       = std::move(other.filename_);
	is_file_backed_ = other.is_file_backed_;
	dirty_          = other.dirty_;
	read_only_      = other.read_only_;
	mark_set_       = other.mark_set_;
	mark_curx_      = other.mark_curx_;
	mark_cury_      = other.mark_cury_;
	undo_tree_      = std::move(other.undo_tree_);
	undo_sys_       = std::move(other.undo_sys_);

	// Move syntax/highlighting state
	version_          = other.version_;
	syntax_enabled_   = other.syntax_enabled_;
	filetype_         = std::move(other.filetype_);
	highlighter_      = std::move(other.highlighter_);
	content_          = std::move(other.content_);
	rows_cache_dirty_ = other.rows_cache_dirty_;
	// Update UndoSystem's buffer reference to point to this object
	if (undo_sys_) {
		undo_sys_->UpdateBufferReference(*this);
	}

	return *this;
}


bool
Buffer::OpenFromFile(const std::string &path, std::string &err)
{
	auto normalize_path = [](const std::string &in) -> std::string {
		std::string expanded = in;
		// Expand leading '~' to HOME
		if (!expanded.empty() && expanded[0] == '~') {
			const char *home = std::getenv("HOME");
			if (home && expanded.size() >= 2 && (expanded[1] == '/' || expanded[1] == '\\')) {
				expanded = std::string(home) + expanded.substr(1);
			} else if (home && expanded.size() == 1) {
				expanded = std::string(home);
			}
		}
		try {
			std::filesystem::path p(expanded);
			if (std::filesystem::exists(p)) {
				return std::filesystem::canonical(p).string();
			}
			return std::filesystem::absolute(p).string();
		} catch (...) {
			// On any error, fall back to input
			return expanded;
		}
	};

	const std::string norm = normalize_path(path);
	// If the file doesn't exist, initialize an empty, non-file-backed buffer
	// with the provided filename. Do not touch the filesystem until Save/SaveAs.
	if (!std::filesystem::exists(norm)) {
		rows_.clear();
		nrows_          = 0;
		filename_       = norm;
		is_file_backed_ = false;
		dirty_          = false;

		// Reset cursor/viewport state
		curx_      = cury_    = rx_ = 0;
		rowoffs_   = coloffs_ = 0;
		mark_set_  = false;
		mark_curx_ = mark_cury_ = 0;

		// Empty PieceTable
		content_.Clear();
		rows_cache_dirty_ = true;

		return true;
	}

	std::ifstream in(norm, std::ios::in | std::ios::binary);
	if (!in) {
		err = "Failed to open file: " + norm;
		return false;
	}

	// Read entire file into PieceTable as-is
	std::string data;
	in.seekg(0, std::ios::end);
	auto sz = in.tellg();
	if (sz > 0) {
		data.resize(static_cast<std::size_t>(sz));
		in.seekg(0, std::ios::beg);
		in.read(data.data(), static_cast<std::streamsize>(data.size()));
	}
	content_.Clear();
	if (!data.empty())
		content_.Append(data.data(), data.size());
	rows_cache_dirty_ = true;
	nrows_            = 0; // not used under PieceTable
	filename_         = norm;
	is_file_backed_   = true;
	dirty_            = false;

	// Reset/initialize undo system for this loaded file
	if (!undo_tree_)
		undo_tree_ = std::make_unique<UndoTree>();
	if (!undo_sys_)
		undo_sys_ = std::make_unique<UndoSystem>(*this, *undo_tree_);
	// Clear any existing history for a fresh load
	undo_sys_->clear();

	// Reset cursor/viewport state
	curx_      = cury_    = rx_ = 0;
	rowoffs_   = coloffs_ = 0;
	mark_set_  = false;
	mark_curx_ = mark_cury_ = 0;

	return true;
}


bool
Buffer::Save(std::string &err) const
{
	if (!is_file_backed_ || filename_.empty()) {
		err = "Buffer is not file-backed; use SaveAs()";
		return false;
	}
	std::ofstream out(filename_, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out) {
		err = "Failed to open for write: " + filename_ + ". Error: " + std::string(std::strerror(errno));
		return false;
	}
	// Write the entire buffer in a single block to minimize I/O calls.
	const char *data = content_.Data();
	const auto size  = static_cast<std::streamsize>(content_.Size());
	if (data != nullptr && size > 0) {
		out.write(data, size);
	}
	if (!out.good()) {
		err = "Write error: " + filename_ + ". Error: " + std::string(std::strerror(errno));
		return false;
	}
	// Note: const method cannot change dirty_. Intentionally const to allow UI code
	// to decide when to flip dirty flag after successful save.
	return true;
}


bool
Buffer::SaveAs(const std::string &path, std::string &err)
{
	// Normalize output path first
	std::string out_path;
	try {
		std::filesystem::path p(path);
		// Do a light expansion of '~'
		std::string expanded = path;
		if (!expanded.empty() && expanded[0] == '~') {
			const char *home = std::getenv("HOME");
			if (home && expanded.size() >= 2 && (expanded[1] == '/' || expanded[1] == '\\'))
				expanded = std::string(home) + expanded.substr(1);
			else if (home && expanded.size() == 1)
				expanded = std::string(home);
		}
		std::filesystem::path ep(expanded);
		out_path = std::filesystem::absolute(ep).string();
	} catch (...) {
		out_path = path;
	}

	// Write to the given path
	std::ofstream out(out_path, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out) {
		err = "Failed to open for write: " + out_path + ". Error: " + std::string(std::strerror(errno));
		return false;
	}
	// Write whole content in a single I/O operation
	const char *data = content_.Data();
	const auto size  = static_cast<std::streamsize>(content_.Size());
	if (data != nullptr && size > 0) {
		out.write(data, size);
	}
	if (!out.good()) {
		err = "Write error: " + out_path + ". Error: " + std::string(std::strerror(errno));
		return false;
	}

	filename_       = out_path;
	is_file_backed_ = true;
	dirty_          = false;
	return true;
}


std::string
Buffer::AsString() const
{
	std::stringstream ss;
	ss << "Buffer<" << this->filename_;
	if (this->Dirty()) {
		ss << "*";
	}
	ss << ">: " << content_.LineCount() << " lines";
	return ss.str();
}


// --- Raw editing APIs (no undo recording, cursor untouched) ---
void
Buffer::insert_text(int row, int col, std::string_view text)
{
	if (row < 0)
		row = 0;
	if (col < 0)
		col = 0;
	const std::size_t off = content_.LineColToByteOffset(static_cast<std::size_t>(row),
	                                                     static_cast<std::size_t>(col));
	if (!text.empty()) {
		content_.Insert(off, text.data(), text.size());
		rows_cache_dirty_ = true;
	}
}


// ===== Adapter helpers for PieceTable-backed Buffer =====
std::string_view
Buffer::GetLineView(std::size_t row) const
{
	// Get byte range for the logical line and return a view into materialized data
	auto range       = content_.GetLineRange(row); // [start,end) in bytes
	const char *base = content_.Data(); // materializes if needed
	if (!base)
		return std::string_view();
	const std::size_t start = range.first;
	const std::size_t len   = (range.second > range.first) ? (range.second - range.first) : 0;
	return std::string_view(base + start, len);
}


void
Buffer::ensure_rows_cache() const
{
	if (!rows_cache_dirty_)
		return;
	rows_.clear();
	const std::size_t lc = content_.LineCount();
	rows_.reserve(lc);
	for (std::size_t i = 0; i < lc; ++i) {
		rows_.emplace_back(content_.GetLine(i));
	}
	// Keep nrows_ in sync for any legacy code that still reads it
	const_cast<Buffer *>(this)->nrows_ = rows_.size();
	rows_cache_dirty_                  = false;
}


std::size_t
Buffer::content_LineCount_() const
{
	return content_.LineCount();
}


void
Buffer::delete_text(int row, int col, std::size_t len)
{
	if (len == 0)
		return;
	if (row < 0)
		row = 0;
	if (col < 0)
		col = 0;
	const std::size_t start = content_.LineColToByteOffset(static_cast<std::size_t>(row),
	                                                       static_cast<std::size_t>(col));
	std::size_t r         = static_cast<std::size_t>(row);
	std::size_t c         = static_cast<std::size_t>(col);
	std::size_t remaining = len;
	const std::size_t lc  = content_.LineCount();

	while (remaining > 0 && r < lc) {
		const std::string line = content_.GetLine(r); // logical line (without trailing '\n')
		const std::size_t L    = line.size();
		if (c < L) {
			const std::size_t take = std::min(remaining, L - c);
			c += take;
			remaining -= take;
		}
		if (remaining == 0)
			break;
		// Consume newline between lines as one char, if there is a next line
		if (r + 1 < lc) {
			if (remaining > 0) {
				remaining -= 1; // the newline
				r += 1;
				c = 0;
			}
		} else {
			// At last line and still remaining: delete to EOF
			std::size_t total = content_.Size();
			content_.Delete(start, total - start);
			rows_cache_dirty_ = true;
			return;
		}
	}

	// Compute end offset at (r,c)
	std::size_t end = content_.LineColToByteOffset(r, c);
	if (end > start) {
		content_.Delete(start, end - start);
		rows_cache_dirty_ = true;
	}
}


void
Buffer::split_line(int row, const int col)
{
	if (row < 0)
		row = 0;
	if (col < 0)
		row = 0;
	const std::size_t off = content_.LineColToByteOffset(static_cast<std::size_t>(row),
	                                                     static_cast<std::size_t>(col));
	const char nl = '\n';
	content_.Insert(off, &nl, 1);
	rows_cache_dirty_ = true;
}


void
Buffer::join_lines(int row)
{
	if (row < 0)
		row = 0;
	std::size_t r = static_cast<std::size_t>(row);
	if (r + 1 >= content_.LineCount())
		return;
	// Delete the newline between line r and r+1
	std::size_t end_of_line = content_.LineColToByteOffset(r, std::numeric_limits<std::size_t>::max());
	// end_of_line now equals line end (clamped before newline). The newline should be exactly at this position.
	content_.Delete(end_of_line, 1);
	rows_cache_dirty_ = true;
}


void
Buffer::insert_row(int row, const std::string_view text)
{
	if (row < 0)
		row = 0;
	std::size_t off = content_.LineColToByteOffset(static_cast<std::size_t>(row), 0);
	if (!text.empty())
		content_.Insert(off, text.data(), text.size());
	const char nl = '\n';
	content_.Insert(off + text.size(), &nl, 1);
	rows_cache_dirty_ = true;
}


void
Buffer::delete_row(int row)
{
	if (row < 0)
		row = 0;
	std::size_t r = static_cast<std::size_t>(row);
	if (r >= content_.LineCount())
		return;
	auto range = content_.GetLineRange(r); // [start,end)
	// If not last line, ensure we include the separating newline by using end as-is (which points to next line start)
	// If last line, end may equal total_size_. We still delete [start,end) which removes the last line content.
	std::size_t start = range.first;
	std::size_t end   = range.second;
	content_.Delete(start, end - start);
	rows_cache_dirty_ = true;
}


// Undo system accessors
UndoSystem *
Buffer::Undo()
{
	return undo_sys_.get();
}


const UndoSystem *
Buffer::Undo() const
{
	return undo_sys_.get();
}