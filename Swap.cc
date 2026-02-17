#include "Swap.h"
#include "Buffer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>

namespace fs = std::filesystem;

namespace kte {
namespace {
constexpr std::uint8_t MAGIC[8] = {'K', 'T', 'E', '_', 'S', 'W', 'P', '\0'};
constexpr std::uint32_t VERSION = 1;


static std::string
snapshot_buffer_bytes(const Buffer &b)
{
	const auto &rows = b.Rows();
	std::string out;
	// Cheap lower bound: sum of row sizes.
	std::size_t approx = 0;
	for (const auto &r: rows)
		approx += r.size();
	out.reserve(approx);
	for (std::size_t i = 0; i < rows.size(); i++) {
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
	if (!s.empty() && (s[0] == '/' || s[0] == '\\'))
		s.erase(0, 1);
	for (char &ch: s) {
		if (ch == '/' || ch == '\\')
			ch = '!';
	}
	return s;
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
	std::string base       = p.filename().string();
	const std::string name = base + "." + hex_u64(fnv1a64(key)) + ".swp";
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


void
SwapManager::Attach(Buffer *buf)
{
	if (!buf)
		return;
	std::lock_guard<std::mutex> lg(mtx_);
	JournalCtx &ctx = journals_[buf];
	if (ctx.path.empty())
		ctx.path = ComputeSidecarPath(*buf);
	// Ensure a recorder exists as well.
	if (recorders_.find(buf) == recorders_.end()) {
		recorders_[buf] = std::make_unique<BufferRecorder>(*this, *buf);
	}
}


void
SwapManager::Detach(Buffer *buf, const bool remove_file)
{
	if (!buf)
		return;
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
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(buf);
		if (it != journals_.end()) {
			path = it->second.path;
			close_ctx(it->second);
			journals_.erase(it);
		}
		recorders_.erase(buf);
	}
	if (remove_file && !path.empty()) {
		(void) std::remove(path.c_str());
	}
}


void
SwapManager::ResetJournal(Buffer &buf)
{
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

	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end())
			return;
		JournalCtx &ctx = it->second;
		close_ctx(ctx);
		ctx.header_ok              = false;
		ctx.last_flush_ns          = 0;
		ctx.last_fsync_ns          = 0;
		ctx.last_chkpt_ns          = 0;
		ctx.edit_bytes_since_chkpt = 0;
		ctx.approx_size_bytes      = 0;
		ctx.suspended              = false;
	}

	if (!path.empty()) {
		(void) std::remove(path.c_str());
	}
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
		if (too_old || over_limit) {
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
	// Best-effort: checkpoint the old journal before switching paths.
	RecordCheckpoint(buf, true);
	std::string old_path;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end())
			return;
		old_path             = it->second.path;
		it->second.suspended = true;
	}
	Flush(&buf);
	std::lock_guard<std::mutex> lg(mtx_);
	auto it = journals_.find(&buf);
	if (it == journals_.end())
		return;
	JournalCtx &ctx = it->second;
	close_ctx(ctx);
	if (!old_path.empty())
		(void) std::remove(old_path.c_str());
	ctx.path                   = ComputeSidecarPath(buf);
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

