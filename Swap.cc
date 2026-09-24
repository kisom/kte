#include "Swap.h"
#include "Buffer.h"
#include "ErrorHandler.h"
#include "SyscallWrappers.h"
#include "ErrorRecovery.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <cerrno>

namespace fs = std::filesystem;

namespace kte {
namespace {
// Records store their payload length in 24 bits.
constexpr std::size_t kMaxRecordPayload = 0xFFFFFFu;
// While a journal has a gap, request a recovery checkpoint at most this often.
constexpr std::uint64_t kGapCheckpointIntervalNs = 1000000000ULL;
// Base files up to this size get their CRC computed on open/save; larger ones
// on the writer thread (see compute_base). Adjustable for tests.
std::uint64_t g_sync_crc_limit = std::uint64_t{64} << 20;
// Checkpoints of buffers larger than this are spaced by buffer size.
constexpr std::size_t kLargeCheckpointBytes = std::size_t{16} << 20;
} // namespace

static void remove_journal_unless_held(const std::string &path, bool locked_out);

namespace {
constexpr std::uint8_t MAGIC[8] = {'K', 'T', 'E', '_', 'S', 'W', 'P', '\0'};
// Version 2 adds chunked checkpoints (CHKPT_BEGIN/DATA/END); version 1
// journals (which never contain them) are still read.
constexpr std::uint32_t VERSION = 2;


static std::string
snapshot_buffer_bytes(const Buffer &b)
{
	const std::size_t nrows = b.Nrows();
	std::string out;
	// Cheap lower bound: sum of row sizes.
	std::size_t approx = 0;
	for (std::size_t i = 0; i < nrows; i++)
		approx += b.GetLineView(i).size();
	out.reserve(approx);
	for (std::size_t i = 0; i < nrows; i++) {
		auto v = b.GetLineView(i);
		out.append(v.data(), v.size());
	}
	return out;
}


static fs::path
xdg_state_home()
{
	if (const char *p = std::getenv("XDG_STATE_HOME")) {
		if (*p)
			return fs::path(p);
	}
	if (const char *home = std::getenv("HOME")) {
		if (*home)
			return fs::path(home) / ".local" / "state";
	}
	// Last resort: still provide a stable per-user-ish location.
	return fs::temp_directory_path() / "kte" / "state";
}


static fs::path
swap_root_dir()
{
	return xdg_state_home() / "kte" / "swap";
}


static std::uint64_t
fnv1a64(std::string_view s)
{
	std::uint64_t h = 14695981039346656037ULL;
	for (unsigned char ch: s) {
		h ^= (std::uint64_t) ch;
		h *= 1099511628211ULL;
	}
	return h;
}


static std::string
hex_u64(std::uint64_t v)
{
	static const char *kHex = "0123456789abcdef";
	char out[16];
	for (int i = 15; i >= 0; --i) {
		out[i] = kHex[v & 0xFULL];
		v      >>= 4;
	}
	return std::string(out, sizeof(out));
}


// Write all bytes in buf to fd, handling EINTR and partial writes.
static bool
write_full(int fd, const void *buf, size_t len)
{
	const std::uint8_t *p = static_cast<const std::uint8_t *>(buf);
	while (len > 0) {
		ssize_t n = ::write(fd, p, len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (n == 0)
			return false; // shouldn't happen for regular files; treat as error
		p   += static_cast<size_t>(n);
		len -= static_cast<size_t>(n);
	}
	return true;
}


static std::string
encode_path_key(std::string s)
{
	// Turn an absolute path like "/home/kyle/tmp/test.txt" into
	// "home!kyle!tmp!test.txt" so swap files are human-identifiable.
	//
	// Notes:
	//  - We strip a single leading path separator so absolute paths don't start with '!'.
	//  - We replace both '/' and '\\' with '!'.
	//  - We leave other characters as-is (spaces are OK on POSIX).
	//  - '!' and '%' in the path itself are escaped as %21 and %25, so the
	//    mapping is one-to-one: "/x/a!b" and "/x/a/b" used to share a journal.
	if (!s.empty() && (s[0] == '/' || s[0] == '\\'))
		s.erase(0, 1);
	std::string out;
	out.reserve(s.size());
	for (char ch: s) {
		if (ch == '/' || ch == '\\')
			out.push_back('!');
		else if (ch == '!')
			out += "%21";
		else if (ch == '%')
			out += "%25";
		else
			out.push_back(ch);
	}
	return out;
}


// stat() a file for journal base identity. False if it is not a regular file.
static bool
stat_base(const std::string &file, std::uint64_t &size, std::int64_t &mtime_ns)
{
	struct stat st{};
	if (file.empty() || ::stat(file.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
		return false;
	size = static_cast<std::uint64_t>(st.st_size);
#if defined(__APPLE__)
	mtime_ns = static_cast<std::int64_t>(st.st_mtimespec.tv_sec) * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
	mtime_ns = static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
#endif
	return true;
}


static std::string
compute_swap_path_for_filename(const std::string &filename)
{
	if (filename.empty())
		return std::string();
	// Always place swap under an XDG home-appropriate state directory.
	// This avoids cluttering working directories and prevents stomping on
	// swap files when multiple different paths share the same basename.
	fs::path root = swap_root_dir();

	fs::path p(filename);
	std::string key;
	try {
		key = fs::weakly_canonical(p).string();
	} catch (...) {
		try {
			key = fs::absolute(p).string();
		} catch (...) {
			key = filename;
		}
	}
	std::string encoded = encode_path_key(key);
	if (!encoded.empty()) {
		std::string name = encoded + ".swp";
		// Avoid filesystem/path length issues; fall back to hashed naming.
		// NAME_MAX is often 255 on POSIX, but keep extra headroom.
		if (name.size() <= 200) {
			return (root / name).string();
		}
	}

	// Fallback: stable, shorter name based on basename + hash.
	std::string base         = p.filename().string();
	const std::string suffix = "." + hex_u64(fnv1a64(key)) + ".swp";
	// A long basename made even this name exceed NAME_MAX (255), so the
	// journal could never be created. Shorten only such names (the hash
	// keeps them distinct), on a UTF-8 boundary, leaving room for the
	// ".tmp" sibling that compaction writes (at exactly 255 compaction
	// failed on every checkpoint and the journal grew without bound).
	constexpr std::size_t kMaxName = 255 - 4; // strlen(".tmp")
	if (base.size() + suffix.size() > kMaxName) {
		std::size_t keep = kMaxName - suffix.size();
		while (keep > 0 && (static_cast<unsigned char>(base[keep]) & 0xC0) == 0x80)
			--keep;
		base.resize(keep);
	}
	const std::string name = base + suffix;
	return (root / name).string();
}
}


SwapManager::SwapManager()
{
	running_.store(true);
	worker_ = std::thread([this] {
		this->writer_loop();
	});
	// Best-effort prune of old swap files.
	// Safe early in startup: journals_ is still empty and no fds are open yet.
	if (cfg_.prune_on_startup) {
		PruneSwapDir();
	}
}


SwapManager::~SwapManager()
{
	// Best-effort: drain queued records before stopping the writer.
	Flush();
	running_.store(false);
	cv_.notify_all();
	if (worker_.joinable())
		worker_.join();
	// Close all journals
	for (auto &kv: journals_) {
		close_ctx(kv.second);
	}
}


void
SwapManager::Checkpoint(Buffer *buf)
{
	if (buf) {
		RecordCheckpoint(*buf, false);
		return;
	}
	// All buffers
	std::vector<Buffer *> bufs;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		bufs.reserve(journals_.size());
		for (auto &kv: journals_) {
			bufs.push_back(kv.first);
		}
	}
	for (Buffer *b: bufs) {
		if (b)
			RecordCheckpoint(*b, false);
	}
}


void
SwapManager::Flush(Buffer *buf)
{
	(void) buf; // stage 1: flushes all buffers
	std::unique_lock<std::mutex> lk(mtx_);
	const std::uint64_t target = next_seq_;
	// Wake the writer in case it's waiting on the interval.
	cv_.notify_one();
	cv_.wait(lk, [&] {
		return queue_.empty() && inflight_ == 0 && last_processed_ >= target;
	});
}


void
SwapManager::BufferRecorder::OnInsert(int row, int col, std::string_view bytes)
{
	m_.RecordInsert(buf_, row, col, bytes);
}


void
SwapManager::BufferRecorder::OnDelete(int row, int col, std::size_t len)
{
	m_.RecordDelete(buf_, row, col, len);
}


SwapRecorder *
SwapManager::RecorderFor(Buffer *buf)
{
	if (!buf)
		return nullptr;
	std::lock_guard<std::mutex> lg(mtx_);
	auto it = recorders_.find(buf);
	if (it != recorders_.end())
		return it->second.get();
	// Create on-demand. Recording calls will no-op until Attach() has been called.
	auto rec          = std::make_unique<BufferRecorder>(*this, *buf);
	SwapRecorder *ptr = rec.get();
	recorders_[buf]   = std::move(rec);
	return ptr;
}


SwapRecorder *
SwapManager::Rehome(Buffer *old_addr, Buffer *new_addr)
{
	if (!old_addr || !new_addr || old_addr == new_addr)
		return nullptr;
	std::lock_guard<std::mutex> lg(mtx_);
	SwapRecorder *result = nullptr;

	auto jit = journals_.find(old_addr);
	if (jit != journals_.end()) {
		JournalCtx ctx = std::move(jit->second);
		journals_.erase(jit);
		journals_[new_addr] = std::move(ctx);
	}

	auto rit = recorders_.find(old_addr);
	if (rit != recorders_.end()) {
		recorders_.erase(rit);
		// BufferRecorder binds a Buffer& at construction, so it can't be
		// repointed in place; rebuild it against the buffer's new address.
		auto rec              = std::make_unique<BufferRecorder>(*this, *new_addr);
		result                = rec.get();
		recorders_[new_addr] = std::move(rec);
	}

	// Defensive: any record still queued (not yet drained by the writer thread)
	// for the old address must follow the buffer to its new location. Callers
	// are expected to Flush() before rehoming so this should normally be a no-op.
	for (auto &p: queue_) {
		if (p.buf == old_addr)
			p.buf = new_addr;
	}

	return result;
}


void
SwapManager::Attach(Buffer *buf)
{
	if (!buf || buf->IsVirtual())
		return; // e.g. +HELP+: nothing to recover, and its name is not a path
	bool fresh = false;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		fresh = journals_.find(buf) == journals_.end();
	}
	const BaseId base = fresh ? compute_base(buf->Filename()) : BaseId{};
	std::lock_guard<std::mutex> lg(mtx_);
	const bool still_fresh = journals_.find(buf) == journals_.end();
	JournalCtx &ctx        = journals_[buf];
	if (ctx.path.empty())
		ctx.path = ComputeSidecarPath(*buf);
	if (fresh && still_fresh)
		apply_base(ctx, base);
	// Ensure a recorder exists as well.
	if (recorders_.find(buf) == recorders_.end()) {
		recorders_[buf] = std::make_unique<BufferRecorder>(*this, *buf);
	}
}


void
SwapManager::Detach(Buffer *buf, const bool remove_file)
{
	if (!buf) {
		return;
	}

	// Write a best-effort final checkpoint before suspending and closing.
	// If the caller requested removal, skip the final checkpoint so the file can be deleted.
	if (!remove_file)
		RecordCheckpoint(*buf, true);
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(buf);
		if (it != journals_.end()) {
			it->second.suspended = true;
		}
	}

