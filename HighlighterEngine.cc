#include "HighlighterEngine.h"
#include "Buffer.h"
#include "LanguageHighlighter.h"

namespace kte {

HighlighterEngine::HighlighterEngine() = default;
HighlighterEngine::~HighlighterEngine() = default;

void
HighlighterEngine::SetHighlighter(std::unique_ptr<LanguageHighlighter> hl)
{
    hl_ = std::move(hl);
    cache_.clear();
    state_cache_.clear();
}

const LineHighlight &
HighlighterEngine::GetLine(const Buffer &buf, int row, std::uint64_t buf_version) const
{
    auto it = cache_.find(row);
    if (it != cache_.end()) {
        if (it->second.version == buf_version) {
            return it->second;
        }
    }
    LineHighlight updated;
    updated.version = buf_version;
    updated.spans.clear();
    if (!hl_) {
        auto &slot = cache_[row];
        slot = std::move(updated);
        return cache_[row];
    }

    if (auto *stateful = dynamic_cast<StatefulHighlighter *>(hl_.get())) {
        // Find nearest cached state at or before row-1 with matching version
        StatefulHighlighter::LineState prev_state;
        int start_row = -1;
        if (!state_cache_.empty()) {
            // linear search over map (unordered), track best candidate
            int best = -1;
            for (const auto &kv : state_cache_) {
                int r = kv.first;
                if (r <= row - 1 && kv.second.version == buf_version) {
                    if (r > best) {
                        best = r;
                    }
                }
            }
            if (best >= 0) {
                start_row = best;
                prev_state = state_cache_.at(best).state;
            }
        }

        // Walk from start_row+1 up to row computing states; only collect spans at the target row
        for (int r = start_row + 1; r <= row; ++r) {
            std::vector<HighlightSpan> tmp;
            std::vector<HighlightSpan> &out = (r == row) ? updated.spans : tmp;
            auto next_state = stateful->HighlightLineStateful(buf, r, prev_state, out);
            // store state for this row (state after finishing r)
            StateEntry se;
            se.version = buf_version;
            se.state = next_state;
            state_cache_[r] = se;
            prev_state = next_state;
        }
    } else {
        // Stateless path
        hl_->HighlightLine(buf, row, updated.spans);
    }

    auto &slot = cache_[row];
    slot = std::move(updated);
    return cache_[row];
}

void
HighlighterEngine::InvalidateFrom(int row)
{
    if (cache_.empty()) return;
    // Simple implementation: erase all rows >= row
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->first >= row) it = cache_.erase(it); else ++it;
    }
    if (!state_cache_.empty()) {
        for (auto it = state_cache_.begin(); it != state_cache_.end(); ) {
            if (it->first >= row) it = state_cache_.erase(it); else ++it;
        }
    }
}

} // namespace kte