	// Unnamed buffers: unique within the process.
	static std::atomic<std::uint64_t> ctr{0};
	const std::uint64_t n  = ++ctr;
	const int pid          = (int) ::getpid();
	const std::string name = "unnamed-" + std::to_string(pid) + "-" + std::to_string(n) + ".swp";
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
SwapManager::write_header(int fd)
{
	if (fd < 0)
		return false;
	// Fixed 64-byte header (v1)
	// [magic 8][version u32][flags u32][created_time u64][reserved/padding]
	std::uint8_t hdr[64];
	std::memset(hdr, 0, sizeof(hdr));
	std::memcpy(hdr, MAGIC, 8);
	// version (little-endian)
	hdr[8]  = static_cast<std::uint8_t>(VERSION & 0xFFu);
	hdr[9]  = static_cast<std::uint8_t>((VERSION >> 8) & 0xFFu);
	hdr[10] = static_cast<std::uint8_t>((VERSION >> 16) & 0xFFu);
	hdr[11] = static_cast<std::uint8_t>((VERSION >> 24) & 0xFFu);
	// flags = 0
	// created_time (unix seconds; little-endian)
	std::uint64_t ts = static_cast<std::uint64_t>(std::time(nullptr));
	put_le64(hdr + 16, ts);
	return write_full(fd, hdr, sizeof(hdr));
}


bool
SwapManager::open_ctx(JournalCtx &ctx, const std::string &path)
{
	if (ctx.fd >= 0)
		return true;
	if (!ensure_parent_dir(path))
		return false;
	int flags = O_CREAT | O_WRONLY | O_APPEND;
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
	int fd = ::open(path.c_str(), flags, 0600);
	if (fd < 0)
		return false;
	// Ensure permissions even if file already existed.
	(void) ::fchmod(fd, 0600);
	struct stat st{};
	if (fstat(fd, &st) != 0) {
		::close(fd);
		return false;
	}
	// If an existing file is too small to contain the fixed header, truncate
	// and restart.
	if (st.st_size > 0 && st.st_size < 64) {
		::close(fd);
		int tflags = O_CREAT | O_WRONLY | O_TRUNC | O_APPEND;
#ifdef O_CLOEXEC
		tflags |= O_CLOEXEC;
#endif
		fd = ::open(path.c_str(), tflags, 0600);
		if (fd < 0)
			return false;
		(void) ::fchmod(fd, 0600);
		st.st_size = 0;
	}
	ctx.fd   = fd;
	ctx.path = path;
	if (st.st_size == 0) {
		ctx.header_ok         = write_header(fd);
		ctx.approx_size_bytes = ctx.header_ok ? 64 : 0;
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
		(void) ::fsync(ctx.fd);
		::close(ctx.fd);
		ctx.fd = -1;
	}
	ctx.header_ok = false;
}


bool
SwapManager::compact_to_checkpoint(JournalCtx &ctx, const std::vector<std::uint8_t> &chkpt_record)
{
	if (ctx.path.empty())
		return false;
	if (chkpt_record.empty())
		return false;

	// Close existing file before rename.
	if (ctx.fd >= 0) {
		(void) ::fsync(ctx.fd);
		::close(ctx.fd);
		ctx.fd = -1;
	}
	ctx.header_ok = false;

	const std::string tmp_path = ctx.path + ".tmp";
	// Create the compacted file: header + checkpoint record.
	if (!ensure_parent_dir(tmp_path))
		return false;

	int flags = O_CREAT | O_WRONLY | O_TRUNC;
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
	int tfd = ::open(tmp_path.c_str(), flags, 0600);
	if (tfd < 0)
		return false;
	(void) ::fchmod(tfd, 0600);
	bool ok = write_header(tfd);
	if (ok)
		ok = write_full(tfd, chkpt_record.data(), chkpt_record.size());
	if (ok)
		ok = (::fsync(tfd) == 0);
	::close(tfd);
	if (!ok) {
		std::remove(tmp_path.c_str());
		return false;
	}

	// Atomic replace.
	if (::rename(tmp_path.c_str(), ctx.path.c_str()) != 0) {
		std::remove(tmp_path.c_str());
		return false;
	}

	// Best-effort: fsync parent dir to persist the rename.
	try {
		fs::path p(ctx.path);
		fs::path dir = p.parent_path();
		if (!dir.empty()) {
			int dflags = O_RDONLY;
#ifdef O_DIRECTORY
			dflags |= O_DIRECTORY;
#endif
			int dfd = ::open(dir.string().c_str(), dflags);
			if (dfd >= 0) {
				(void) ::fsync(dfd);
				::close(dfd);
			}
		}
	} catch (...) {
		// ignore
	}

	// Re-open for further appends.
	if (!open_ctx(ctx, ctx.path))
		return false;
	ctx.approx_size_bytes = 64 + static_cast<std::uint64_t>(chkpt_record.size());
	return true;
}


std::uint32_t
SwapManager::crc32(const std::uint8_t *data, std::size_t len, std::uint32_t seed)
{
	static std::uint32_t table[256];
	static bool inited = false;
	if (!inited) {
		for (std::uint32_t i = 0; i < 256; ++i) {
			std::uint32_t c = i;
			for (int j = 0; j < 8; ++j)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[i] = c;
		}
		inited = true;
	}
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
	Pending p;
	p.buf  = &buf;
	p.type = SwapRecType::INS;
	// payload v1: [encver u8=1][row u32][col u32][nbytes u32][bytes]
	if (text.size() > 0xFFFFFFFFu)
		return;
	p.payload.push_back(1);
	put_le32(p.payload, static_cast<std::uint32_t>(std::max(0, row)));
	put_le32(p.payload, static_cast<std::uint32_t>(std::max(0, col)));
	put_le32(p.payload, static_cast<std::uint32_t>(text.size()));
	p.payload.insert(p.payload.end(), reinterpret_cast<const std::uint8_t *>(text.data()),
	                 reinterpret_cast<const std::uint8_t *>(text.data()) + text.size());
	enqueue(std::move(p));
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
		if (cfg.checkpoint_bytes == 0 && cfg.checkpoint_interval_ms == 0)
			return;
		auto it = journals_.find(&buf);
		if (it == journals_.end() || it->second.suspended)
			return;
		JournalCtx &ctx            = it->second;
		ctx.edit_bytes_since_chkpt += approx_edit_bytes;
		const std::uint64_t now    = now_ns();
		if (ctx.last_chkpt_ns == 0)
			ctx.last_chkpt_ns = now;
		const bool bytes_hit = (cfg.checkpoint_bytes > 0) && (
			                       ctx.edit_bytes_since_chkpt >= cfg.checkpoint_bytes);
		const bool time_hit = (cfg.checkpoint_interval_ms > 0) &&
		                      (((now - ctx.last_chkpt_ns) / 1000000ULL) >= cfg.checkpoint_interval_ms);
		if (bytes_hit || time_hit) {
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
SwapManager::RecordCheckpoint(Buffer &buf, const bool urgent_flush)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end() || it->second.suspended)
			return;
	}

