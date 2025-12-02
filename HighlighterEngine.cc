#include "HighlighterEngine.h"
#include "Buffer.h"
#include "LanguageHighlighter.h"
#include <thread>

namespace kte {

HighlighterEngine::HighlighterEngine() = default;
HighlighterEngine::~HighlighterEngine()
{
    // stop background worker
    if (worker_running_.load()) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            worker_running_.store(false);
            has_request_ = true; // wake it up to exit
        }
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
}

void
HighlighterEngine::SetHighlighter(std::unique_ptr<LanguageHighlighter> hl)
{
    std::lock_guard<std::mutex> lock(mtx_);
    hl_ = std::move(hl);
    cache_.clear();
    state_cache_.clear();
    state_last_contig_.clear();
}

const LineHighlight &
HighlighterEngine::GetLine(const Buffer &buf, int row, std::uint64_t buf_version) const
{
    std::unique_lock<std::mutex> lock(mtx_);
    auto it = cache_.find(row);
    if (it != cache_.end() && it->second.version == buf_version) {
        return it->second;
    }

    // Prepare destination slot to reuse its capacity and avoid allocations
    LineHighlight &slot = cache_[row];
    slot.version = buf_version;
    slot.spans.clear();

    if (!hl_) {
        return slot;
    }

    // Copy shared_ptr-like raw pointer for use outside critical sections
    LanguageHighlighter *hl_ptr = hl_.get();
    bool is_stateful = dynamic_cast<StatefulHighlighter *>(hl_ptr) != nullptr;

    if (!is_stateful) {
        // Stateless fast path: we can release the lock while computing to reduce contention
        auto &out = slot.spans;
        lock.unlock();
        hl_ptr->HighlightLine(buf, row, out);
        return cache_.at(row);
    }

    // Stateful path: we need to walk from a known previous state. Keep lock while consulting caches,
    // but release during heavy computation.
    auto *stateful = static_cast<StatefulHighlighter *>(hl_ptr);

    StatefulHighlighter::LineState prev_state;
    int start_row = -1;
    if (!state_cache_.empty()) {
        // linear search over map (unordered), track best candidate
        int best = -1;
        for (const auto &kv : state_cache_) {
            int r = kv.first;
            if (r <= row - 1 && kv.second.version == buf_version) {
                if (r > best) best = r;
            }
        }
        if (best >= 0) {
            start_row = best;
            prev_state = state_cache_.at(best).state;
        }
    }

    // We'll compute states and the target line's spans without holding the lock for most of the work.
    // Create a local copy of prev_state and iterate rows; we will update caches under lock.
    lock.unlock();
    StatefulHighlighter::LineState cur_state = prev_state;
    for (int r = start_row + 1; r <= row; ++r) {
        std::vector<HighlightSpan> tmp;
        std::vector<HighlightSpan> &out = (r == row) ? slot.spans : tmp;
        auto next_state = stateful->HighlightLineStateful(buf, r, cur_state, out);
        // Update state cache for r
        std::lock_guard<std::mutex> gl(mtx_);
        StateEntry se;
        se.version = buf_version;
        se.state = next_state;
        state_cache_[r] = se;
        cur_state = next_state;
    }

    // Return reference under lock to ensure slot's address stability in map
    lock.lock();
    return cache_.at(row);
}

void
HighlighterEngine::InvalidateFrom(int row)
{
    std::lock_guard<std::mutex> lock(mtx_);
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

void HighlighterEngine::ensure_worker_started() const
{
    if (worker_running_.load()) return;
    worker_running_.store(true);
    worker_ = std::thread([this]() { this->worker_loop(); });
}

void HighlighterEngine::worker_loop() const
{
    std::unique_lock<std::mutex> lock(mtx_);
    while (worker_running_.load()) {
        cv_.wait(lock, [this]() { return has_request_ || !worker_running_.load(); });
        if (!worker_running_.load()) break;
        WarmRequest req = pending_;
        has_request_ = false;
        // Copy locals then release lock while computing
        lock.unlock();
        if (req.buf) {
            int start = std::max(0, req.start_row);
            int end = std::max(start, req.end_row);
            for (int r = start; r <= end; ++r) {
                // Re-check version staleness quickly by peeking cache version; not strictly necessary
                // Compute line; GetLine is thread-safe
                (void)this->GetLine(*req.buf, r, req.version);
            }
        }
        lock.lock();
    }
}

void HighlighterEngine::PrefetchViewport(const Buffer &buf, int first_row, int row_count, std::uint64_t buf_version, int warm_margin) const
{
    if (row_count <= 0) return;
    // Synchronously compute visible rows to ensure cache hits during draw
    int start = std::max(0, first_row);
    int end = start + row_count - 1;
    int max_rows = static_cast<int>(buf.Nrows());
    if (start >= max_rows) return;
    if (end >= max_rows) end = max_rows - 1;

    for (int r = start; r <= end; ++r) {
        (void)GetLine(buf, r, buf_version);
    }

    // Enqueue background warm-around
    int warm_start = std::max(0, start - warm_margin);
    int warm_end = std::min(max_rows - 1, end + warm_margin);
    {
        std::lock_guard<std::mutex> lock(mtx_);
        pending_.buf = &buf;
        pending_.version = buf_version;
        pending_.start_row = warm_start;
        pending_.end_row = warm_end;
        has_request_ = true;
    }
    ensure_worker_started();
    cv_.notify_one();
}

} // namespace kte
