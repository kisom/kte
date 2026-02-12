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
};

// SwapManager manages sidecar swap files and a single background writer thread.
class SwapManager final {
public:
	SwapManager();

	~SwapManager();

	// Attach a buffer to begin journaling. Safe to call multiple times; idempotent.
	void Attach(Buffer *buf);

	// Detach and close journal.
	void Detach(Buffer *buf);

	// Block until all currently queued records have been written.
	// If buf is non-null, flushes all records (stage 1) but is primarily intended
	// for tests and shutdown.
	void Flush(Buffer *buf = nullptr);

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

	struct JournalCtx {
		std::string path;
		int fd{-1};
		bool header_ok{false};
		bool suspended{false};
		std::uint64_t last_flush_ns{0};
		std::uint64_t last_fsync_ns{0};
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

	static std::uint64_t now_ns();

	static bool ensure_parent_dir(const std::string &path);

	static bool write_header(int fd);

	static bool open_ctx(JournalCtx &ctx, const std::string &path);

	static void close_ctx(JournalCtx &ctx);

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