	const std::string bytes = snapshot_buffer_bytes(buf);
	if (bytes.size() > 0xFFFFFFFFu)
		return;

	Pending p;
	p.buf          = &buf;
	p.type         = SwapRecType::CHKPT;
	p.urgent_flush = urgent_flush;
	// payload v1: [encver u8=1][nbytes u32][bytes]
	p.payload.push_back(1);
	put_le32(p.payload, static_cast<std::uint32_t>(bytes.size()));
	p.payload.insert(p.payload.end(), reinterpret_cast<const std::uint8_t *>(bytes.data()),
	                 reinterpret_cast<const std::uint8_t *>(bytes.data()) + bytes.size());
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
			process_one(p);
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
		std::vector<int> to_sync;
		std::uint64_t now = now_ns();
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
		for (int fd: to_sync) {
			(void) ::fsync(fd);
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
	Buffer &buf = *p.buf;

	JournalCtx *ctxp = nullptr;
	std::string path;
	std::size_t compact_bytes = 0;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(p.buf);
		if (it == journals_.end())
			return;
		if (it->second.path.empty())
			it->second.path = ComputeSidecarPath(buf);
		path          = it->second.path;
		ctxp          = &it->second;
		compact_bytes = cfg_.compact_bytes;
	}
	if (!ctxp)
		return;
	if (!open_ctx(*ctxp, path))
		return;
	if (p.payload.size() > 0xFFFFFFu)
		return;

	// Build record: [type u8][len u24][payload][crc32 u32]
	std::uint8_t len3[3];
	put_u24_le(len3, static_cast<std::uint32_t>(p.payload.size()));

	std::uint8_t head[4];
	head[0] = static_cast<std::uint8_t>(p.type);
	head[1] = len3[0];
	head[2] = len3[1];
	head[3] = len3[2];

	std::uint32_t c = 0;
	c               = crc32(head, sizeof(head), c);
	if (!p.payload.empty())
		c = crc32(p.payload.data(), p.payload.size(), c);
	std::uint8_t crcbytes[4];
	crcbytes[0] = static_cast<std::uint8_t>(c & 0xFFu);
	crcbytes[1] = static_cast<std::uint8_t>((c >> 8) & 0xFFu);
	crcbytes[2] = static_cast<std::uint8_t>((c >> 16) & 0xFFu);
	crcbytes[3] = static_cast<std::uint8_t>((c >> 24) & 0xFFu);

	std::vector<std::uint8_t> rec;
	rec.reserve(sizeof(head) + p.payload.size() + sizeof(crcbytes));
	rec.insert(rec.end(), head, head + sizeof(head));
	if (!p.payload.empty())
		rec.insert(rec.end(), p.payload.begin(), p.payload.end());
	rec.insert(rec.end(), crcbytes, crcbytes + sizeof(crcbytes));

