// Swap.h - swap journal (crash recovery) writer/manager for kte
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

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

// Lightweight interface that Buffer can call without depending on full manager impl
class SwapRecorder {
public:
	virtual ~SwapRecorder() = default;

	virtual void RecordInsert(Buffer &buf, int row, int col, std::string_view text) = 0;

	virtual void RecordDelete(Buffer &buf, int row, int col, std::size_t len) = 0;

	virtual void RecordSplit(Buffer &buf, int row, int col) = 0;

	virtual void RecordJoin(Buffer &buf, int row) = 0;

	virtual void NotifyFilenameChanged(Buffer &buf) = 0;

	virtual void SetSuspended(Buffer &buf, bool on) = 0;
};

// SwapManager manages sidecar swap files and a single background writer thread.
class SwapManager final : public SwapRecorder {
public:
	SwapManager();

	~SwapManager() override;

	// Attach a buffer to begin journaling. Safe to call multiple times; idempotent.
	void Attach(Buffer *buf);

	// Detach and close journal.
	void Detach(Buffer *buf);

	// SwapRecorder: Notify that the buffer's filename changed (e.g., SaveAs)
	void NotifyFilenameChanged(Buffer &buf) override;

	// SwapRecorder
	void RecordInsert(Buffer &buf, int row, int col, std::string_view text) override;

	void RecordDelete(Buffer &buf, int row, int col, std::size_t len) override;

	void RecordSplit(Buffer &buf, int row, int col) override;

	void RecordJoin(Buffer &buf, int row) override;

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
	void SetSuspended(Buffer &buf, bool on) override;

private:
	struct JournalCtx {
		std::string path;
		void *file{nullptr}; // FILE*
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
	};

	// Helpers
	static std::string ComputeSidecarPath(const Buffer &buf);

	static std::uint64_t now_ns();

	static bool ensure_parent_dir(const std::string &path);

	static bool write_header(JournalCtx &ctx);

	static bool open_ctx(JournalCtx &ctx);

	static void close_ctx(JournalCtx &ctx);

	static std::uint32_t crc32(const std::uint8_t *data, std::size_t len, std::uint32_t seed = 0);

	static void put_varu64(std::vector<std::uint8_t> &out, std::uint64_t v);

	static void put_u24(std::uint8_t dst[3], std::uint32_t v);

	void enqueue(Pending &&p);

	void writer_loop();

	void process_one(const Pending &p);

	// State
	SwapConfig cfg_{};
	std::unordered_map<Buffer *, JournalCtx> journals_;
	std::mutex mtx_;
	std::condition_variable cv_;
	std::vector<Pending> queue_;
	std::atomic<bool> running_{false};
	std::thread worker_;
};
} // namespace kte