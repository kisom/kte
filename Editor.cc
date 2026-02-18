#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <utility>

#include "Editor.h"
#include "syntax/HighlighterRegistry.h"
#include "syntax/CppHighlighter.h"
#include "syntax/NullHighlighter.h"


namespace {
static std::string
buffer_bytes_via_views(const Buffer &b)
{
	const std::size_t nrows = b.Nrows();
	std::string out;
	for (std::size_t i = 0; i < nrows; i++) {
		auto v = b.GetLineView(i);
		out.append(v.data(), v.size());
	}
	return out;
}


static void
apply_pending_line(Editor &ed, const std::size_t line1)
{
	if (line1 == 0)
		return;
	Buffer *b = ed.CurrentBuffer();
	if (!b)
		return;
	const std::size_t nrows = b->Nrows();
	std::size_t line        = line1 > 0 ? line1 - 1 : 0; // 1-based to 0-based
	if (nrows > 0) {
		if (line >= nrows)
			line = nrows - 1;
	} else {
		line = 0;
	}
	b->SetCursor(0, line);
}
} // namespace


Editor::Editor()
{
	swap_ = std::make_unique<kte::SwapManager>();
}


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
	// Attach swap recorder
	if (swap_) {
		swap_->Attach(&buffers_.back());
		buffers_.back().SetSwapRecorder(swap_->RecorderFor(&buffers_.back()));
	}
	if (buffers_.size() == 1) {
		curbuf_ = 0;
	}
	return buffers_.size() - 1;
}


std::size_t
Editor::AddBuffer(Buffer &&buf)
{
	buffers_.push_back(std::move(buf));
	if (swap_) {
		swap_->Attach(&buffers_.back());
		buffers_.back().SetSwapRecorder(swap_->RecorderFor(&buffers_.back()));
	}
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
		const std::size_t nrows      = cur.Nrows();
		const bool rows_empty        = (nrows == 0);
		const bool single_empty_line = (nrows == 1 && cur.GetLineView(0).size() == 0);
		if (unnamed && clean && (rows_empty || single_empty_line)) {
			bool ok = cur.OpenFromFile(path, err);
			if (!ok)
				return false;
			// Ensure swap recorder is attached for this buffer
			if (swap_) {
				swap_->Attach(&cur);
				cur.SetSwapRecorder(swap_->RecorderFor(&cur));
				swap_->NotifyFilenameChanged(cur);
			}
			// Setup highlighting using registry (extension + shebang)
			cur.EnsureHighlighter();
			std::string first = "";
			if (cur.Nrows() > 0)
				first = cur.GetLineString(0);
			std::string ft = kte::HighlighterRegistry::DetectForPath(path, first);
			if (!ft.empty()) {
				cur.SetFiletype(ft);
				cur.SetSyntaxEnabled(true);
				if (auto *eng = cur.Highlighter()) {
					eng->SetHighlighter(kte::HighlighterRegistry::CreateFor(ft));
					eng->InvalidateFrom(0);
				}
			} else {
				cur.SetFiletype("");
				cur.SetSyntaxEnabled(true);
				if (auto *eng = cur.Highlighter()) {
					eng->SetHighlighter(std::make_unique<kte::NullHighlighter>());
					eng->InvalidateFrom(0);
				}
			}
			// Defensive: ensure any active prompt is closed after a successful open
			CancelPrompt();
			return true;
		}
	}

	Buffer b;
	if (!b.OpenFromFile(path, err)) {
		return false;
	}
	// NOTE: swap recorder/attach must happen after the buffer is stored in its
	// final location (vector) because swap manager keys off Buffer*.
	// Initialize syntax highlighting by extension + shebang via registry (v2)
	b.EnsureHighlighter();
	std::string first = "";
	if (b.Nrows() > 0)
		first = b.GetLineString(0);
	std::string ft = kte::HighlighterRegistry::DetectForPath(path, first);
	if (!ft.empty()) {
		b.SetFiletype(ft);
		b.SetSyntaxEnabled(true);
		if (auto *eng = b.Highlighter()) {
			eng->SetHighlighter(kte::HighlighterRegistry::CreateFor(ft));
			eng->InvalidateFrom(0);
		}
	} else {
		b.SetFiletype("");
		b.SetSyntaxEnabled(true);
		if (auto *eng = b.Highlighter()) {
			eng->SetHighlighter(std::make_unique<kte::NullHighlighter>());
			eng->InvalidateFrom(0);
		}
	}
	// Add as a new buffer and switch to it
	std::size_t idx = AddBuffer(std::move(b));
	if (swap_) {
		swap_->NotifyFilenameChanged(buffers_[idx]);
	}
	SwitchTo(idx);
	// Defensive: ensure any active prompt is closed after a successful open
	CancelPrompt();
	return true;
}


