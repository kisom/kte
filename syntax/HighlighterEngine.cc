#include "HighlighterEngine.h"
#include "../Buffer.h"
#include "LanguageHighlighter.h"

namespace kte {
HighlighterEngine::HighlighterEngine() = default;


HighlighterEngine::~HighlighterEngine() = default;


void
HighlighterEngine::SetHighlighter(std::unique_ptr<LanguageHighlighter> hl)
{
	std::lock_guard<std::mutex> lock(mtx_);
	hl_ = std::move(hl);
	cache_.clear();
	state_cache_.clear();
	state_last_contig_.clear();
}


LineHighlight
HighlighterEngine::GetLine(const Buffer &buf, int row, std::uint64_t buf_version) const
{
	std::unique_lock<std::mutex> lock(mtx_);
	auto it = cache_.find(row);
	if (it != cache_.end() && it->second.version == buf_version) {
		return it->second; // return by value (copy)
	}

	// We'll compute into a local result to avoid exposing references to cache
	LineHighlight result;
	result.version = buf_version;
	result.spans.clear();

	if (!hl_) {
		// Cache empty result and return it
		cache_[row] = result;
		return result;
	}

	// Copy shared_ptr-like raw pointer for use outside critical sections
	LanguageHighlighter *hl_ptr = hl_.get();
	bool is_stateful            = dynamic_cast<StatefulHighlighter *>(hl_ptr) != nullptr;

	if (!is_stateful) {
		// Stateless fast path: we can release the lock while computing to reduce contention
		lock.unlock();
		hl_ptr->HighlightLine(buf, row, result.spans);
		// Update cache and return
		std::lock_guard<std::mutex> gl(mtx_);
		cache_[row] = result;
		return result;
	}

	// Stateful path: we need to walk from a known previous state. Keep lock while consulting caches,
	// but release during heavy computation.
	auto *stateful = static_cast<StatefulHighlighter *>(hl_ptr);

	StatefulHighlighter::LineState prev_state;
	int start_row = -1;

	// Fast path: state_last_contig_ tracks the highest row we've cached state
	// for, per version. If that row is already below our target, it's
	// necessarily the best anchor the O(n) scan below would have found, so we
	// can skip the scan entirely. Re-validated against state_cache_ before
	// use since InvalidateFrom()/SetHighlighter() may have raced/cleared it.
	auto contig_it = state_last_contig_.find(buf_version);
	if (contig_it != state_last_contig_.end() && contig_it->second < row) {
		auto sc_it = state_cache_.find(contig_it->second);
		if (sc_it != state_cache_.end() && sc_it->second.version == buf_version) {
			start_row  = contig_it->second;
			prev_state = sc_it->second.state;
		}
	}

	if (start_row < 0 && !state_cache_.empty()) {
		// linear search over map (unordered), track best candidate
		int best = -1;
		for (const auto &kv: state_cache_) {
			int r = kv.first;
			// Only use cached state if it's for the current version and row still exists
			if (r <= row - 1 && kv.second.version == buf_version) {
				// Validate that the cached row index is still valid in the buffer
				if (r >= 0 && static_cast<std::size_t>(r) < buf.Nrows()) {
					if (r > best)
						best = r;
				}
			}
		}
		if (best >= 0) {
			start_row  = best;
			prev_state = state_cache_.at(best).state;
		}
	}

	// We'll compute states and the target line's spans without holding the lock for most of the work.
	// Create a local copy of prev_state and iterate rows; we will update caches under lock.
	lock.unlock();
	StatefulHighlighter::LineState cur_state = prev_state;
	for (int r = start_row + 1; r <= row; ++r) {
		std::vector<HighlightSpan> tmp;
		std::vector<HighlightSpan> &out = (r == row) ? result.spans : tmp;
		auto next_state                 = stateful->HighlightLineStateful(buf, r, cur_state, out);
		// Update state cache for r
		std::lock_guard<std::mutex> gl(mtx_);
		StateEntry se;
		se.version      = buf_version;
		se.state        = next_state;
		state_cache_[r] = se;
		cur_state       = next_state;
		int &contig     = state_last_contig_[buf_version];
		if (r > contig)
			contig = r;
	}

	// Store in cache and return by value
	lock.lock();
	cache_[row] = result;
	return result;
}


void
HighlighterEngine::InvalidateFrom(int row)
{
	std::lock_guard<std::mutex> lock(mtx_);
	if (cache_.empty())
		return;
	// Simple implementation: erase all rows >= row
	for (auto it = cache_.begin(); it != cache_.end();) {
		if (it->first >= row)
			it = cache_.erase(it);
		else
			++it;
	}
	if (!state_cache_.empty()) {
		for (auto it = state_cache_.begin(); it != state_cache_.end();) {
			if (it->first >= row)
				it = state_cache_.erase(it);
			else
				++it;
		}
	}
	// A version's tracked contiguous-state row is no longer valid once any
	// row at or above it has been evicted from state_cache_ above.
	for (auto it = state_last_contig_.begin(); it != state_last_contig_.end();) {
		if (it->second >= row)
			it = state_last_contig_.erase(it);
		else
			++it;
	}
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