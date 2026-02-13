// Swap.h - swap journal (crash recovery) writer/manager for kte
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

#include "SwapRecorder.h"

class Buffer;

namespace kte {
// Minimal record types for stage 1
enum class SwapRecType : std::uint8_t {
	INS = 1,
	DEL = 2,
	SPLIT = 3,
	JOIN = 4,
	META = 0xF0,
	CHKPT = 0xFE,
};

struct SwapConfig {
	// Grouping and durability knobs (stage 1 defaults)
	unsigned flush_interval_ms{200}; // group small writes
	unsigned fsync_interval_ms{1000}; // at most once per second

	// Checkpoint/compaction knobs (stage 2 defaults)
	// A checkpoint is a full snapshot of the buffer content written as a CHKPT record.
	// Compaction rewrites the swap file to contain just the latest checkpoint.
	std::size_t checkpoint_bytes{1024 * 1024}; // request checkpoint after this many queued edit-bytes
	unsigned checkpoint_interval_ms{60000}; // request checkpoint at least this often while editing
	std::size_t compact_bytes{8 * 1024 * 1024}; // compact on checkpoint once journal grows beyond this

	// Cleanup / retention (best-effort)
	bool prune_on_startup{true};
	unsigned prune_max_age_days{30};
	std::size_t prune_max_files{2048};
};

// SwapManager manages sidecar swap files and a single background writer thread.
class SwapManager final {
public:
	SwapManager();

	~SwapManager();

	// Attach a buffer to begin journaling. Safe to call multiple times; idempotent.
	void Attach(Buffer *buf);

	// Detach and close journal.
	// If remove_file is true, the swap file is deleted after closing.
	// Intended for clean shutdown/close flows.
	void Detach(Buffer *buf, bool remove_file = false);

	// Reset (truncate-by-delete) the journal for a buffer after a clean save.
	// Best-effort: closes the current fd, deletes the swap file, and resumes recording.
	void ResetJournal(Buffer &buf);

	// Best-effort pruning of old swap files under the swap directory.
	// Never touches non-`.swp` files.
	void PruneSwapDir();

	// Block until all currently queued records have been written.
	// If buf is non-null, flushes all records (stage 1) but is primarily intended
	// for tests and shutdown.
	void Flush(Buffer *buf = nullptr);

	// Request a full-content checkpoint record for one buffer (or all buffers if buf is null).
	// This is best-effort and asynchronous; call Flush() if you need it written before continuing.
	void Checkpoint(Buffer *buf = nullptr);


	void SetConfig(const SwapConfig &cfg)
	{
		std::lock_guard<std::mutex> lg(mtx_);
		cfg_ = cfg;
		cv_.notify_one();
	}


	// Obtain a per-buffer recorder adapter that emits records for that buffer.
	// The returned pointer is owned by the SwapManager and remains valid until
	// Detach(buf) or SwapManager destruction.
	SwapRecorder *RecorderFor(Buffer *buf);

	// Notify that the buffer's filename changed (e.g., SaveAs)
	void NotifyFilenameChanged(Buffer &buf);

	// Replay a swap journal into an already-open buffer.
	// On success, the buffer content reflects all valid journal records.
	// On failure (corrupt/truncated/invalid), the buffer is left in whatever
	// state results from applying records up to the failure point; callers should
	// treat this as a recovery failure and surface `err`.
	static bool ReplayFile(Buffer &buf, const std::string &swap_path, std::string &err);

	// Compute the swap path for a file-backed buffer by filename.
	// Returns empty string if filename is empty.
	static std::string ComputeSwapPathForFilename(const std::string &filename);

	// Test-only hook to keep swap path logic centralized.
	// (Avoid duplicating naming rules in unit tests.)
#ifdef KTE_TESTS
	static std::string ComputeSwapPathForTests(const Buffer &buf)
	{
		return ComputeSidecarPath(buf);
	}
#endif

	// RAII guard to suspend recording for internal operations
	class SuspendGuard {
	public:
		SuspendGuard(SwapManager &m, Buffer *b);

		~SuspendGuard();

	private:
		SwapManager &m_;
		Buffer *buf_;
		bool prev_;
	};

	// Per-buffer toggle
	void SetSuspended(Buffer &buf, bool on);

private:
	class BufferRecorder final : public SwapRecorder {
	public:
		BufferRecorder(SwapManager &m, Buffer &b) : m_(m), buf_(b) {}

		void OnInsert(int row, int col, std::string_view bytes) override;

		void OnDelete(int row, int col, std::size_t len) override;

	private:
		SwapManager &m_;
		Buffer &buf_;
	};

	void RecordInsert(Buffer &buf, int row, int col, std::string_view text);

	void RecordDelete(Buffer &buf, int row, int col, std::size_t len);

	void RecordSplit(Buffer &buf, int row, int col);

	void RecordJoin(Buffer &buf, int row);

	void RecordCheckpoint(Buffer &buf, bool urgent_flush);

	void maybe_request_checkpoint(Buffer &buf, std::size_t approx_edit_bytes);

	struct JournalCtx {
		std::string path;
		int fd{-1};
		bool header_ok{false};
		bool suspended{false};
		std::uint64_t last_flush_ns{0};
		std::uint64_t last_fsync_ns{0};
		std::uint64_t last_chkpt_ns{0};
		std::uint64_t edit_bytes_since_chkpt{0};
		std::uint64_t approx_size_bytes{0};
	};

	struct Pending {
		Buffer *buf{nullptr};
		SwapRecType type{SwapRecType::INS};
		std::vector<std::uint8_t> payload; // framed payload only
		bool urgent_flush{false};
		std::uint64_t seq{0};
	};

	// Helpers
	static std::string ComputeSidecarPath(const Buffer &buf);

	static std::string ComputeSidecarPathForFilename(const std::string &filename);

	static std::uint64_t now_ns();

	static bool ensure_parent_dir(const std::string &path);

	static std::string SwapDirRoot();

	static bool write_header(int fd);

	static bool open_ctx(JournalCtx &ctx, const std::string &path);

	static void close_ctx(JournalCtx &ctx);

	static bool compact_to_checkpoint(JournalCtx &ctx, const std::vector<std::uint8_t> &chkpt_record);

	static std::uint32_t crc32(const std::uint8_t *data, std::size_t len, std::uint32_t seed = 0);

	static void put_le32(std::vector<std::uint8_t> &out, std::uint32_t v);

	static void put_le64(std::uint8_t dst[8], std::uint64_t v);

	static void put_u24_le(std::uint8_t dst[3], std::uint32_t v);

	void enqueue(Pending &&p);

	void writer_loop();

	void process_one(const Pending &p);

	// State
	SwapConfig cfg_{};
	std::unordered_map<Buffer *, JournalCtx> journals_;
	std::unordered_map<Buffer *, std::unique_ptr<BufferRecorder> > recorders_;
	std::mutex mtx_;
	std::condition_variable cv_;
	std::vector<Pending> queue_;
	std::uint64_t next_seq_{0};
	std::uint64_t last_processed_{0};
	std::uint64_t inflight_{0};
	std::atomic<bool> running_{false};
	std::thread worker_;
};
} // namespace kte