void
Editor::RequestOpenFile(const std::string &path, const std::size_t line1)
{
	PendingOpen p;
	p.path  = path;
	p.line1 = line1;
	pending_open_.push_back(std::move(p));
}


bool
Editor::HasPendingOpens() const
{
	return !pending_open_.empty();
}


Editor::RecoveryPromptKind
Editor::PendingRecoveryPrompt() const
{
	return pending_recovery_prompt_;
}


void
Editor::CancelRecoveryPrompt()
{
	pending_recovery_prompt_ = RecoveryPromptKind::None;
	pending_recovery_open_   = PendingOpen{};
	pending_recovery_swap_path_.clear();
	pending_recovery_replay_err_.clear();
}


bool
Editor::ResolveRecoveryPrompt(const bool yes)
{
	const RecoveryPromptKind kind = pending_recovery_prompt_;
	if (kind == RecoveryPromptKind::None)
		return false;
	const PendingOpen req    = pending_recovery_open_;
	const std::string swp    = pending_recovery_swap_path_;
	const std::string rerr_s = pending_recovery_replay_err_;
	CancelRecoveryPrompt();

	std::string err;
	if (kind == RecoveryPromptKind::RecoverOrDiscard) {
		if (yes) {
			if (!OpenFile(req.path, err)) {
				SetStatus(err);
				return false;
			}
			Buffer *b = CurrentBuffer();
			if (!b) {
				SetStatus("Recovery failed: no buffer");
				return false;
			}
			std::string rerr;
			if (!kte::SwapManager::ReplayFile(*b, swp, rerr)) {
				SetStatus("Swap recovery failed: " + rerr);
				return false;
			}
			b->SetDirty(true);
			apply_pending_line(*this, req.line1);
			SetStatus("Recovered " + req.path);
			return true;
		}
		// Discard: best-effort delete swap, then open clean.
		(void) std::remove(swp.c_str());
		if (!OpenFile(req.path, err)) {
			SetStatus(err);
			return false;
		}
		apply_pending_line(*this, req.line1);
		SetStatus("Opened " + req.path);
		return true;
	}
	if (kind == RecoveryPromptKind::DeleteCorruptSwap) {
		if (yes) {
			(void) std::remove(swp.c_str());
		}
		if (!OpenFile(req.path, err)) {
			SetStatus(err);
			return false;
		}
		apply_pending_line(*this, req.line1);
		// Include a short hint that the swap was corrupt.
		if (!rerr_s.empty()) {
			SetStatus("Opened " + req.path + " (swap unreadable)");
		} else {
			SetStatus("Opened " + req.path);
		}
		return true;
	}
	return false;
}


