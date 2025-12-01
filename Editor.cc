#include <algorithm>
#include <utility>
#include <filesystem>

#include "Editor.h"


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


static std::vector<std::filesystem::path>
split_reverse(const std::filesystem::path &p)
{
	std::vector<std::filesystem::path> parts;
	for (auto it = p; !it.empty(); it = it.parent_path()) {
		if (it == it.parent_path()) {
			// root or single element
			if (!it.empty())
				parts.push_back(it);
			break;
		}
		parts.push_back(it.filename());
	}
	return parts; // from leaf toward root
}


std::string
Editor::DisplayNameFor(const Buffer &buf) const
{
	std::string full = buf.Filename();
	if (full.empty())
		return std::string("[no name]");

	std::filesystem::path target(full);
	auto target_parts = split_reverse(target);
	if (target_parts.empty())
		return target.filename().string();

	// Prepare list of other buffer paths
	std::vector<std::vector<std::filesystem::path> > others;
	others.reserve(buffers_.size());
	for (const auto &b: buffers_) {
		if (&b == &buf)
			continue;
		if (b.Filename().empty())
			continue;
		others.push_back(split_reverse(std::filesystem::path(b.Filename())));
	}

	// Increase suffix length until unique among others
	std::size_t need = 1; // at least basename
	for (;;) {
		// Build candidate suffix for target
		std::filesystem::path cand;
		for (std::size_t i = 0; i < need && i < target_parts.size(); ++i) {
			cand = std::filesystem::path(target_parts[i]) / cand;
		}
		// Compare against others
		bool clash = false;
		for (const auto &o_parts: others) {
			std::filesystem::path ocand;
			for (std::size_t i = 0; i < need && i < o_parts.size(); ++i) {
				ocand = std::filesystem::path(o_parts[i]) / ocand;
			}
			if (ocand == cand) {
				clash = true;
				break;
			}
		}
		if (!clash || need >= target_parts.size()) {
			std::string s = cand.string();
			// Remove any trailing slash that may appear from root joining
			if (!s.empty() && (s.back() == '/' || s.back() == '\\'))
				s.pop_back();
			return s;
		}
		++need;
	}
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
	// If there is exactly one unnamed, empty, clean buffer, reuse it instead
	// of creating a new one.
	if (buffers_.size() == 1) {
		Buffer &cur                  = buffers_[curbuf_];
		const bool unnamed           = cur.Filename().empty() && !cur.IsFileBacked();
		const bool clean             = !cur.Dirty();
		const auto &rows             = cur.Rows();
		const bool rows_empty        = rows.empty();
		const bool single_empty_line = (!rows.empty() && rows.size() == 1 && rows[0].size() == 0);
		if (unnamed && clean && (rows_empty || single_empty_line)) {
			return cur.OpenFromFile(path, err);
		}
	}

	Buffer b;
	if (!b.OpenFromFile(path, err)) {
		return false;
	}
	// Add as a new buffer and switch to it
	std::size_t idx = AddBuffer(std::move(b));
	SwitchTo(idx);
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
	msgtm_                = 0;
	uarg_                 = 0;
	ucount_               = 0;
	quit_requested_       = false;
	quit_confirm_pending_ = false;
	buffers_.clear();
	curbuf_ = 0;
}
