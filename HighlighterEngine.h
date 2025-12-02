// HighlighterEngine.h - caching layer for per-line highlights
#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

#include "Highlight.h"
#include "LanguageHighlighter.h"

class Buffer;

namespace kte {

class HighlighterEngine {
public:
    HighlighterEngine();
    ~HighlighterEngine();

    void SetHighlighter(std::unique_ptr<LanguageHighlighter> hl);

    // Retrieve highlights for a given line and buffer version.
    // If cache is stale, recompute using the current highlighter.
    const LineHighlight &GetLine(const Buffer &buf, int row, std::uint64_t buf_version) const;

    // Invalidate cached lines from row (inclusive)
    void InvalidateFrom(int row);

    bool HasHighlighter() const { return static_cast<bool>(hl_); }

    // Phase 3: viewport-first prefetch and background warming
    // Compute only the visible range now, and enqueue a background warm-around task.
    // warm_margin: how many extra lines above/below to warm in the background.
    void PrefetchViewport(const Buffer &buf, int first_row, int row_count, std::uint64_t buf_version, int warm_margin = 200) const;

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

    // Thread-safety for caches and background worker state
    mutable std::mutex mtx_;

    // Background warmer
    struct WarmRequest {
        const Buffer *buf{nullptr};
        std::uint64_t version{0};
        int start_row{0};
        int end_row{0}; // inclusive
    };
    mutable std::condition_variable cv_;
    mutable std::thread worker_;
    mutable std::atomic<bool> worker_running_{false};
    mutable bool has_request_{false};
    mutable WarmRequest pending_{};

    void ensure_worker_started() const;
    void worker_loop() const;
};

} // namespace kte