	Flush(buf);
	std::string path;
	bool locked_out = false;
	{
		std::lock_guard<std::mutex> io(io_mtx_);
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(buf);
		if (it != journals_.end()) {
			path       = it->second.path;
			locked_out = it->second.locked_out;
			close_ctx(it->second);
			journals_.erase(it);
		}
		recorders_.erase(buf);
	}

	if (remove_file)
		remove_journal_unless_held(path, locked_out);
}


void
SwapManager::ResetJournal(Buffer &buf)
{
	bool was_locked_out = false;
	std::string path;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end())
			return;
		JournalCtx &ctx = it->second;
		if (ctx.path.empty())
			ctx.path = ComputeSidecarPath(buf);
		path          = ctx.path;
		ctx.suspended = true;
	}

	Flush(&buf);
	const BaseId base = compute_base(buf.Filename());

	{
		std::lock_guard<std::mutex> io(io_mtx_);
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end())
			return;
		JournalCtx &ctx = it->second;
		close_ctx(ctx);
		was_locked_out             = ctx.locked_out;
		ctx.header_ok              = false;
		ctx.gap                    = false; // the journal restarts from the saved file
		ctx.gap_notified           = false;
		ctx.gap_chkpt_request_ns   = 0;
		ctx.gap_unfixable_reported = false;
		ctx.locked_out             = false;
		apply_base(ctx, base);
		ctx.last_flush_ns          = 0;
		ctx.last_fsync_ns          = 0;
		ctx.last_chkpt_ns          = 0;
		ctx.edit_bytes_since_chkpt = 0;
		ctx.approx_size_bytes      = 0;
		ctx.suspended              = false;
	}

	remove_journal_unless_held(path, was_locked_out);
}


std::string
SwapManager::SwapDirRoot()
{
	return swap_root_dir().string();
}


void
SwapManager::PruneSwapDir()
{
	SwapConfig cfg;
	std::vector<std::string> active;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		cfg = cfg_;
		active.reserve(journals_.size());
		for (const auto &kv: journals_) {
			if (!kv.second.path.empty())
				active.push_back(kv.second.path);
		}
	}

	const fs::path root = swap_root_dir();
	std::error_code ec;
	if (!fs::exists(root, ec) || ec)
		return;

	struct Entry {
		fs::path path;
		std::filesystem::file_time_type mtime;
	};
	std::vector<Entry> swps;
	for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
		const fs::path p = it->path();
		if (p.extension() != ".swp")
			continue;
		// Never delete active journals.
		const std::string ps = p.string();
		bool is_active       = false;
		for (const auto &a: active) {
			if (a == ps) {
				is_active = true;
				break;
			}
		}
		if (is_active)
			continue;
		std::error_code ec2;
		if (!it->is_regular_file(ec2) || ec2)
			continue;
		auto tm = fs::last_write_time(p, ec2);
		if (ec2)
			continue;
		swps.push_back({p, tm});
	}

	if (swps.empty())
		return;

	// Sort newest first.
	std::sort(swps.begin(), swps.end(), [](const Entry &a, const Entry &b) {
		return a.mtime > b.mtime;
	});

	// Convert age threshold.
	auto now     = std::filesystem::file_time_type::clock::now();
	auto max_age = std::chrono::hours(24) * static_cast<long long>(cfg.prune_max_age_days);

	std::size_t kept = 0;
	for (const auto &e: swps) {
		bool too_old = false;
		if (cfg.prune_max_age_days > 0) {
			// If file_time_type isn't system_clock, duration arithmetic still works.
			if (now - e.mtime > max_age)
				too_old = true;
		}
		bool over_limit = (cfg.prune_max_files > 0) && (kept >= cfg.prune_max_files);
		// Never prune a journal another running session holds.
		if ((too_old || over_limit) && !JournalInUse(e.path.string())) {
			std::error_code ec3;
			fs::remove(e.path, ec3);
		} else {
			++kept;
		}
	}
}


void
SwapManager::NotifyFilenameChanged(Buffer &buf)
{
	// No checkpoint here: the old journal is deleted below, and every caller
	// has just loaded or saved the file, so the new journal's base is the
	// file's content.
	std::string old_path;
	bool attached = false;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it != journals_.end()) {
			attached             = true;
			old_path             = it->second.path;
			it->second.suspended = true;
		}
	}
	if (!attached) {
		// A buffer that was never journaled (a virtual buffer such as +HELP+,
		// now saved under a real name) starts journaling here.
		if (!buf.IsVirtual() && !buf.Filename().empty()) {
			Attach(&buf);
			buf.SetSwapRecorder(RecorderFor(&buf));
		}
		return;
	}
	Flush(&buf);
	const BaseId base = compute_base(buf.Filename());
	std::lock_guard<std::mutex> io(io_mtx_);
	std::lock_guard<std::mutex> lg(mtx_);
	auto it = journals_.find(&buf);
	if (it == journals_.end())
		return;
	JournalCtx &ctx = it->second;
	close_ctx(ctx);
	const std::string new_path = ComputeSidecarPath(buf);
	// When the path is unchanged (Attach already pointed the journal at this
	// file's swap), that file may be the one the caller is about to replay
	// for recovery: removing it here lost the recovery.
	if (!old_path.empty() && old_path != new_path)
		remove_journal_unless_held(old_path, ctx.locked_out);
	ctx.gap                     = false;
	ctx.gap_chkpt_request_ns    = 0;
	ctx.gap_unfixable_reported  = false;
	ctx.gap_notified            = false;
	ctx.locked_out              = false;
	apply_base(ctx, base);
	ctx.path                    = new_path;
	ctx.suspended              = false;
	ctx.header_ok              = false;
	ctx.last_flush_ns          = 0;
	ctx.last_fsync_ns          = 0;
	ctx.last_chkpt_ns          = 0;
	ctx.edit_bytes_since_chkpt = 0;
	ctx.approx_size_bytes      = 0;
}


