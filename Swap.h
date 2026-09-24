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
#include <deque>
#include <thread>
#include <atomic>

#include "SwapRecorder.h"
#include "ErrorRecovery.h"

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

	// Main thread, called once per frame: request the recovery checkpoint of
	// any journal left with a gap (a lost record) whose last attempt is more
	// than the retry interval ago. Otherwise that checkpoint is only
	// requested by the next edit, and a journal that lost a record just
	// before the user stopped typing would stay behind indefinitely.
	void RetryGapCheckpoints();

	// Main thread: the oldest pending message for the user (a journal that
	// another kte holds, or that fell behind because writes failed), or ""
	// when there is none. Such failures otherwise went only to the error
	// log, leaving crash recovery silently off or incomplete.
	std::string TakeUserNotice();


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

	// Re-key an attached buffer's journal/recorder entries after its Buffer object
	// has moved to a new address (e.g. std::vector<Buffer> reallocation/erase-shift).
	// Callers must ensure no swap records for old_addr are in flight (see Flush())
	// before calling this, and must not call it with an address that isn't
	// currently attached. Returns the recorder for new_addr (nullptr if old_addr
	// wasn't attached); the caller is responsible for calling
	// new_buf->SetSwapRecorder() with the result.
	SwapRecorder *Rehome(Buffer *old_addr, Buffer *new_addr);

	// Notify that the buffer's filename changed (e.g., SaveAs)
	void NotifyFilenameChanged(Buffer &buf);

	// Replay a swap journal into an already-open buffer.
	// On success, the buffer content reflects all valid journal records.
	// On failure (corrupt/truncated/invalid), the buffer is left in whatever
	// state results from applying records up to the failure point; callers should
	// treat this as a recovery failure and surface `err`.
	//
	// A record cut short at end of file (a crash mid-append) ends the replay
	// successfully; err then describes the incomplete record. If valid_bytes
	// is given, it receives the length of the journal's valid prefix, so a
	// caller that keeps using the journal can truncate the torn tail first.
	static bool ReplayFile(Buffer &buf, const std::string &swap_path, std::string &err,
	                       std::uint64_t *valid_bytes = nullptr);

	// Compute the swap path for a file-backed buffer by filename.
	// Returns empty string if filename is empty.
	static std::string ComputeSwapPathForFilename(const std::string &filename);

	// True if another process currently holds the journal at swap_path (it
	// is live, not left by a crash): it must be neither replayed nor removed.
	static bool JournalInUse(const std::string &swap_path);

	// False if the journal records which version of `file_path` it applies
	// to and the file on disk no longer matches (edited or replaced since
	// the journal started); replaying it would apply edits to the wrong text.
	// Journals without that record (older kte) are assumed to match.
	static bool JournalMatchesFile(const std::string &swap_path, const std::string &file_path);

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

	// Error reporting for background thread
	struct SwapError {
		std::uint64_t timestamp_ns{0};
		std::string message;
		std::string buffer_name; // filename or "<unnamed>"
	};

	// Query error state (thread-safe)
	bool HasErrors() const;

	std::string GetLastError() const;

	std::size_t GetErrorCount() const;

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
		// A record was lost (write failure, breaker open, oversize). Records are
		// position-based, so later ones no longer apply to the journal's state:
		// drop them until a checkpoint re-establishes the full content.
		bool gap{false};
		bool gap_notified{false}; // the user was told about the current gap
		bool lockout_notified{false}; // told about the lock; kept across ResetJournal
		std::uint64_t gap_chkpt_request_ns{0};
		bool gap_unfixable_reported{false}; // gap on a buffer too large to checkpoint
		// Another kte process holds this journal's lock: do not write to it.
		bool locked_out{false};
		// Identity of the file the journal's records apply to (captured when
		// the journal starts) and written into its header, so recovery can
		// tell whether the file changed since.
		bool has_base{false};
		std::uint64_t base_size{0};
		std::int64_t base_mtime_ns{0};
		bool has_base_crc{false}; // CRC-32 of the base file's content
		std::uint32_t base_crc{0};
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

	static bool write_header(int fd, const JournalCtx &ctx);

	static bool open_ctx(JournalCtx &ctx, const std::string &path, std::string &err, bool *locked_out = nullptr);

	static void close_ctx(JournalCtx &ctx);

	static bool compact_to_checkpoint(JournalCtx &ctx, const std::vector<std::uint8_t> &chkpt_record,
	                                  std::string &err);

	static std::uint32_t crc32(const std::uint8_t *data, std::size_t len, std::uint32_t seed = 0);

	// CRC-32 of a file's content; false if it cannot be read.
	static bool file_crc32(const std::string &path, std::uint32_t &out);

	// Identity (size, mtime, content CRC) of the file a journal's records
	// apply to. Computed without holding mtx_ (it reads the file), then
	// stored into the JournalCtx under it.
	struct BaseId {
		bool has{false};
		std::uint64_t size{0};
		std::int64_t mtime_ns{0};
		bool has_crc{false};
		std::uint32_t crc{0};
	};

	static BaseId compute_base(const std::string &file);

	static void apply_base(JournalCtx &ctx, const BaseId &id);

	static void put_le32(std::vector<std::uint8_t> &out, std::uint32_t v);

	static void put_le64(std::uint8_t dst[8], std::uint64_t v);

	static void put_u24_le(std::uint8_t dst[3], std::uint32_t v);

	void enqueue(Pending &&p);

	void writer_loop();

	void process_one(const Pending &p);

	// Error reporting helper (called from writer thread)
	void report_error(const std::string &message, Buffer *buf = nullptr);

	// State
	SwapConfig cfg_{};
	std::unordered_map<Buffer *, JournalCtx> journals_;
	std::unordered_map<Buffer *, std::unique_ptr<BufferRecorder> > recorders_;
	mutable std::mutex mtx_;
	// Serialises closing journal fds with the writer's periodic fsync, so
	// neither can act on an fd the other has closed (or that was reused).
	// Taken before mtx_ when both are needed; edits (which take mtx_) never
	// wait for an fsync.
	mutable std::mutex io_mtx_;
	std::condition_variable cv_;
	std::vector<Pending> queue_;
	std::uint64_t next_seq_{0};
	std::uint64_t last_processed_{0};
	std::uint64_t inflight_{0};
	std::atomic<bool> running_{false};
	std::thread worker_;

	// Error tracking (protected by mtx_)
	std::deque<SwapError> errors_; // bounded to max 100 entries
	std::deque<std::string> user_notices_; // see TakeUserNotice(); guarded by mtx_

	// Queue a message for the user (mtx_ must be held).
	void notify_user_locked_(std::string msg);
	std::size_t total_error_count_{0};

	// Circuit breaker for swap operations (protected by mtx_)
	CircuitBreaker circuit_breaker_;
};
} // namespace kte