bool
Editor::ProcessPendingOpens()
{
	if (PromptActive())
		return false;
	if (pending_recovery_prompt_ != RecoveryPromptKind::None)
		return false;

	bool opened_any = false;
	while (!pending_open_.empty()) {
		PendingOpen req = std::move(pending_open_.front());
		pending_open_.pop_front();
		if (req.path.empty())
			continue;

		std::string swp = kte::SwapManager::ComputeSwapPathForFilename(req.path);
		bool swp_exists = false;
		try {
			swp_exists = !swp.empty() && std::filesystem::exists(std::filesystem::path(swp));
		} catch (...) {
			swp_exists = false;
		}
		if (swp_exists) {
			Buffer tmp;
			std::string oerr;
			if (tmp.OpenFromFile(req.path, oerr)) {
				const std::string orig = buffer_bytes_via_views(tmp);
				std::string rerr;
				if (kte::SwapManager::ReplayFile(tmp, swp, rerr)) {
					const std::string rec = buffer_bytes_via_views(tmp);
					if (rec != orig) {
						pending_recovery_prompt_    = RecoveryPromptKind::RecoverOrDiscard;
						pending_recovery_open_      = req;
						pending_recovery_swap_path_ = swp;
						StartPrompt(PromptKind::Confirm, "Recover", "");
						SetStatus("Recover swap edits for " + req.path + "? (y/N, C-g cancel)");
						return opened_any;
					}
				} else {
					pending_recovery_prompt_     = RecoveryPromptKind::DeleteCorruptSwap;
					pending_recovery_open_       = req;
					pending_recovery_swap_path_  = swp;
					pending_recovery_replay_err_ = rerr;
					StartPrompt(PromptKind::Confirm, "Swap", "");
					SetStatus(
						"Swap file unreadable for " + req.path +
						". Delete it? (y/N, C-g cancel)");
					return opened_any;
				}
			}
		}

		std::string err;
		if (!OpenFile(req.path, err)) {
			SetStatus(err);
			opened_any = false;
			continue;
		}
		apply_pending_line(*this, req.line1);
		SetStatus("Opened " + req.path);
		opened_any = true;
		// Open at most one per call; frontends can call us again next frame.
		break;
	}
	return opened_any;
}


bool
Editor::SwitchTo(std::size_t index)
{
	if (index >= buffers_.size()) {
		return false;
	}
	curbuf_ = index;
	// Robustness: ensure a valid highlighter is installed when switching buffers
	Buffer &b = buffers_[curbuf_];
	if (b.SyntaxEnabled()) {
		b.EnsureHighlighter();
		if (auto *eng = b.Highlighter()) {
			if (!eng->HasHighlighter()) {
				// Try to set based on existing filetype; fall back to NullHighlighter
				if (!b.Filetype().empty()) {
					auto hl = kte::HighlighterRegistry::CreateFor(b.Filetype());
					if (hl) {
						eng->SetHighlighter(std::move(hl));
					} else {
						eng->SetHighlighter(std::make_unique<kte::NullHighlighter>());
					}
				} else {
					eng->SetHighlighter(std::make_unique<kte::NullHighlighter>());
				}
				eng->InvalidateFrom(0);
			}
		}
	}
	return true;
}


bool
Editor::CloseBuffer(std::size_t index)
{
	if (index >= buffers_.size()) {
		return false;
	}
	if (swap_) {
		// Always remove swap file when closing a buffer on normal exit.
		// Swap files are for crash recovery; on clean close, we don't need them.
		// This prevents stale swap files from accumulating (e.g., when used as git editor).
		swap_->Detach(&buffers_[index], true);
		buffers_[index].SetSwapRecorder(nullptr);
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
	repeatable_           = false;
	quit_requested_       = false;
	quit_confirm_pending_ = false;
	// Reset close-confirm/save state
	close_confirm_pending_ = false;
	close_after_save_      = false;
	buffers_.clear();
	curbuf_ = 0;
}


// --- Universal argument helpers ---
void
Editor::UArgStart()
{
	// If not active, start fresh; else multiply by 4 per ke semantics
	if (uarg_ == 0) {
		ucount_ = 0;
	} else {
		if (ucount_ == 0) {
			ucount_ = 1;
		}
		ucount_ *= 4;
	}
	uarg_ = 1;
	char buf[64];
	std::snprintf(buf, sizeof(buf), "C-u %d", ucount_);
	SetStatus(buf);
}


void
Editor::UArgDigit(int d)
{
	if (d < 0)
		d = 0;
	if (d > 9)
		d = 9;
	if (uarg_ == 0) {
		uarg_   = 1;
		ucount_ = 0;
	}
	ucount_ = ucount_ * 10 + d;
	char buf[64];
	std::snprintf(buf, sizeof(buf), "C-u %d", ucount_);
	SetStatus(buf);
}


void
Editor::UArgClear()
{
	uarg_   = 0;
	ucount_ = 0;
}


int
Editor::UArgGet()
{
	int n = (ucount_ > 0) ? ucount_ : 1;
	UArgClear();
	return n;
}