void
SwapManager::SetSuspended(Buffer &buf, bool on)
{
	std::lock_guard<std::mutex> lg(mtx_);
	auto it = journals_.find(&buf);
	if (it == journals_.end())
		return;
	it->second.suspended = on;
}


SwapManager::SuspendGuard::SuspendGuard(SwapManager &m, Buffer *b)
	: m_(m), buf_(b), prev_(false)
{
	if (!buf_)
		return;
	{
		std::lock_guard<std::mutex> lg(m_.mtx_);
		auto it = m_.journals_.find(buf_);
		if (it != m_.journals_.end()) {
			prev_                = it->second.suspended;
			it->second.suspended = true;
		}
	}
}


SwapManager::SuspendGuard::~SuspendGuard()
{
	if (!buf_)
		return;
	std::lock_guard<std::mutex> lg(m_.mtx_);
	auto it = m_.journals_.find(buf_);
	if (it != m_.journals_.end()) {
		it->second.suspended = prev_;
	}
}


std::string
SwapManager::ComputeSidecarPath(const Buffer &buf)
{
	fs::path root = swap_root_dir();
	if (!buf.Filename().empty()) {
		return compute_swap_path_for_filename(buf.Filename());
	}

	// Unnamed buffers: unique across processes (pid) and within one (counter).
	// A per-process counter alone let a new session's scratch buffer claim,
	// and later delete, another running session's unnamed journal.
	static std::atomic<std::uint64_t> ctr{0};
	const std::uint64_t n  = ++ctr;
	const std::string name = "unnamed-" + std::to_string(static_cast<long>(::getpid())) + "-" +
	                         std::to_string(n) + ".swp";
	return (root / name).string();
}


std::string
SwapManager::ComputeSwapPathForFilename(const std::string &filename)
{
	return ComputeSidecarPathForFilename(filename);
}


std::string
SwapManager::ComputeSidecarPathForFilename(const std::string &filename)
{
	return compute_swap_path_for_filename(filename);
}


std::uint64_t
SwapManager::now_ns()
{
	using namespace std::chrono;
	return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}


bool
SwapManager::ensure_parent_dir(const std::string &path)
{
	try {
		fs::path p(path);
		fs::path dir = p.parent_path();
		if (dir.empty())
			return true;
		if (!fs::exists(dir))
			fs::create_directories(dir);
		return true;
	} catch (...) {
		return false;
	}
}


bool
SwapManager::write_header(int fd, const JournalCtx &ctx)
{
	if (fd < 0)
		return false;
	// Fixed 64-byte header (v1)
	// [magic 8][version u32][flags u32][created_time u64]
	// [base_size u64][base_mtime_ns i64] (valid when flags bit 0 is set)
	// [reserved/padding]
	std::uint8_t hdr[64];
	std::memset(hdr, 0, sizeof(hdr));
	std::memcpy(hdr, MAGIC, 8);
	// version (little-endian)
	hdr[8]  = static_cast<std::uint8_t>(VERSION & 0xFFu);
	hdr[9]  = static_cast<std::uint8_t>((VERSION >> 8) & 0xFFu);
	hdr[10] = static_cast<std::uint8_t>((VERSION >> 16) & 0xFFu);
	hdr[11] = static_cast<std::uint8_t>((VERSION >> 24) & 0xFFu);
	// flags: bit 0 = base file identity present
	// flags: bit 1 = base content CRC-32 present (bytes 40..43)
	if (ctx.has_base) {
		hdr[12] = 1;
		put_le64(hdr + 24, ctx.base_size);
		put_le64(hdr + 32, static_cast<std::uint64_t>(ctx.base_mtime_ns));
		if (ctx.has_base_crc) {
			hdr[12] |= 2;
			hdr[40] = static_cast<std::uint8_t>(ctx.base_crc & 0xFFu);
			hdr[41] = static_cast<std::uint8_t>((ctx.base_crc >> 8) & 0xFFu);
			hdr[42] = static_cast<std::uint8_t>((ctx.base_crc >> 16) & 0xFFu);
			hdr[43] = static_cast<std::uint8_t>((ctx.base_crc >> 24) & 0xFFu);
		}
	}
	// created_time (unix seconds; little-endian)
	std::uint64_t ts = static_cast<std::uint64_t>(std::time(nullptr));
	put_le64(hdr + 16, ts);
	return write_full(fd, hdr, sizeof(hdr));
}


