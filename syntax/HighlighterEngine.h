// HighlighterEngine.h - caching layer for per-line highlights
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>
#include <mutex>

#include "../Highlight.h"
#include "LanguageHighlighter.h"

class Buffer;

namespace kte {
class HighlighterEngine {
public:
	HighlighterEngine();

	~HighlighterEngine();

	void SetHighlighter(std::unique_ptr<LanguageHighlighter> hl);

	// Retrieve highlights for a given line and buffer version.
	// Returns a copy to avoid lifetime issues across threads/renderers.
	// If cache is stale, recompute using the current highlighter.
	LineHighlight GetLine(const Buffer &buf, int row, std::uint64_t buf_version) const;

	// Invalidate cached lines from row (inclusive)
	void InvalidateFrom(int row);

	// The buffer changed at `row` and is now at `buf_version`. Highlighting
	// of a row depends only on the rows above it, so rows before `row` stay
	// cached. A version change that arrives without OnEdit (a path that
	// changed content without reporting it) still clears the whole cache.
	void OnEdit(int row, std::uint64_t buf_version);


	bool HasHighlighter() const
	{
		return static_cast<bool>(hl_);
	}


	// Compute highlights for the visible range so drawing hits the cache.
	// There is deliberately no background warming: a worker thread would read
	// the Buffer while the main thread edits it (and could outlive a moved
	// Buffer), and the PieceTable is not safe for concurrent read and write.
	// warm_margin is accepted for source compatibility and ignored.
	void PrefetchViewport(const Buffer &buf, int first_row, int row_count, std::uint64_t buf_version,
	                      int warm_margin = 200) const;

private:
	std::unique_ptr<LanguageHighlighter> hl_;
	// Cached spans by row, valid for version_ (ordered so InvalidateFrom can
	// drop a tail cheaply).
	mutable std::map<int, LineHighlight> cache_;

	// For stateful highlighters: states_[r] is the state after row r, for
	// every row 0..states_.size()-1 (always a contiguous prefix).
	mutable std::vector<StatefulHighlighter::LineState> states_;

	// Buffer version the caches describe.
	mutable std::uint64_t version_{0};
	mutable bool have_version_{false};

	void clear_caches_locked() const;

	void invalidate_from_locked(int row) const;

	// Guards the caches above.
	mutable std::mutex mtx_;
};
} // namespace kte
