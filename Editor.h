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


	// Kill ring API
	void KillRingClear()
	{
		kill_ring_.clear();
	}


	void KillRingPush(const std::string &text)
	{
		if (text.empty())
			return;
		// push to front
		kill_ring_.insert(kill_ring_.begin(), text);
		if (kill_ring_.size() > kill_ring_max_)
			kill_ring_.resize(kill_ring_max_);
	}


	void KillRingAppend(const std::string &text)
	{
		if (text.empty())
			return;
		if (kill_ring_.empty()) {
			KillRingPush(text);
		} else {
			kill_ring_.front() += text;
		}
	}


	void KillRingPrepend(const std::string &text)
	{
		if (text.empty())
			return;
		if (kill_ring_.empty()) {
			KillRingPush(text);
		} else {
			kill_ring_.front() = text + kill_ring_.front();
		}
	}


	[[nodiscard]] std::string KillRingHead() const
	{
		return kill_ring_.empty() ? std::string() : kill_ring_.front();
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


	// --- Quit/Exit state ---
	void SetQuitRequested(bool on)
	{
		quit_requested_ = on;
	}


	[[nodiscard]] bool QuitRequested() const
	{
		return quit_requested_;
	}


	void SetQuitConfirmPending(bool on)
	{
		quit_confirm_pending_ = on;
	}


	[[nodiscard]] bool QuitConfirmPending() const
	{
		return quit_confirm_pending_;
	}


	[[nodiscard]] std::time_t StatusTime() const
	{
		return msgtm_;
	}


	// --- Minimal search state for incremental search (milestone 6) ---
	void SetSearchActive(bool on)
	{
		search_active_ = on;
	}


	[[nodiscard]] bool SearchActive() const
	{
		return search_active_;
	}


	void SetSearchQuery(const std::string &q)
	{
		search_query_ = q;
	}


	[[nodiscard]] const std::string &SearchQuery() const
	{
		return search_query_;
	}


	void SetSearchMatch(std::size_t y, std::size_t x, std::size_t len)
	{
		search_y_   = y;
		search_x_   = x;
		search_len_ = len;
	}


	[[nodiscard]] std::size_t SearchMatchY() const
	{
		return search_y_;
	}


	[[nodiscard]] std::size_t SearchMatchX() const
	{
		return search_x_;
	}


	[[nodiscard]] std::size_t SearchMatchLen() const
	{
		return search_len_;
	}


	// Additional helpers for search session bookkeeping
	void SetSearchOrigin(std::size_t x, std::size_t y, std::size_t rowoffs, std::size_t coloffs)
	{
		search_origin_set_   = true;
		search_orig_x_       = x;
		search_orig_y_       = y;
		search_orig_rowoffs_ = rowoffs;
		search_orig_coloffs_ = coloffs;
	}


	void ClearSearchOrigin()
	{
		search_origin_set_ = false;
		search_orig_x_     = search_orig_y_ = search_orig_rowoffs_ = search_orig_coloffs_ = 0;
	}


	[[nodiscard]] bool SearchOriginSet() const
	{
		return search_origin_set_;
	}


	[[nodiscard]] std::size_t SearchOrigX() const
	{
		return search_orig_x_;
	}


	[[nodiscard]] std::size_t SearchOrigY() const
	{
		return search_orig_y_;
	}


	[[nodiscard]] std::size_t SearchOrigRowoffs() const
	{
		return search_orig_rowoffs_;
	}


	[[nodiscard]] std::size_t SearchOrigColoffs() const
	{
		return search_orig_coloffs_;
	}


	void SetSearchIndex(int i)
	{
		search_index_ = i;
	}


	[[nodiscard]] int SearchIndex() const
	{
		return search_index_;
	}


 // --- Generic Prompt subsystem (for search, open-file, save-as, etc.) ---
 enum class PromptKind {
     None = 0,
     Search,
     RegexSearch,
     OpenFile,
     SaveAs,
     Confirm,
     BufferSwitch,
     GotoLine,
     Chdir,
     ReplaceFind,  // step 1 of Search & Replace: find what
     ReplaceWith   // step 2 of Search & Replace: replace with
 };


	void StartPrompt(PromptKind kind, const std::string &label, const std::string &initial)
	{
		prompt_active_ = true;
		prompt_kind_   = kind;
		prompt_label_  = label;
		prompt_text_   = initial;
	}


	void CancelPrompt()
	{
		prompt_active_ = false;
		prompt_kind_   = PromptKind::None;
		prompt_label_.clear();
		prompt_text_.clear();
	}


	void AcceptPrompt()
	{
		// Editor-level accept only ends prompt; commands act on value.
		prompt_active_ = false;
	}


	void SetPromptText(const std::string &t)
	{
		prompt_text_ = t;
	}


	void AppendPromptText(const std::string &t)
	{
		prompt_text_ += t;
	}


	void BackspacePromptText()
	{
		if (!prompt_text_.empty())
			prompt_text_.pop_back();
	}


	[[nodiscard]] bool PromptActive() const
	{
		return prompt_active_;
	}


	[[nodiscard]] PromptKind CurrentPromptKind() const
	{
		return prompt_kind_;
	}


	// --- Overwrite confirmation (save-as on existing file) ---
	void SetPendingOverwritePath(const std::string &path)
	{
		pending_overwrite_path_ = path;
	}


	void ClearPendingOverwritePath()
	{
		pending_overwrite_path_.clear();
	}


	[[nodiscard]] const std::string &PendingOverwritePath() const
	{
		return pending_overwrite_path_;
	}


	[[nodiscard]] const std::string &PromptLabel() const
	{
		return prompt_label_;
	}


	[[nodiscard]] const std::string &PromptText() const
	{
		return prompt_text_;
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

	// Compute a display-friendly short name for a buffer path that is the
	// shortest unique suffix among all open buffers. If buffer has no name,
	// returns "[no name]".
	[[nodiscard]] std::string DisplayNameFor(const Buffer &buf) const;

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


	// --- GUI: Visual File Picker state ---
	void SetFilePickerVisible(bool on)
	{
		file_picker_visible_ = on;
	}


	[[nodiscard]] bool FilePickerVisible() const
	{
		return file_picker_visible_;
	}


	void SetFilePickerDir(const std::string &path)
	{
		file_picker_dir_ = path;
	}


	[[nodiscard]] const std::string &FilePickerDir() const
	{
		return file_picker_dir_;
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

	// Kill ring (Emacs-like)
	std::vector<std::string> kill_ring_;
	std::size_t kill_ring_max_ = 60;

	// Quit state
	bool quit_requested_       = false;
	bool quit_confirm_pending_ = false;

	// Search state
	bool search_active_ = false;
	std::string search_query_;
	std::size_t search_y_ = 0, search_x_ = 0, search_len_ = 0;
	// Search session bookkeeping
	bool search_origin_set_          = false;
	std::size_t search_orig_x_       = 0, search_orig_y_       = 0;
	std::size_t search_orig_rowoffs_ = 0, search_orig_coloffs_ = 0;
	int search_index_                = -1;

	// Prompt state
	bool prompt_active_     = false;
	PromptKind prompt_kind_ = PromptKind::None;
	std::string prompt_label_;
	std::string prompt_text_;
	std::string pending_overwrite_path_;

    // GUI-only state (safe no-op in terminal builds)
    bool file_picker_visible_ = false;
    std::string file_picker_dir_;

    // Temporary state for Search & Replace flow
public:
    void SetReplaceFindTmp(const std::string &s) { replace_find_tmp_ = s; }
    void SetReplaceWithTmp(const std::string &s) { replace_with_tmp_ = s; }
    [[nodiscard]] const std::string &ReplaceFindTmp() const { return replace_find_tmp_; }
    [[nodiscard]] const std::string &ReplaceWithTmp() const { return replace_with_tmp_; }

private:
    std::string replace_find_tmp_;
    std::string replace_with_tmp_;
};

#endif // KTE_EDITOR_H
