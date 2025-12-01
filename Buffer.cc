#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

#include "Buffer.h"
#include "UndoSystem.h"
#include "UndoTree.h"


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
	curx_           = other.curx_;
	cury_           = other.cury_;
	rx_             = other.rx_;
	nrows_          = other.nrows_;
	rowoffs_        = other.rowoffs_;
	coloffs_        = other.coloffs_;
	rows_           = other.rows_;
	filename_       = other.filename_;
	is_file_backed_ = other.is_file_backed_;
	dirty_          = other.dirty_;
	mark_set_       = other.mark_set_;
	mark_curx_      = other.mark_curx_;
	mark_cury_      = other.mark_cury_;
	// Fresh undo system for the copy
	undo_tree_ = std::make_unique<UndoTree>();
	undo_sys_  = std::make_unique<UndoSystem>(*this, *undo_tree_);
}


Buffer &
Buffer::operator=(const Buffer &other)
{
	if (this == &other)
		return *this;
	curx_           = other.curx_;
	cury_           = other.cury_;
	rx_             = other.rx_;
	nrows_          = other.nrows_;
	rowoffs_        = other.rowoffs_;
	coloffs_        = other.coloffs_;
	rows_           = other.rows_;
	filename_       = other.filename_;
	is_file_backed_ = other.is_file_backed_;
	dirty_          = other.dirty_;
	mark_set_       = other.mark_set_;
	mark_curx_      = other.mark_curx_;
	mark_cury_      = other.mark_cury_;
	// Recreate undo system for this instance
	undo_tree_ = std::make_unique<UndoTree>();
	undo_sys_  = std::make_unique<UndoSystem>(*this, *undo_tree_);
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
	  mark_set_(other.mark_set_),
	  mark_curx_(other.mark_curx_),
	  mark_cury_(other.mark_cury_),
	  undo_tree_(std::move(other.undo_tree_)),
	  undo_sys_(std::move(other.undo_sys_))
{
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
	mark_set_       = other.mark_set_;
	mark_curx_      = other.mark_curx_;
	mark_cury_      = other.mark_cury_;
	undo_tree_      = std::move(other.undo_tree_);
	undo_sys_       = std::move(other.undo_sys_);

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

		return true;
	}

	std::ifstream in(norm, std::ios::in | std::ios::binary);
	if (!in) {
		err = "Failed to open file: " + norm;
		return false;
	}

	// Detect if file ends with a newline so we can preserve a final empty line
	// in our in-memory representation (mg-style semantics).
	bool ends_with_nl = false;
	{
		in.seekg(0, std::ios::end);
		std::streamoff sz = in.tellg();
		if (sz > 0) {
			in.seekg(-1, std::ios::end);
			char last = 0;
			in.read(&last, 1);
			ends_with_nl = (last == '\n');
		} else {
			in.clear();
		}
		// Rewind to start for line-by-line read
		in.clear();
		in.seekg(0, std::ios::beg);
	}

	rows_.clear();
	std::string line;
	while (std::getline(in, line)) {
		// std::getline strips the '\n', keep raw line content only
		// Handle potential Windows CRLF: strip trailing '\r'
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		rows_.emplace_back(line);
	}

	// If the file ended with a newline and we didn't already get an
	// empty final row from getline (e.g., when the last textual line
	// had content followed by '\n'), append an empty row to represent
	// the cursor position past the last newline.
	if (ends_with_nl) {
		if (rows_.empty() || !rows_.back().empty()) {
			rows_.emplace_back(std::string());
		}
	}

	nrows_          = rows_.size();
	filename_       = norm;
	is_file_backed_ = true;
	dirty_          = false;

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
		err = "Failed to open for write: " + filename_;
		return false;
	}
	for (std::size_t i = 0; i < rows_.size(); ++i) {
		const char *d = rows_[i].Data();
		std::size_t n = rows_[i].Size();
		if (d && n)
			out.write(d, static_cast<std::streamsize>(n));
		if (i + 1 < rows_.size()) {
			out.put('\n');
		}
	}
	if (!out.good()) {
		err = "Write error";
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
		err = "Failed to open for write: " + out_path;
		return false;
	}
	for (std::size_t i = 0; i < rows_.size(); ++i) {
		const char *d = rows_[i].Data();
		std::size_t n = rows_[i].Size();
		if (d && n)
			out.write(d, static_cast<std::streamsize>(n));
		if (i + 1 < rows_.size()) {
			out.put('\n');
		}
	}
	if (!out.good()) {
		err = "Write error";
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
	ss << ">: " << rows_.size() << " lines";
	return ss.str();
}


