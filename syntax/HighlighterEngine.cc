#include "HighlighterEngine.h"
#include "../Buffer.h"
#include "LanguageHighlighter.h"

#include <algorithm>

namespace kte {
HighlighterEngine::HighlighterEngine() = default;


HighlighterEngine::~HighlighterEngine() = default;


void
HighlighterEngine::SetHighlighter(std::unique_ptr<LanguageHighlighter> hl)
{
	std::lock_guard<std::mutex> lock(mtx_);
	hl_ = std::move(hl);
	clear_caches_locked();
}


void
HighlighterEngine::clear_caches_locked() const
{
	cache_.clear();
	states_.clear();
}


void
HighlighterEngine::invalidate_from_locked(int row) const
{
	if (row < 0)
		row = 0;
	cache_.erase(cache_.lower_bound(row), cache_.end());
	if (states_.size() > static_cast<std::size_t>(row))
		states_.resize(static_cast<std::size_t>(row));
}


LineHighlight
HighlighterEngine::GetLine(const Buffer &buf, int row, std::uint64_t buf_version) const
{
	std::lock_guard<std::mutex> lock(mtx_);
	if (!have_version_ || buf_version != version_) {
		// Content changed without OnEdit: nothing cached can be trusted.
		clear_caches_locked();
		version_      = buf_version;
		have_version_ = true;
	}
	auto it = cache_.find(row);
	if (it != cache_.end())
		return it->second; // return by value (copy)

	LineHighlight result;
	result.version = buf_version;

	if (!hl_ || row < 0) {
		cache_[row] = result;
		return result;
	}

	auto *stateful = dynamic_cast<StatefulHighlighter *>(hl_.get());
	if (!stateful) {
		hl_->HighlightLine(buf, row, result.spans);
		cache_[row] = result;
		trim_cache_locked(row);
		return result;
	}

	// Stateful: continue from the last row with a known end state (or the
	// row just above the target, if that is known), caching every row's
	// spans and state along the way so neighbouring rows are cache hits.
	const int known = static_cast<int>(states_.size()) - 1;
	const int start = std::min(known, row - 1);
	StatefulHighlighter::LineState state = (start >= 0) ? states_[static_cast<std::size_t>(start)]
	                                                    : StatefulHighlighter::LineState{};
	const int nrows = static_cast<int>(buf.Nrows());
	for (int r = start + 1; r <= row && r < nrows; ++r) {
		LineHighlight lh;
		lh.version = buf_version;
		state      = stateful->HighlightLineStateful(buf, r, state, lh.spans);
		if (static_cast<std::size_t>(r) == states_.size())
			states_.push_back(state);
		else
			states_[static_cast<std::size_t>(r)] = state;
		if (r == row)
			result = lh;
		else if (row - r <= kCacheNear)
			cache_.insert_or_assign(r, std::move(lh));
		// Rows further above only needed their end state (kept in states_):
		// caching their spans too held about 10x the file size in memory
		// after jumping to the end of a large file.
	}
	cache_[row] = result;
	trim_cache_locked(row);
	return result;
}


void
HighlighterEngine::trim_cache_locked(const int row) const
{
	if (cache_.size() <= kCacheMax)
		return;
	cache_.erase(cache_.begin(), cache_.lower_bound(row - kCacheNear));
	cache_.erase(cache_.upper_bound(row + kCacheNear), cache_.end());
}


void
HighlighterEngine::InvalidateFrom(int row)
{
	std::lock_guard<std::mutex> lock(mtx_);
	invalidate_from_locked(row);
}


void
HighlighterEngine::OnEdit(int row, std::uint64_t buf_version)
{
	std::lock_guard<std::mutex> lock(mtx_);
	if (have_version_)
		invalidate_from_locked(row);
	version_      = buf_version;
	have_version_ = true;
}


void
HighlighterEngine::PrefetchViewport(const Buffer &buf, int first_row, int row_count, std::uint64_t buf_version,
                                    int /*warm_margin*/) const
{
	if (row_count <= 0)
		return;
	// Synchronously compute visible rows to ensure cache hits during draw
	int start    = std::max(0, first_row);
	int end      = start + row_count - 1;
	int max_rows = static_cast<int>(buf.Nrows());
	if (start >= max_rows)
		return;
	if (end >= max_rows)
		end = max_rows - 1;

	for (int r = start; r <= end; ++r) {
		(void) GetLine(buf, r, buf_version);
	}
}
} // namespace kte