bool
SwapManager::open_ctx(JournalCtx &ctx, const std::string &path, std::string &err, bool *locked_out)
{
	err.clear();
	if (ctx.fd >= 0)
		return true;
	if (!ensure_parent_dir(path)) {
		err = "Failed to create parent directory for swap file: " + path;
		return false;
	}
	int flags = O_CREAT | O_WRONLY | O_APPEND;
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif

	// Retry on transient errors (ENOSPC, EDQUOT, EBUSY, etc.)
	int fd       = -1;
	auto open_fn = [&]() -> bool {
		fd = kte::syscall::Open(path.c_str(), flags, 0600);
		return fd >= 0;
	};

	if (!RetryOnTransientError(open_fn, RetryPolicy::Aggressive(), err)) {
		if (fd < 0) {
			int saved_errno = errno;
			err = "Failed to open swap file '" + path + "': " + std::strerror(saved_errno) + err;
		}
		return false;
	}
	// Take ownership of the journal. A lock held by another kte process means
	// that session is journaling this file: writing here too would interleave
	// its records with ours.
	if (::flock(fd, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK) {
		kte::syscall::Close(fd);
		if (locked_out)
			*locked_out = true; // caller records it under mtx_
		err = "Swap file is in use by another kte process: " + path;
		return false;
	}
	// Ensure permissions even if file already existed.
	(void) kte::syscall::Fchmod(fd, 0600);
	struct stat st{};
	if (kte::syscall::Fstat(fd, &st) != 0) {
		int saved_errno = errno;
		kte::syscall::Close(fd);
		err = "Failed to fstat swap file '" + path + "': " + std::strerror(saved_errno);
		return false;
	}
	// If an existing file is too small to contain the fixed header, truncate
	// and restart.
	if (st.st_size > 0 && st.st_size < 64) {
		kte::syscall::Close(fd);
		int tflags = O_CREAT | O_WRONLY | O_TRUNC | O_APPEND;
#ifdef O_CLOEXEC
		tflags |= O_CLOEXEC;
#endif

		// Retry on transient errors for truncation open
		fd             = -1;
		auto reopen_fn = [&]() -> bool {
			fd = kte::syscall::Open(path.c_str(), tflags, 0600);
			return fd >= 0;
		};

		if (!RetryOnTransientError(reopen_fn, RetryPolicy::Aggressive(), err)) {
			if (fd < 0) {
				int saved_errno = errno;
				err = "Failed to reopen swap file for truncation '" + path + "': " + std::strerror(
					      saved_errno) + err;
			}
			return false;
		}
		if (::flock(fd, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK) {
			kte::syscall::Close(fd);
			if (locked_out)
				*locked_out = true; // caller records it under mtx_
			err = "Swap file is in use by another kte process: " + path;
			return false;
		}
		(void) kte::syscall::Fchmod(fd, 0600);
		st.st_size = 0;
	}
	// (ctx.path is not written here: this runs on the writer thread and the
	// main thread reads ctx.path under mtx_; callers pass ctx.path itself.)
	ctx.fd = fd;
	if (st.st_size == 0) {
		ctx.header_ok         = write_header(fd, ctx);
		ctx.approx_size_bytes = ctx.header_ok ? 64 : 0;
		if (!ctx.header_ok) {
			err = "Failed to write swap file header: " + path;
			// Drop any partial header and close, so the next attempt starts a
			// fresh file instead of appending records behind a torn header.
			(void) kte::syscall::Ftruncate(fd, 0);
			kte::syscall::Close(fd);
			ctx.fd = -1;
		}
	} else {
		ctx.header_ok         = true; // stage 1: trust existing header
		ctx.approx_size_bytes = static_cast<std::uint64_t>(st.st_size);
	}
	return ctx.header_ok;
}


void
SwapManager::close_ctx(JournalCtx &ctx)
{
	if (ctx.fd >= 0) {
		(void) kte::syscall::Fsync(ctx.fd);
		kte::syscall::Close(ctx.fd);
		ctx.fd = -1;
	}
	ctx.header_ok = false;
}


void
SwapManager::frame_pending(const Pending &p, RecordFrames &f)
{
	// One record: [type u8][len u24][prefix][data][crc32 u32], CRC over all
	// but itself.
	auto frame = [&f](SwapRecType type, std::vector<std::uint8_t> prefix, const char *data, std::size_t n) {
		std::vector<std::uint8_t> head(4);
		head[0] = static_cast<std::uint8_t>(type);
		put_u24_le(head.data() + 1, static_cast<std::uint32_t>(prefix.size() + n));
		std::uint32_t c = crc32(head.data(), head.size(), 0);
		if (!prefix.empty())
			c = crc32(prefix.data(), prefix.size(), c);
		if (n > 0)
			c = crc32(reinterpret_cast<const std::uint8_t *>(data), n, c);
		head.insert(head.end(), prefix.begin(), prefix.end());
		f.owned.push_back(std::move(head));
		f.segs.emplace_back(f.owned.back().data(), f.owned.back().size());
		f.total += f.owned.back().size();
		if (n > 0) {
			f.segs.emplace_back(reinterpret_cast<const std::uint8_t *>(data), n);
			f.total += n;
		}
		std::vector<std::uint8_t> crc(4);
		crc[0] = static_cast<std::uint8_t>(c & 0xFFu);
		crc[1] = static_cast<std::uint8_t>((c >> 8) & 0xFFu);
		crc[2] = static_cast<std::uint8_t>((c >> 16) & 0xFFu);
		crc[3] = static_cast<std::uint8_t>((c >> 24) & 0xFFu);
		f.owned.push_back(std::move(crc));
		f.segs.emplace_back(f.owned.back().data(), f.owned.back().size());
		f.total += 4;
	};

	if (p.type != SwapRecType::CHKPT) {
		frame(p.type, p.payload, nullptr, 0);
		return;
	}
	const std::string &bytes = p.chkpt;
	if (bytes.size() <= kMaxRecordPayload - 5) {
		// v1 checkpoint: [encver u8=1][nbytes u32][bytes]
		std::vector<std::uint8_t> prefix{1};
		put_le32(prefix, static_cast<std::uint32_t>(bytes.size()));
		frame(SwapRecType::CHKPT, std::move(prefix), bytes.data(), bytes.size());
		return;
	}
	// Chunked: BEGIN, DATA..., END (see SwapRecType).
	std::vector<std::uint8_t> begin{1};
	std::uint8_t total[8];
	put_le64(total, static_cast<std::uint64_t>(bytes.size()));
	begin.insert(begin.end(), total, total + 8);
	put_le32(begin, crc32(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size(), 0));
	frame(SwapRecType::CHKPT_BEGIN, std::move(begin), nullptr, 0);
	for (std::size_t off = 0; off < bytes.size(); off += kMaxRecordPayload) {
		const std::size_t n = std::min(kMaxRecordPayload, bytes.size() - off);
		frame(SwapRecType::CHKPT_DATA, {}, bytes.data() + off, n);
	}
	frame(SwapRecType::CHKPT_END, std::vector<std::uint8_t>{1}, nullptr, 0);
}


bool
SwapManager::write_frames(int fd, const RecordFrames &f)
{
	for (const auto &[data, n]: f.segs) {
		if (!write_full(fd, data, n))
			return false;
	}
	return true;
}


bool
SwapManager::compact_to_checkpoint(JournalCtx &ctx, const RecordFrames &chkpt_record, std::string &err)
{
	err.clear();
	if (ctx.path.empty()) {
		err = "Compact failed: empty path";
		return false;
	}
	if (chkpt_record.total == 0) {
		err = "Compact failed: empty checkpoint record";
		return false;
	}

	// Keep the current (locked) journal fd open until the compacted file has
	// replaced it, so the journal is never unlocked in between: another
	// session would otherwise see it as abandoned and offer to recover or
	// discard it.
	const std::string tmp_path = ctx.path + ".tmp";
	// Create the compacted file: header + checkpoint record.
	if (!ensure_parent_dir(tmp_path)) {
		err = "Failed to create parent directory for temp swap file: " + tmp_path;
		return false;
	}

	int flags = O_CREAT | O_WRONLY | O_TRUNC;
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif

	// Retry on transient errors for temp file creation
	int tfd          = -1;
	auto open_tmp_fn = [&]() -> bool {
		tfd = kte::syscall::Open(tmp_path.c_str(), flags, 0600);
		return tfd >= 0;
	};

	if (!RetryOnTransientError(open_tmp_fn, RetryPolicy::Aggressive(), err)) {
		if (tfd < 0) {
			int saved_errno = errno;
			err = "Failed to open temp swap file '" + tmp_path + "': " + std::strerror(saved_errno) + err;
		}
		return false;
	}
	(void) kte::syscall::Fchmod(tfd, 0600);
	bool ok = write_header(tfd, ctx);
	if (ok)
		ok = write_frames(tfd, chkpt_record);
	if (ok) {
		if (kte::syscall::Fsync(tfd) != 0) {
			int saved_errno = errno;
			err = "Failed to fsync temp swap file '" + tmp_path + "': " + std::strerror(saved_errno);
			ok = false;
		}
	}
	if (ok)
		(void) ::flock(tfd, LOCK_EX | LOCK_NB); // new file: nobody else has it
	if (!ok) {
		kte::syscall::Close(tfd);
		if (err.empty()) {
			err = "Failed to write temp swap file: " + tmp_path;
		}
		std::remove(tmp_path.c_str());
		return false;
	}

	// Atomic replace.
	if (::rename(tmp_path.c_str(), ctx.path.c_str()) != 0) {
		int saved_errno = errno;
		kte::syscall::Close(tfd);
		err = "Failed to rename temp swap file '" + tmp_path + "' to '" + ctx.path + "': " + std::strerror(
			      saved_errno);
		std::remove(tmp_path.c_str());
		return false;
	}
	// The compacted file is the journal now; retire the old descriptor.
	if (ctx.fd >= 0)
		kte::syscall::Close(ctx.fd);
	const int fl = ::fcntl(tfd, F_GETFL);
	if (fl >= 0)
		(void) ::fcntl(tfd, F_SETFL, fl | O_APPEND);
	ctx.fd        = tfd;
	ctx.header_ok = true;

	// Best-effort: fsync parent dir to persist the rename.
	try {
		fs::path p(ctx.path);
		fs::path dir = p.parent_path();
		if (!dir.empty()) {
			int dflags = O_RDONLY;
#ifdef O_DIRECTORY
			dflags |= O_DIRECTORY;
#endif
			int dfd = kte::syscall::Open(dir.string().c_str(), dflags);
			if (dfd >= 0) {
				(void) kte::syscall::Fsync(dfd);
				kte::syscall::Close(dfd);
			}
		}
	} catch (...) {
		// ignore
	}

	// Re-open for further appends.
	ctx.approx_size_bytes = 64 + static_cast<std::uint64_t>(chkpt_record.total);
	return true;
}


std::uint32_t
SwapManager::crc32(const std::uint8_t *data, std::size_t len, std::uint32_t seed)
{
	// Built once, thread-safely (used by the writer thread and by replay on
	// the main thread; the old lazy init with a plain bool was a data race).
	static const std::array<std::uint32_t, 256> table = [] {
		std::array<std::uint32_t, 256> t{};
		for (std::uint32_t i = 0; i < 256; ++i) {
			std::uint32_t c = i;
			for (int j = 0; j < 8; ++j)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			t[i] = c;
		}
		return t;
	}();
	std::uint32_t c = ~seed;
	for (std::size_t i = 0; i < len; ++i)
		c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
	return ~c;
}


void
SwapManager::put_le32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
	out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
	out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
	out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
	out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}


void
SwapManager::put_le64(std::uint8_t *dst, std::uint64_t v)
{
	dst[0] = static_cast<std::uint8_t>(v & 0xFFu);
	dst[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
	dst[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
	dst[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
	dst[4] = static_cast<std::uint8_t>((v >> 32) & 0xFFu);
	dst[5] = static_cast<std::uint8_t>((v >> 40) & 0xFFu);
	dst[6] = static_cast<std::uint8_t>((v >> 48) & 0xFFu);
	dst[7] = static_cast<std::uint8_t>((v >> 56) & 0xFFu);
}


void
SwapManager::put_u24_le(std::uint8_t dst[3], std::uint32_t v)
{
	dst[0] = static_cast<std::uint8_t>(v & 0xFFu);
	dst[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
	dst[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
}


void
SwapManager::enqueue(Pending &&p)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		p.seq = ++next_seq_;
		queue_.emplace_back(std::move(p));
	}
	cv_.notify_one();
}


void
SwapManager::RecordInsert(Buffer &buf, int row, int col, std::string_view text)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end() || it->second.suspended)
			return;
	}
	// payload v1: [encver u8=1][row u32][col u32][nbytes u32][bytes]
	// A record holds at most kMaxRecordPayload bytes, so large inserts are
	// split into consecutive INS records, each positioned after the last.
	constexpr std::size_t kInsHeader = 13;
	constexpr std::size_t kChunk     = kMaxRecordPayload - kInsHeader;
	std::size_t r                    = static_cast<std::size_t>(std::max(0, row));
	std::size_t c                    = static_cast<std::size_t>(std::max(0, col));
	std::size_t off                  = 0;
	do {
		const std::string_view chunk = text.substr(off, kChunk);
		Pending p;
		p.buf  = &buf;
		p.type = SwapRecType::INS;
		p.payload.reserve(kInsHeader + chunk.size());
		p.payload.push_back(1);
		put_le32(p.payload, static_cast<std::uint32_t>(r));
		put_le32(p.payload, static_cast<std::uint32_t>(c));
		put_le32(p.payload, static_cast<std::uint32_t>(chunk.size()));
		p.payload.insert(p.payload.end(), reinterpret_cast<const std::uint8_t *>(chunk.data()),
		                 reinterpret_cast<const std::uint8_t *>(chunk.data()) + chunk.size());
		enqueue(std::move(p));
		// Advance (r, c) past the chunk for the next record.
		const std::size_t last_nl = chunk.rfind('\n');
		if (last_nl == std::string_view::npos) {
			c += chunk.size();
		} else {
			r += static_cast<std::size_t>(std::count(chunk.begin(), chunk.end(), '\n'));
			c = chunk.size() - last_nl - 1;
		}
		off += chunk.size();
	} while (off < text.size());
	maybe_request_checkpoint(buf, text.size());
}


void
SwapManager::RecordDelete(Buffer &buf, int row, int col, std::size_t len)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end() || it->second.suspended)
			return;
	}
	if (len > 0xFFFFFFFFu)
		return;
	Pending p;
	p.buf  = &buf;
	p.type = SwapRecType::DEL;
	// payload v1: [encver u8=1][row u32][col u32][len u32]
	p.payload.push_back(1);
	put_le32(p.payload, static_cast<std::uint32_t>(std::max(0, row)));
	put_le32(p.payload, static_cast<std::uint32_t>(std::max(0, col)));
	put_le32(p.payload, static_cast<std::uint32_t>(len));
	enqueue(std::move(p));
	maybe_request_checkpoint(buf, len);
}