// --- Raw editing APIs (no undo recording, cursor untouched) ---
void
Buffer::insert_text(int row, int col, std::string_view text)
{
	if (row < 0)
		row = 0;
	if (static_cast<std::size_t>(row) > rows_.size())
		row = static_cast<int>(rows_.size());
	if (rows_.empty())
		rows_.emplace_back("");
	if (static_cast<std::size_t>(row) >= rows_.size())
		rows_.emplace_back("");

	auto y = static_cast<std::size_t>(row);
	auto x = static_cast<std::size_t>(col);
	if (x > rows_[y].size())
		x = rows_[y].size();

	std::string remain(text);
	while (true) {
		auto pos = remain.find('\n');
		if (pos == std::string::npos) {
			rows_[y].insert(x, remain);
			break;
		}
		// Insert up to newline
		std::string seg = remain.substr(0, pos);
		rows_[y].insert(x, seg);
		x += seg.size();
		// Split line at x
		std::string tail = rows_[y].substr(x);
		rows_[y].erase(x);
		rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(y + 1), tail);
		y += 1;
		x = 0;
		remain.erase(0, pos + 1);
	}
	// Do not set dirty here; UndoSystem will manage state/dirty externally
}


void
Buffer::delete_text(int row, int col, std::size_t len)
{
	if (rows_.empty() || len == 0)
		return;
	if (row < 0)
		row = 0;
	if (static_cast<std::size_t>(row) >= rows_.size())
		return;
	const auto y = static_cast<std::size_t>(row);
	const auto x = std::min<std::size_t>(static_cast<std::size_t>(col), rows_[y].size());

	std::size_t remaining = len;
	while (remaining > 0 && y < rows_.size()) {
		auto &line                = rows_[y];
		const std::size_t in_line = std::min<std::size_t>(remaining, line.size() - std::min(x, line.size()));
		if (x < line.size() && in_line > 0) {
			line.erase(x, in_line);
			remaining -= in_line;
		}
		if (remaining == 0)
			break;
		// If at or beyond end of line and there is a next line, join it (deleting the implied '\n')
		if (y + 1 < rows_.size()) {
			line += rows_[y + 1];
			rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(y + 1));
			// deleting the newline consumes one virtual character
			if (remaining > 0) {
				// Treat the newline as one deletion unit if len spans it
				// We already joined, so nothing else to do here.
			}
		} else {
			break;
		}
	}
}


void
Buffer::split_line(int row, const int col)
{
	if (row < 0) {
		row = 0;
	}

	if (static_cast<std::size_t>(row) >= rows_.size()) {
		rows_.resize(static_cast<std::size_t>(row) + 1);
	}
	const auto y    = static_cast<std::size_t>(row);
	const auto x    = std::min<std::size_t>(static_cast<std::size_t>(col), rows_[y].size());
	const auto tail = rows_[y].substr(x);
	rows_[y].erase(x);
	rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(y + 1), tail);
}


void
Buffer::join_lines(int row)
{
	if (row < 0) {
		row = 0;
	}

	const auto y = static_cast<std::size_t>(row);
	if (y + 1 >= rows_.size()) {
		return;
	}

	rows_[y] += rows_[y + 1];
	rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(y + 1));
}


void
Buffer::insert_row(int row, const std::string_view text)
{
	if (row < 0)
		row = 0;
	if (static_cast<std::size_t>(row) > rows_.size())
		row = static_cast<int>(rows_.size());
	rows_.insert(rows_.begin() + row, std::string(text));
}


void
Buffer::delete_row(int row)
{
	if (row < 0)
		row = 0;
	if (static_cast<std::size_t>(row) >= rows_.size())
		return;
	rows_.erase(rows_.begin() + row);
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
