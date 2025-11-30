#include "Buffer.h"

#include <fstream>
#include <sstream>
#include <filesystem>


Buffer::Buffer() = default;


Buffer::Buffer(const std::string &path)
{
	std::string err;
	OpenFromFile(path, err);
}


bool
Buffer::OpenFromFile(const std::string &path, std::string &err)
{
	// If the file doesn't exist, initialize an empty, non-file-backed buffer
	// with the provided filename. Do not touch the filesystem until Save/SaveAs.
 if (!std::filesystem::exists(path)) {
        rows_.clear();
        nrows_          = 0;
        filename_       = path;
        is_file_backed_ = false;
        dirty_          = false;

        // Reset cursor/viewport state
        curx_      = cury_    = rx_ = 0;
        rowoffs_   = coloffs_ = 0;
        mark_set_  = false;
        mark_curx_ = mark_cury_ = 0;

        return true;
    }

	std::ifstream in(path, std::ios::in | std::ios::binary);
	if (!in) {
		err = "Failed to open file: " + path;
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
	filename_       = path;
	is_file_backed_ = true;
	dirty_          = false;

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
        if (d && n) out.write(d, static_cast<std::streamsize>(n));
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
	// Write to the given path
	std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out) {
		err = "Failed to open for write: " + path;
		return false;
	}
 for (std::size_t i = 0; i < rows_.size(); ++i) {
        const char *d = rows_[i].Data();
        std::size_t n = rows_[i].Size();
        if (d && n) out.write(d, static_cast<std::streamsize>(n));
        if (i + 1 < rows_.size()) {
            out.put('\n');
        }
    }
	if (!out.good()) {
		err = "Write error";
		return false;
	}

	filename_       = path;
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