void
SwapManager::RecordSplit(Buffer &buf, int row, int col)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end() || it->second.suspended)
			return;
	}
	Pending p;
	p.buf  = &buf;
	p.type = SwapRecType::SPLIT;
	// payload v1: [encver u8=1][row u32][col u32]
	p.payload.push_back(1);
	put_le32(p.payload, static_cast<std::uint32_t>(std::max(0, row)));
	put_le32(p.payload, static_cast<std::uint32_t>(std::max(0, col)));
	enqueue(std::move(p));
	maybe_request_checkpoint(buf, 1);
}


void
SwapManager::RecordJoin(Buffer &buf, int row)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end() || it->second.suspended)
			return;
	}
	Pending p;
	p.buf  = &buf;
	p.type = SwapRecType::JOIN;
	// payload v1: [encver u8=1][row u32]
	p.payload.push_back(1);
	put_le32(p.payload, static_cast<std::uint32_t>(std::max(0, row)));
	enqueue(std::move(p));
	maybe_request_checkpoint(buf, 1);
}


void
SwapManager::maybe_request_checkpoint(Buffer &buf, const std::size_t approx_edit_bytes)
{
	SwapConfig cfg;
	bool do_chkpt = false;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		cfg = cfg_;
		auto it = journals_.find(&buf);
		if (it == journals_.end() || it->second.suspended)
			return;
		if (cfg.checkpoint_bytes == 0 && cfg.checkpoint_interval_ms == 0 && !it->second.gap)
			return;
		JournalCtx &ctx            = it->second;
		ctx.edit_bytes_since_chkpt += approx_edit_bytes;
		const std::uint64_t now    = now_ns();
		if (ctx.last_chkpt_ns == 0)
			ctx.last_chkpt_ns = now;
		// A checkpoint writes the whole buffer. For a large one, checkpoint
		// after edits amounting to half its size (and on the timer only
		// once edits reach a sixteenth), so the bytes written stay in
		// proportion to the bytes edited; retry a gap less often too.
		const std::size_t size        = buf.ContentBytes();
		const bool large              = size > kLargeCheckpointBytes;
		const std::size_t bytes_limit = std::max<std::size_t>(cfg.checkpoint_bytes, size / 2);
		const bool bytes_hit          = (cfg.checkpoint_bytes > 0) && ctx.edit_bytes_since_chkpt >= bytes_limit;
		const bool time_hit = (cfg.checkpoint_interval_ms > 0) &&
		                      (((now - ctx.last_chkpt_ns) / 1000000ULL) >= cfg.checkpoint_interval_ms) &&
		                      (!large || ctx.edit_bytes_since_chkpt >= size / 16);
		const std::uint64_t gap_interval = kGapCheckpointIntervalNs *
		                                   (1 + size / kLargeCheckpointBytes);
		const bool gap_hit = ctx.gap && (now - ctx.gap_chkpt_request_ns) >= gap_interval;
		if (gap_hit)
			ctx.gap_chkpt_request_ns = now;
		if (bytes_hit || time_hit || gap_hit) {
			ctx.edit_bytes_since_chkpt = 0;
			ctx.last_chkpt_ns          = now;
			do_chkpt                   = true;
		}
	}
	if (do_chkpt) {
		RecordCheckpoint(buf, false);
	}
}


void
SwapManager::RetryGapCheckpoints()
{
	std::vector<Buffer *> due;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		const std::uint64_t now = now_ns();
		for (auto &[b, ctx]: journals_) {
			if (!b || !ctx.gap || ctx.suspended || ctx.locked_out || ctx.gap_unfixable_reported)
				continue;
			const std::uint64_t interval = kGapCheckpointIntervalNs *
			                               (1 + b->ContentBytes() / kLargeCheckpointBytes);
			if (now - ctx.gap_chkpt_request_ns < interval)
				continue;
			ctx.gap_chkpt_request_ns = now;
			due.push_back(b);
		}
	}
	for (Buffer *b: due)
		RecordCheckpoint(*b, false);
}


std::string
SwapManager::TakeUserNotice()
{
	std::lock_guard<std::mutex> lg(mtx_);
	if (user_notices_.empty())
		return {};
	std::string msg = std::move(user_notices_.front());
	user_notices_.pop_front();
	return msg;
}


void
SwapManager::notify_user_locked_(std::string msg)
{
	if (user_notices_.size() < 8)
		user_notices_.push_back(std::move(msg));
}


void
SwapManager::RecordCheckpoint(Buffer &buf, const bool urgent_flush)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end() || it->second.suspended)
			return;
	}

	// Any size: the writer frames large checkpoints as several records.
	Pending p;
	p.buf          = &buf;
	p.type         = SwapRecType::CHKPT;
	p.urgent_flush = urgent_flush;
	p.chkpt        = snapshot_buffer_bytes(buf);
	enqueue(std::move(p));
}


