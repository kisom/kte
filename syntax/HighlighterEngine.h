// HighlighterEngine.h - caching layer for per-line highlights
#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
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
	// Simple cache by row index (mutable to allow caching in const GetLine)
	mutable std::unordered_map<int, LineHighlight> cache_;

	// For stateful highlighters, remember per-line state (state after finishing that row)
	struct StateEntry {
		std::uint64_t version{0};
		// Using the interface type; forward-declare via header
		StatefulHighlighter::LineState state;
	};

	mutable std::unordered_map<int, StateEntry> state_cache_;

	// Track best known contiguous state row for a given version to avoid O(n) scans
	mutable std::unordered_map<std::uint64_t, int> state_last_contig_;

	// Guards the caches above.
	mutable std::mutex mtx_;
};
} // namespace kte
