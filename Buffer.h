/*
 * Buffer.h - editor buffer representing an open document
 */
#ifndef KTE_BUFFER_H
#define KTE_BUFFER_H

#include <cstddef>
#include <string>
#include <vector>

class Buffer {
public:
	Buffer();

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
		return nrows_;
	}


	[[nodiscard]] std::size_t Rowoffs() const
	{
		return rowoffs_;
	}


	[[nodiscard]] std::size_t Coloffs() const
	{
		return coloffs_;
	}


	[[nodiscard]] const std::vector<std::string> &Rows() const
	{
		return rows_;
	}


	[[nodiscard]] std::vector<std::string> &Rows()
	{
		return rows_;
	}


	[[nodiscard]] const std::string &Filename() const
	{
		return filename_;
	}


	[[nodiscard]] bool IsFileBacked() const
	{
		return is_file_backed_;
	}


	[[nodiscard]] bool Dirty() const
	{
		return dirty_;
	}


	void SetCursor(std::size_t x, std::size_t y)
	{
		curx_ = x;
		cury_ = y;
	}


	void SetRenderX(std::size_t rx)
	{
		rx_ = rx;
	}


	void SetOffsets(std::size_t row, std::size_t col)
	{
		rowoffs_ = row;
		coloffs_ = col;
	}


	void SetDirty(bool d)
	{
		dirty_ = d;
	}


	// Mark support
	void ClearMark()
	{
		mark_set_ = false;
	}


	void SetMark(std::size_t x, std::size_t y)
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

private:
	// State mirroring original C struct (without undo_tree)
	std::size_t curx_    = 0, cury_ = 0; // cursor position in characters
	std::size_t rx_      = 0; // render x (tabs expanded)
	std::size_t nrows_   = 0; // number of rows
	std::size_t rowoffs_ = 0, coloffs_ = 0; // viewport offsets
	std::vector<std::string> rows_; // buffer rows (without trailing newlines)
	std::string filename_;
	bool is_file_backed_   = false;
	bool dirty_            = false;
	bool mark_set_         = false;
	std::size_t mark_curx_ = 0, mark_cury_ = 0;
};

#endif // KTE_BUFFER_H