void
SwapManager::writer_loop()
{
	for (;;) {
		std::vector<Pending> batch;
		{
			std::unique_lock<std::mutex> lk(mtx_);
			if (queue_.empty()) {
				if (!running_.load())
					break;
				cv_.wait_for(lk, std::chrono::milliseconds(cfg_.flush_interval_ms));
			}
			if (!queue_.empty()) {
				batch.swap(queue_);
				inflight_ += batch.size();
			}
		}
		if (batch.empty())
			continue;

		for (const Pending &p: batch) {
			try {
				process_one(p);
			} catch (const std::exception &e) {
				report_error(std::string("Exception in process_one: ") + e.what(), p.buf);
			} catch (...) {
				report_error("Unknown exception in process_one", p.buf);
			}
			{
				std::lock_guard<std::mutex> lg(mtx_);
				if (p.seq > last_processed_)
					last_processed_ = p.seq;
				if (inflight_ > 0)
					--inflight_;
			}
			cv_.notify_all();
		}

		// Throttled fsync: best-effort (grouped)
		try {
			std::uint64_t now = now_ns();
			// Hold io_mtx_ across the fsyncs: journal fds are only closed
			// under it (by the main thread), so none of these can be closed or
			// reused mid-fsync. Unsynchronised, an in-flight fsync also kept a
			// just-closed journal's flock alive, so the owner's own cleanup saw
			// its journal as held. mtx_ is held only to collect the fds, so
			// edits are not blocked while fsync runs.
			std::lock_guard<std::mutex> io(io_mtx_);
			std::vector<int> to_sync;
			{
				std::lock_guard<std::mutex> lg(mtx_);
				for (auto &kv: journals_) {
					JournalCtx &ctx = kv.second;
					if (ctx.fd >= 0) {
						if (ctx.last_fsync_ns == 0 || (now - ctx.last_fsync_ns) / 1000000ULL >=
						    cfg_.fsync_interval_ms) {
							ctx.last_fsync_ns = now;
							to_sync.push_back(ctx.fd);
						}
					}
				}
			}
			for (int fd: to_sync)
				(void) kte::syscall::Fsync(fd);
		} catch (const std::exception &e) {
			report_error(std::string("Exception in fsync operations: ") + e.what());
		} catch (...) {
			report_error("Unknown exception in fsync operations");
		}
	}
	// Wake any waiters.
	cv_.notify_all();
}


void
SwapManager::process_one(const Pending &p)
{
	if (!p.buf)
		return;

	// Any record that is not written leaves a gap in the journal.
	auto mark_gap = [&]() {
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(p.buf);
		if (it != journals_.end()) {
			it->second.gap = true;
			if (!it->second.gap_notified) {
				it->second.gap_notified = true;
				const std::string &jp   = it->second.path;
				notify_user_locked_("Crash-recovery journal write failed" +
				                    (jp.empty() ? std::string() : " (" + jp + ")") +
				                    "; recent edits may not be recoverable");
			}
		}
	};

	// Check circuit breaker before processing
	bool circuit_open = false;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		if (!circuit_breaker_.AllowRequest()) {
			circuit_open = true;
		}
	}

	if (circuit_open) {
		// Circuit is open - graceful degradation: skip swap write
		// This prevents repeated failures from overwhelming the system
		// Swap recording will resume when circuit closes
		static std::atomic<std::uint64_t> last_warning_ns{0};
		const std::uint64_t now  = now_ns();
		const std::uint64_t last = last_warning_ns.load();
		// Log warning at most once per 60 seconds to avoid spam
		if (now - last > 60000000000ULL) {
			last_warning_ns.store(now);
			// Journal path read under mtx_, not Buffer::Filename(), which the
			// main thread may be assigning (see report_error).
			std::string context = "<unnamed>";
			{
				std::lock_guard<std::mutex> lg(mtx_);
				auto it = journals_.find(p.buf);
				if (it != journals_.end() && !it->second.path.empty())
					context = it->second.path;
			}
			ErrorHandler::Instance().Warning("SwapManager",
			                                 "Swap operations temporarily disabled due to repeated failures (circuit breaker open)",
			                                 context);
		}
		mark_gap();
		return;
	}

	try {
		Buffer &buf = *p.buf;

		JournalCtx *ctxp = nullptr;
		std::string path;
		std::size_t compact_bytes = 0;
		{
			std::lock_guard<std::mutex> lg(mtx_);
			auto it = journals_.find(p.buf);
			if (it == journals_.end())
				return;
			// Another process owns this journal (reported once when found).
			if (it->second.locked_out)
				return;
			// After a gap only a checkpoint can bring the journal back in step.
			if (it->second.gap && p.type != SwapRecType::CHKPT)
				return;
			if (it->second.path.empty())
				it->second.path = ComputeSidecarPath(buf);
			path          = it->second.path;
			ctxp          = &it->second;
			compact_bytes = cfg_.compact_bytes;
		}
		if (!ctxp)
			return;
		if (ctxp->fd < 0)
			complete_base_crc(*ctxp);
		std::string open_err;
		bool locked_out = false;
		if (!open_ctx(*ctxp, path, open_err, &locked_out)) {
			if (locked_out) {
				{
					std::lock_guard<std::mutex> lg(mtx_);
					ctxp->locked_out = true;
					if (!ctxp->lockout_notified) {
						ctxp->lockout_notified = true;
						notify_user_locked_(
							"File is being edited in another kte; crash recovery is off here (" +
							path + ")");
					}
				}
				report_error(open_err, p.buf);
				return; // not an I/O failure; later records are skipped
			}
			report_error(open_err, p.buf);
			{
				std::lock_guard<std::mutex> lg(mtx_);
				circuit_breaker_.RecordFailure();
			}
			mark_gap();
			return;
		}
		{
			// Holding the journal now; a later lock-out is news again.
			std::lock_guard<std::mutex> lg(mtx_);
			ctxp->lockout_notified = false;
		}
		if (p.payload.size() > kMaxRecordPayload) {
			// Recorders keep payloads within the limit, so this is a bug, not an
			// I/O failure: do not count it against the circuit breaker.
			report_error("Payload too large: " + std::to_string(p.payload.size()) + " bytes", p.buf);
			mark_gap();
			return;
		}

		RecordFrames rec;
		frame_pending(p, rec);

		// A checkpoint that would trigger compaction goes straight into the
		// compacted journal instead of being appended and then copied there.
		// For a large buffer the thresholds scale with its size, so the cost
		// of compacting stays proportional to what was journaled since.
		const std::uint64_t compact_at = std::max<std::uint64_t>(compact_bytes, 2 * p.chkpt.size());
		if (p.type == SwapRecType::CHKPT && compact_bytes > 0 &&
		    ctxp->approx_size_bytes + rec.total >= compact_at) {
			std::string compact_err;
			if (compact_to_checkpoint(*ctxp, rec, compact_err)) {
				std::lock_guard<std::mutex> lg(mtx_);
				ctxp->gap          = false;
				ctxp->gap_notified = false;
				circuit_breaker_.RecordSuccess();
				return;
			}
			report_error(compact_err, p.buf); // fall back to appending
		}

		// Remember where this record starts so a partial write can be undone;
		// otherwise later records would be appended behind the torn bytes.
		struct stat before{};
		const bool have_size = kte::syscall::Fstat(ctxp->fd, &before) == 0;

		// Write (handle partial writes and check results)
		bool ok = write_frames(ctxp->fd, rec);
		if (!ok) {
			int err = errno;
			report_error("Failed to write swap record to '" + path + "': " + std::strerror(err), p.buf);
			if (have_size)
				(void) kte::syscall::Ftruncate(ctxp->fd, before.st_size);
			{
				std::lock_guard<std::mutex> lg(mtx_);
				circuit_breaker_.RecordFailure();
			}
			mark_gap();
			return;
		}
		if (p.type == SwapRecType::CHKPT) {
			std::lock_guard<std::mutex> lg(mtx_);
			ctxp->gap          = false;
			ctxp->gap_notified = false;
		}
		ctxp->approx_size_bytes += static_cast<std::uint64_t>(rec.total);
		if (p.urgent_flush) {
			if (kte::syscall::Fsync(ctxp->fd) != 0) {
				int err = errno;
				report_error("Failed to fsync swap file '" + path + "': " + std::strerror(err), p.buf);
			}
			ctxp->last_fsync_ns = now_ns();
		}

		// Record success for circuit breaker
		{
			std::lock_guard<std::mutex> lg(mtx_);
			circuit_breaker_.RecordSuccess();
		}
	} catch (const std::exception &e) {
		report_error(std::string("Exception in process_one: ") + e.what(), p.buf);
		{
			std::lock_guard<std::mutex> lg(mtx_);
			circuit_breaker_.RecordFailure();
		}
		mark_gap();
	} catch (...) {
		report_error("Unknown exception in process_one", p.buf);
		{
			std::lock_guard<std::mutex> lg(mtx_);
			circuit_breaker_.RecordFailure();
		}
		mark_gap();
	}
}


static bool
read_exact(std::ifstream &in, void *dst, std::size_t n)
{
	in.read(static_cast<char *>(dst), static_cast<std::streamsize>(n));
	return in.good() && static_cast<std::size_t>(in.gcount()) == n;
}


static std::uint32_t
read_le32(const std::uint8_t b[4])
{
	return (std::uint32_t) b[0] | ((std::uint32_t) b[1] << 8) | ((std::uint32_t) b[2] << 16) | (
		       (std::uint32_t) b[3] << 24);
}


