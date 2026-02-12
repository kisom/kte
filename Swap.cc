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
}


SwapManager::SwapManager()
{
	running_.store(true);
	worker_ = std::thread([this] {
		this->writer_loop();
	});
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
SwapManager::Detach(Buffer *buf)
{
	if (!buf)
		return;
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(buf);
		if (it != journals_.end()) {
			it->second.suspended = true;
		}
	}
	Flush(buf);
	std::lock_guard<std::mutex> lg(mtx_);
	auto it = journals_.find(buf);
	if (it != journals_.end()) {
		close_ctx(it->second);
		journals_.erase(it);
	}
	recorders_.erase(buf);
}


void
SwapManager::NotifyFilenameChanged(Buffer &buf)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(&buf);
		if (it == journals_.end())
			return;
		it->second.suspended = true;
	}
	Flush(&buf);
	std::lock_guard<std::mutex> lg(mtx_);
	auto it = journals_.find(&buf);
	if (it == journals_.end())
		return;
	JournalCtx &ctx = it->second;
	close_ctx(ctx);
	ctx.path      = ComputeSidecarPath(buf);
	ctx.suspended = false;
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
	// Always place swap under an XDG home-appropriate state directory.
	// This avoids cluttering working directories and prevents stomping on
	// swap files when multiple different paths share the same basename.
	fs::path root = xdg_state_home() / "kte" / "swap";

	auto encode_path = [](std::string s) -> std::string {
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
	};

	if (!buf.Filename().empty()) {
		fs::path p(buf.Filename());
		std::string key;
		try {
			key = fs::weakly_canonical(p).string();
		} catch (...) {
			try {
				key = fs::absolute(p).string();
			} catch (...) {
				key = buf.Filename();
			}
		}
		std::string encoded = encode_path(key);
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

	// Unnamed buffers: unique within the process.
	static std::atomic<std::uint64_t> ctr{0};
	const std::uint64_t n  = ++ctr;
	const int pid          = (int) ::getpid();
	const std::string name = "unnamed-" + std::to_string(pid) + "-" + std::to_string(n) + ".swp";
	return (root / name).string();
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
		ctx.header_ok = write_header(fd);
	} else {
		ctx.header_ok = true; // stage 1: trust existing header
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
	{
		std::lock_guard<std::mutex> lg(mtx_);
		auto it = journals_.find(p.buf);
		if (it == journals_.end())
			return;
		if (it->second.suspended)
			return;
		if (it->second.path.empty())
			it->second.path = ComputeSidecarPath(buf);
		path = it->second.path;
		ctxp = &it->second;
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

	// Write (handle partial writes and check results)
	bool ok = write_full(ctxp->fd, head, sizeof(head));
	if (ok && !p.payload.empty())
		ok = write_full(ctxp->fd, p.payload.data(), p.payload.size());
	if (ok)
		ok = write_full(ctxp->fd, crcbytes, sizeof(crcbytes));
	(void) ok; // stage 1: best-effort; future work could mark ctx error state
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
		std::size_t off = 0;
		if (payload.empty()) {
			err = "Swap record missing payload";
			return false;
		}
		const std::uint8_t encver = payload[off++];
		if (encver != 1) {
			err = "Unsupported swap payload encoding";
			return false;
		}
		switch (type) {
		case SwapRecType::INS: {
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
			std::uint32_t row = 0, col = 0;
			if (!parse_u32_le(payload, off, row) || !parse_u32_le(payload, off, col)) {
				err = "Malformed SPLIT payload";
				return false;
			}
			buf.split_line((int) row, (int) col);
			break;
		}
		case SwapRecType::JOIN: {
			std::uint32_t row = 0;
			if (!parse_u32_le(payload, off, row)) {
				err = "Malformed JOIN payload";
				return false;
			}
			buf.join_lines((int) row);
			break;
		}
		default:
			// Ignore unknown types for forward-compat in stage 1
			break;
		}
	}
}
} // namespace kte