/*
 * Editor.h - top-level editor state and buffer management
 */
#ifndef KTE_EDITOR_H
#define KTE_EDITOR_H

#include <cstddef>
#include <ctime>
#include <string>
#include <vector>

#include "Buffer.h"

class Editor {
public:
	Editor();

	// Dimensions (terminal or viewport)
	void SetDimensions(std::size_t rows, std::size_t cols);


	[[nodiscard]] std::size_t Rows() const
	{
		return rows_;
	}


	[[nodiscard]] std::size_t Cols() const
	{
		return cols_;
	}


	// Mode and flags (mirroring legacy fields)
	void SetMode(int m)
	{
		mode_ = m;
	}


	[[nodiscard]] int Mode() const
	{
		return mode_;
	}


	void SetKillChain(bool on)
	{
		kill_ = on ? 1 : 0;
	}


	[[nodiscard]] bool KillChain() const
	{
		return kill_ != 0;
	}


	void SetNoKill(bool on)
	{
		no_kill_ = on ? 1 : 0;
	}


	[[nodiscard]] bool NoKill() const
	{
		return no_kill_ != 0;
	}


	void SetDirtyEx(int d)
	{
		dirtyex_ = d;
	}


	[[nodiscard]] int DirtyEx() const
	{
		return dirtyex_;
	}


	void SetUniversalArg(int uarg, int ucount)
	{
		uarg_   = uarg;
		ucount_ = ucount;
	}


	[[nodiscard]] int UArg() const
	{
		return uarg_;
	}


	[[nodiscard]] int UCount() const
	{
		return ucount_;
	}


	// Status message storage. Rendering is renderer-dependent; the editor
	// merely stores the current message and its timestamp.
	void SetStatus(const std::string &message);


	[[nodiscard]] const std::string &Status() const
	{
		return msg_;
	}


	[[nodiscard]] std::time_t StatusTime() const
	{
		return msgtm_;
	}


	// Buffers
	[[nodiscard]] std::size_t BufferCount() const
	{
		return buffers_.size();
	}


	[[nodiscard]] std::size_t CurrentBufferIndex() const
	{
		return curbuf_;
	}


	Buffer *CurrentBuffer();

	const Buffer *CurrentBuffer() const;

	// Add an existing buffer (copy/move) or open from file path
	std::size_t AddBuffer(const Buffer &buf);

	std::size_t AddBuffer(Buffer &&buf);

	bool OpenFile(const std::string &path, std::string &err);

	// Buffer switching/closing
	bool SwitchTo(std::size_t index);

	bool CloseBuffer(std::size_t index);

	// Reset to initial state
	void Reset();

	// Direct access when needed (try to prefer methods above)
	[[nodiscard]] const std::vector<Buffer> &Buffers() const
	{
		return buffers_;
	}


	std::vector<Buffer> &Buffers()
	{
		return buffers_;
	}

private:
	std::size_t rows_ = 0, cols_ = 0;
	int mode_         = 0;
	int kill_         = 0; // KILL CHAIN
	int no_kill_      = 0; // don't kill in delete_row
	int dirtyex_      = 0;
	std::string msg_;
	std::time_t msgtm_ = 0;
	int uarg_          = 0, ucount_ = 0; // C-u support

	std::vector<Buffer> buffers_;
	std::size_t curbuf_ = 0; // index into buffers_
};

#endif // KTE_EDITOR_H
