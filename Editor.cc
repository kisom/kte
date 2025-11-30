#include "Editor.h"

#include <algorithm>
#include <utility>


Editor::Editor() = default;


void
Editor::SetDimensions(std::size_t rows, std::size_t cols)
{
	rows_ = rows;
	cols_ = cols;
}


void
Editor::SetStatus(const std::string &message)
{
	msg_   = message;
	msgtm_ = std::time(nullptr);
}


Buffer *
Editor::CurrentBuffer()
{
	if (buffers_.empty() || curbuf_ >= buffers_.size()) {
		return nullptr;
	}
	return &buffers_[curbuf_];
}


const Buffer *
Editor::CurrentBuffer() const
{
	if (buffers_.empty() || curbuf_ >= buffers_.size()) {
		return nullptr;
	}
	return &buffers_[curbuf_];
}


std::size_t
Editor::AddBuffer(const Buffer &buf)
{
	buffers_.push_back(buf);
	if (buffers_.size() == 1) {
		curbuf_ = 0;
	}
	return buffers_.size() - 1;
}


std::size_t
Editor::AddBuffer(Buffer &&buf)
{
	buffers_.push_back(std::move(buf));
	if (buffers_.size() == 1) {
		curbuf_ = 0;
	}
	return buffers_.size() - 1;
}


bool
Editor::OpenFile(const std::string &path, std::string &err)
{
	Buffer b;
	if (!b.OpenFromFile(path, err)) {
		return false;
	}
	AddBuffer(std::move(b));
	return true;
}


bool
Editor::SwitchTo(std::size_t index)
{
	if (index >= buffers_.size()) {
		return false;
	}
	curbuf_ = index;
	return true;
}


bool
Editor::CloseBuffer(std::size_t index)
{
	if (index >= buffers_.size()) {
		return false;
	}
	buffers_.erase(buffers_.begin() + static_cast<std::ptrdiff_t>(index));
	if (buffers_.empty()) {
		curbuf_ = 0;
	} else if (curbuf_ >= buffers_.size()) {
		curbuf_ = buffers_.size() - 1;
	}
	return true;
}


void
Editor::Reset()
{
	rows_    = cols_ = 0;
	mode_    = 0;
	kill_    = 0;
	no_kill_ = 0;
	dirtyex_ = 0;
	msg_.clear();
	msgtm_  = 0;
	uarg_   = 0;
	ucount_ = 0;
	buffers_.clear();
	curbuf_ = 0;
}