	// Write (handle partial writes and check results)
	bool ok = write_full(ctxp->fd, rec.data(), rec.size());
	if (ok) {
		ctxp->approx_size_bytes += static_cast<std::uint64_t>(rec.size());
		if (p.urgent_flush) {
			(void) ::fsync(ctxp->fd);
			ctxp->last_fsync_ns = now_ns();
		}
		if (p.type == SwapRecType::CHKPT && compact_bytes > 0 &&
		    ctxp->approx_size_bytes >= static_cast<std::uint64_t>(compact_bytes)) {
			(void) compact_to_checkpoint(*ctxp, rec);
		}
	}
	(void) ok; // best-effort; future work could mark ctx error state
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


bool
SwapManager::ReplayFile(Buffer &buf, const std::string &swap_path, std::string &err)
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
	if (ver != VERSION) {
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

	for (;;) {
		std::uint8_t head[4];
		in.read(reinterpret_cast<char *>(head), sizeof(head));
		const std::size_t got_head = static_cast<std::size_t>(in.gcount());
		if (got_head == 0 && in.eof()) {
			return true; // clean EOF
		}
		if (got_head != sizeof(head)) {
			err = "Swap file truncated (record header): " + swap_path;
			return false;
		}

		const SwapRecType type = static_cast<SwapRecType>(head[0]);
		const std::size_t len  = (std::size_t) head[1] | ((std::size_t) head[2] << 8) | (
			                         (std::size_t) head[3] << 16);
		std::vector<std::uint8_t> payload;
		payload.resize(len);
		if (len > 0 && !read_exact(in, payload.data(), len)) {
			err = "Swap file truncated (payload): " + swap_path;
			return false;
		}
		std::uint8_t crcbytes[4];
		if (!read_exact(in, crcbytes, sizeof(crcbytes))) {
			err = "Swap file truncated (crc): " + swap_path;
			return false;
		}
		const std::uint32_t want_crc = read_le32(crcbytes);
		std::uint32_t got_crc        = 0;
		got_crc                      = crc32(head, sizeof(head), got_crc);
		if (!payload.empty())
			got_crc = crc32(payload.data(), payload.size(), got_crc);
		if (got_crc != want_crc) {
			err = "Swap file CRC mismatch: " + swap_path;
			return false;
		}

		// Apply record
		switch (type) {
		case SwapRecType::INS: {
			std::size_t off = 0;
			if (payload.empty()) {
				err = "Swap record missing INS payload";
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
				err = "Malformed INS payload";
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
			if (payload.empty()) {
				err = "Swap record missing DEL payload";
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
				err = "Malformed DEL payload";
				return false;
			}
			buf.delete_text((int) row, (int) col, (std::size_t) dlen);
			break;
		}
		case SwapRecType::SPLIT: {
			std::size_t off = 0;
			if (payload.empty()) {
				err = "Swap record missing SPLIT payload";
				return false;
			}
			const std::uint8_t encver = payload[off++];
			if (encver != 1) {
				err = "Unsupported swap payload encoding";
				return false;
			}
			std::uint32_t row = 0, col = 0;
			if (!parse_u32_le(payload, off, row) || !parse_u32_le(payload, off, col)) {
				err = "Malformed SPLIT payload";
				return false;
			}
			buf.split_line((int) row, (int) col);
			break;
		}
		case SwapRecType::JOIN: {
			std::size_t off = 0;
			if (payload.empty()) {
				err = "Swap record missing JOIN payload";
				return false;
			}
			const std::uint8_t encver = payload[off++];
			if (encver != 1) {
				err = "Unsupported swap payload encoding";
				return false;
			}
			std::uint32_t row = 0;
			if (!parse_u32_le(payload, off, row)) {
				err = "Malformed JOIN payload";
				return false;
			}
			buf.join_lines((int) row);
			break;
		}
		case SwapRecType::CHKPT: {
			std::size_t off = 0;
			if (payload.size() < 5) {
				err = "Malformed CHKPT payload";
				return false;
			}
			const std::uint8_t encver = payload[off++];
			if (encver != 1) {
				err = "Unsupported swap checkpoint encoding";
				return false;
			}
			std::uint32_t nbytes = 0;
			if (!parse_u32_le(payload, off, nbytes)) {
				err = "Malformed CHKPT payload";
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
} // namespace kte
