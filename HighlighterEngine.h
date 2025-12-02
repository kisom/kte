// HighlighterEngine.h - caching layer for per-line highlights
#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

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
};

} // namespace kte