static bool
parse_u32_le(const std::vector<std::uint8_t> &p, std::size_t &off, std::uint32_t &out)
{
	if (off + 4 > p.size())
		return false;
	out = (std::uint32_t) p[off] | ((std::uint32_t) p[off + 1] << 8) | ((std::uint32_t) p[off + 2] << 16) |
	      ((std::uint32_t) p[off + 3] << 24);
	off += 4;
	return true;
}


// Remove a journal this session is done with, unless another session holds
// it: a session that was locked out of the journal (or finds it locked) must
// not unlink the live journal of the session that owns it.
static void
remove_journal_unless_held(const std::string &path, bool locked_out)
{
	if (path.empty() || locked_out || SwapManager::JournalInUse(path))
		return;
	(void) std::remove(path.c_str());
}


bool
SwapManager::JournalInUse(const std::string &swap_path)
{
	int flags = O_RDONLY;
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
	const int fd = kte::syscall::Open(swap_path.c_str(), flags);
	if (fd < 0)
		return false;
	const bool in_use = ::flock(fd, LOCK_SH | LOCK_NB) != 0 && errno == EWOULDBLOCK;
	kte::syscall::Close(fd); // also releases our shared lock, if taken
	return in_use;
}


bool
SwapManager::JournalMatchesFile(const std::string &swap_path, const std::string &file_path)
{
	std::ifstream in(swap_path, std::ios::binary);
	std::uint8_t hdr[64];
	if (!in || !in.read(reinterpret_cast<char *>(hdr), sizeof(hdr)))
		return true; // unreadable: left to ReplayFile to report
	if (std::memcmp(hdr, MAGIC, 8) != 0 || (hdr[12] & 1u) == 0)
		return true; // no base identity recorded
	std::uint64_t want_size = 0, want_mtime = 0;
	for (int i = 7; i >= 0; --i) {
		want_size  = (want_size << 8) | hdr[24 + i];
		want_mtime = (want_mtime << 8) | hdr[32 + i];
	}
	std::uint64_t size = 0;
	std::int64_t mtime = 0;
	if (!stat_base(file_path, size, mtime))
		return false; // the file the journal was based on is gone
	if (size != want_size)
		return false;
	if (static_cast<std::uint64_t>(mtime) == want_mtime)
		return true;
	// Timestamp changed (touch, checkout back and forth, sync tools): the
	// content may still be the same, which is what matters.
	if ((hdr[12] & 2u) == 0)
		return false;
	const std::uint32_t want_crc = static_cast<std::uint32_t>(hdr[40]) | (static_cast<std::uint32_t>(hdr[41]) << 8) |
	                               (static_cast<std::uint32_t>(hdr[42]) << 16) |
	                               (static_cast<std::uint32_t>(hdr[43]) << 24);
	std::uint32_t crc = 0;
	return file_crc32(file_path, crc) && crc == want_crc;
}


bool
SwapManager::file_crc32(const std::string &path, std::uint32_t &out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	std::uint32_t c = 0;
	std::uint8_t chunk[1 << 16];
	while (in) {
		in.read(reinterpret_cast<char *>(chunk), sizeof(chunk));
		const auto n = in.gcount();
		if (n > 0)
			c = crc32(chunk, static_cast<std::size_t>(n), c);
	}
	if (in.bad())
		return false;
	out = c;
	return true;
}


#if defined(KTE_TESTS)
void
SwapManager::SetSyncCrcLimitForTests(std::uint64_t bytes)
{
	g_sync_crc_limit = bytes;
}
#endif


void
SwapManager::complete_base_crc(JournalCtx &ctx)
{
	std::string file;
	std::uint64_t size = 0;
	std::int64_t mtime = 0;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		if (!ctx.has_base || ctx.has_base_crc || ctx.base_crc_file.empty())
			return;
		file  = ctx.base_crc_file;
		size  = ctx.base_size;
		mtime = ctx.base_mtime_ns;
	}
	// Off the lock: this reads the whole file. Keep the CRC only if the file
	// is still the one whose size and mtime the journal records.
	std::uint32_t crc   = 0;
	std::uint64_t size2 = 0;
	std::int64_t mtime2 = 0;
	const bool ok = file_crc32(file, crc) && stat_base(file, size2, mtime2) && size2 == size && mtime2 == mtime;
	std::lock_guard<std::mutex> lg(mtx_);
	if (ctx.base_crc_file != file || ctx.base_size != size || ctx.base_mtime_ns != mtime)
		return; // the base changed meanwhile (save, rename)
	ctx.base_crc_file.clear();
	if (ok) {
		ctx.has_base_crc = true;
		ctx.base_crc     = crc;
	}
}


SwapManager::BaseId
SwapManager::compute_base(const std::string &file)
{
	// Reading a very large file again on every open and save is too slow
	// for the main thread: its CRC is left to the writer thread, which
	// computes it before creating the journal (see process_one).
	const std::uint64_t kMaxCrcBytes = g_sync_crc_limit;
	BaseId id;
	id.has     = stat_base(file, id.size, id.mtime_ns);
	id.has_crc = id.has && id.size <= kMaxCrcBytes && file_crc32(file, id.crc);
	if (id.has && !id.has_crc && id.size > kMaxCrcBytes)
		id.crc_file = file;
	return id;
}


void
SwapManager::apply_base(JournalCtx &ctx, const BaseId &id)
{
	ctx.has_base      = id.has;
	ctx.base_size     = id.size;
	ctx.base_mtime_ns = id.mtime_ns;
	ctx.has_base_crc  = id.has_crc;
	ctx.base_crc      = id.crc;
	ctx.base_crc_file = id.crc_file;
}


bool
SwapManager::ReplayFile(Buffer &buf, const std::string &swap_path, std::string &err,
                        std::uint64_t *valid_bytes)
{
	err.clear();
	std::ifstream in(swap_path, std::ios::binary);
	if (!in) {
		err = "Failed to open swap file for replay: " + swap_path;
		return false;
	}

	std::uint8_t hdr[64];
	if (!read_exact(in, hdr, sizeof(hdr))) {
		err = "Swap file truncated (header): " + swap_path;
		return false;
	}
	if (std::memcmp(hdr, MAGIC, 8) != 0) {
		err = "Swap file has bad magic: " + swap_path;
		return false;
	}
	const std::uint32_t ver = read_le32(hdr + 8);
	if (ver != 1 && ver != VERSION) {
		err = "Unsupported swap version: " + std::to_string(ver);
		return false;
	}

	// Ensure replayed edits don't get re-journaled if the caller forgot to detach/suspend.
	kte::SwapRecorder *prev_rec = buf.SwapRecorder();
	buf.SetSwapRecorder(nullptr);
	struct RestoreSwapRecorder {
		Buffer &b;
		kte::SwapRecorder *prev;


		~RestoreSwapRecorder()
		{
			b.SetSwapRecorder(prev);
		}
	} restore{buf, prev_rec};

	// A chunked checkpoint being assembled (see SwapRecType::CHKPT_BEGIN).
	bool in_chkpt            = false;
	std::uint64_t chkpt_size = 0;
	std::uint32_t chkpt_crc  = 0;
	std::string chkpt;

	for (;;) {
		// Everything before this offset replayed cleanly. Inside a chunked
		// checkpoint, that is where the checkpoint began (it only counts
		// once complete).
		const std::streamoff record_start = in.tellg();
		if (valid_bytes && record_start >= 0 && !in_chkpt)
			*valid_bytes = static_cast<std::uint64_t>(record_start);
		std::uint8_t head[4];
		in.read(reinterpret_cast<char *>(head), sizeof(head));
		const std::size_t got_head = static_cast<std::size_t>(in.gcount());
		if (got_head == 0 && in.eof()) {
			if (in_chkpt) {
				err = "Swap file ends inside a checkpoint; recovered the records before it: " + swap_path;
				kte::ErrorHandler::Instance().Warning("SwapManager", err, swap_path);
			}
			return true; // clean EOF
		}
		// A record cut short by end-of-file is the expected result of a crash or
		// power loss mid-append. Keep everything replayed so far and stop; the
		// caller still sees err (with a true result) and can report it.
		auto torn_tail = [&](const char *what) {
			if (in.bad()) {
				err = std::string("Failed to read swap file (") + what + "): " + swap_path;
				return false;
			}
			err = std::string("Swap file ends with an incomplete record (") + what + "); recovered the records before it: " +
			      swap_path;
			kte::ErrorHandler::Instance().Warning("SwapManager", err, swap_path);
			return true;
		};
		if (got_head != sizeof(head))
			return torn_tail("record header");

		const SwapRecType type = static_cast<SwapRecType>(head[0]);
		const std::size_t len  = (std::size_t) head[1] | ((std::size_t) head[2] << 8) | (
			                         (std::size_t) head[3] << 16);
		std::vector<std::uint8_t> payload;
		payload.resize(len);
		if (len > 0 && !read_exact(in, payload.data(), len))
			return torn_tail("payload");
		std::uint8_t crcbytes[4];
		if (!read_exact(in, crcbytes, sizeof(crcbytes)))
			return torn_tail("crc");
		const std::uint32_t want_crc = read_le32(crcbytes);
		std::uint32_t got_crc        = 0;
		got_crc                      = crc32(head, sizeof(head), got_crc);
		if (!payload.empty())
			got_crc = crc32(payload.data(), payload.size(), got_crc);
		if (got_crc != want_crc) {
			err = "Swap file CRC mismatch: " + swap_path;
			return false;
		}

		// Chunked checkpoint records; nothing else may come between them.
		if (in_chkpt && type != SwapRecType::CHKPT_DATA && type != SwapRecType::CHKPT_END) {
			err = "Swap file record inside a checkpoint: " + swap_path;
			return false;
		}

		// Apply record
		switch (type) {
		case SwapRecType::CHKPT_BEGIN: {
			// [encver u8=1][total u64][crc32 u32]
			if (payload.size() < 13 || payload[0] != 1) {
				err = "Malformed CHKPT_BEGIN payload";
				return false;
			}
			chkpt_size = 0;
			for (int i = 7; i >= 0; --i)
				chkpt_size = (chkpt_size << 8) | payload[1 + static_cast<std::size_t>(i)];
			chkpt_crc = read_le32(payload.data() + 9);
			chkpt.clear();
			try {
				chkpt.reserve(static_cast<std::size_t>(chkpt_size));
			} catch (...) {
				err = "Checkpoint too large to replay: " + std::to_string(chkpt_size) + " bytes";
				return false;
			}
			in_chkpt = true;
			break;
		}
		case SwapRecType::CHKPT_DATA:
			if (!in_chkpt || chkpt.size() + payload.size() > chkpt_size) {
				err = "Swap file checkpoint data out of place: " + swap_path;
				return false;
			}
			chkpt.append(reinterpret_cast<const char *>(payload.data()), payload.size());
			break;
		case SwapRecType::CHKPT_END: {
			if (!in_chkpt || chkpt.size() != chkpt_size ||
			    crc32(reinterpret_cast<const std::uint8_t *>(chkpt.data()), chkpt.size(), 0) != chkpt_crc) {
				err = "Swap file checkpoint incomplete or corrupt: " + swap_path;
				return false;
			}
			buf.replace_all_bytes(chkpt);
			std::string().swap(chkpt);
			in_chkpt = false;
			break;
		}
		case SwapRecType::INS: {
			std::size_t off = 0;
			// INS payload: encver(1) + row(4) + col(4) + nbytes(4) + data(nbytes)
			// Minimum: 1 + 4 + 4 + 4 = 13 bytes
			if (payload.size() < 13) {
				err = "INS payload too short (need at least 13 bytes)";
				return false;
			}
			const std::uint8_t encver = payload[off++];
			if (encver != 1) {
				err = "Unsupported swap payload encoding";
				return false;
			}
			std::uint32_t row = 0, col = 0, nbytes = 0;
			if (!parse_u32_le(payload, off, row) || !parse_u32_le(payload, off, col) || !parse_u32_le(
				    payload, off, nbytes)) {
				err = "Malformed INS payload (failed to parse row/col/nbytes)";
				return false;
			}
			if (off + nbytes > payload.size()) {
				err = "Truncated INS payload bytes";
				return false;
			}
			buf.insert_text((int) row, (int) col,
			                std::string_view(reinterpret_cast<const char *>(payload.data() + off), nbytes));
			break;
		}
		case SwapRecType::DEL: {
			std::size_t off = 0;
			// DEL payload: encver(1) + row(4) + col(4) + dlen(4)
			// Minimum: 1 + 4 + 4 + 4 = 13 bytes
			if (payload.size() < 13) {
				err = "DEL payload too short (need at least 13 bytes)";
				return false;
			}
			const std::uint8_t encver = payload[off++];
			if (encver != 1) {
				err = "Unsupported swap payload encoding";
				return false;
			}
			std::uint32_t row = 0, col = 0, dlen = 0;
			if (!parse_u32_le(payload, off, row) || !parse_u32_le(payload, off, col) || !parse_u32_le(
				    payload, off, dlen)) {
				err = "Malformed DEL payload (failed to parse row/col/dlen)";
				return false;
			}
			buf.delete_text((int) row, (int) col, (std::size_t) dlen);
			break;
		}
		case SwapRecType::SPLIT: {
			std::size_t off = 0;
			// SPLIT payload: encver(1) + row(4) + col(4)
			// Minimum: 1 + 4 + 4 = 9 bytes
			if (payload.size() < 9) {
				err = "SPLIT payload too short (need at least 9 bytes)";
				return false;
			}
			const std::uint8_t encver = payload[off++];
			if (encver != 1) {
				err = "Unsupported swap payload encoding";
				return false;
			}
			std::uint32_t row = 0, col = 0;
			if (!parse_u32_le(payload, off, row) || !parse_u32_le(payload, off, col)) {
				err = "Malformed SPLIT payload (failed to parse row/col)";
				return false;
			}
			buf.split_line((int) row, (int) col);
			break;
		}
		case SwapRecType::JOIN: {
			std::size_t off = 0;
			// JOIN payload: encver(1) + row(4)
			// Minimum: 1 + 4 = 5 bytes
			if (payload.size() < 5) {
				err = "JOIN payload too short (need at least 5 bytes)";
				return false;
			}
			const std::uint8_t encver = payload[off++];
			if (encver != 1) {
				err = "Unsupported swap payload encoding";
				return false;
			}
			std::uint32_t row = 0;
			if (!parse_u32_le(payload, off, row)) {
				err = "Malformed JOIN payload (failed to parse row)";
				return false;
			}
			buf.join_lines((int) row);
			break;
		}
		case SwapRecType::CHKPT: {
			std::size_t off = 0;
			// CHKPT payload: encver(1) + nbytes(4) + data(nbytes)
			// Minimum: 1 + 4 = 5 bytes
			if (payload.size() < 5) {
				err = "CHKPT payload too short (need at least 5 bytes)";
				return false;
			}
			const std::uint8_t encver = payload[off++];
			if (encver != 1) {
				err = "Unsupported swap checkpoint encoding";
				return false;
			}
			std::uint32_t nbytes = 0;
			if (!parse_u32_le(payload, off, nbytes)) {
				err = "Malformed CHKPT payload (failed to parse nbytes)";
				return false;
			}
			if (off + nbytes > payload.size()) {
				err = "Truncated CHKPT payload bytes";
				return false;
			}
			buf.replace_all_bytes(std::string_view(reinterpret_cast<const char *>(payload.data() + off),
			                                       (std::size_t) nbytes));
			break;
		}
		default:
			// Ignore unknown types for forward-compat
			break;
		}
	}
}


void
SwapManager::report_error(const std::string &message, Buffer *buf)
{
	// Identify the buffer by its journal path, read under mtx_. This runs on
	// the writer thread, where reading Buffer::Filename() raced with the main
	// thread assigning it (SaveAs, OpenFromFile).
	std::string context = "<unknown>";
	{
		std::lock_guard<std::mutex> lg(mtx_);
		if (buf) {
			auto it = journals_.find(buf);
			context = (it != journals_.end() && !it->second.path.empty()) ? it->second.path : "<unnamed>";
		}
	}

	// Report to centralized error handler
	ErrorHandler::Instance().Error("SwapManager", message, context);

	// Maintain local error tracking for backward compatibility
	std::lock_guard<std::mutex> lg(mtx_);
	SwapError err;
	err.timestamp_ns = now_ns();
	err.message      = message;
	err.buffer_name  = context;
	errors_.push_back(err);
	// Bound the error queue to 100 entries
	while (errors_.size() > 100) {
		errors_.pop_front();
	}
	++total_error_count_;
}


bool
SwapManager::HasErrors() const
{
	std::lock_guard<std::mutex> lg(mtx_);
	return !errors_.empty();
}


std::string
SwapManager::GetLastError() const
{
	std::lock_guard<std::mutex> lg(mtx_);
	if (errors_.empty())
		return "";
	const SwapError &e = errors_.back();
	return "[" + e.buffer_name + "] " + e.message;
}


std::size_t
SwapManager::GetErrorCount() const
{
	std::lock_guard<std::mutex> lg(mtx_);
	return total_error_count_;
}
} // namespace kte