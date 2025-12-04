#include "Swap.h"
#include "Buffer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace kte {
namespace {
constexpr std::uint8_t MAGIC[8] = {'K', 'T', 'E', '_', 'S', 'W', 'P', '\0'};
constexpr std::uint32_t VERSION = 1;
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
SwapManager::Attach(Buffer * /*buf*/)
{
	// Stage 1: lazy-open on first record; nothing to do here.
}


void
SwapManager::Detach(Buffer * /*buf*/)
{
	// Stage 1: keep files open until manager destruction; future work can close per-buffer.
}


void
SwapManager::NotifyFilenameChanged(Buffer &buf)
{
	std::lock_guard<std::mutex> lg(mtx_);
	auto it = journals_.find(&buf);
	if (it == journals_.end())
		return;
	JournalCtx &ctx = it->second;
	// Close existing file handle, update path; lazily reopen on next write
	close_ctx(ctx);
	ctx.path = ComputeSidecarPath(buf);
}


void
SwapManager::SetSuspended(Buffer &buf, bool on)
{
	std::lock_guard<std::mutex> lg(mtx_);
	auto path = ComputeSidecarPath(buf);
	// Create/update context for this buffer
	JournalCtx &ctx = journals_[&buf];
	ctx.path        = path;
	ctx.suspended   = on;
}


SwapManager::SuspendGuard::SuspendGuard(SwapManager &m, Buffer *b)
	: m_(m), buf_(b), prev_(false)
{
	// Suspend recording while guard is alive
	if (buf_)
		m_.SetSuspended(*buf_, true);
}


SwapManager::SuspendGuard::~SuspendGuard()
{
	if (buf_)
		m_.SetSuspended(*buf_, false);
}


std::string
SwapManager::ComputeSidecarPath(const Buffer &buf)
{
	if (buf.IsFileBacked() || !buf.Filename().empty()) {
		fs::path p(buf.Filename());
		fs::path dir     = p.parent_path();
		std::string base = p.filename().string();
		std::string side = "." + base + ".kte.swp";
		return (dir / side).string();
	}
	// unnamed: $TMPDIR/kte/unnamed-<ptr>.kte.swp (best-effort)
	const char *tmp = std::getenv("TMPDIR");
	fs::path t      = tmp ? fs::path(tmp) : fs::temp_directory_path();
	fs::path d      = t / "kte";
	char bufptr[32];
	std::snprintf(bufptr, sizeof(bufptr), "%p", (const void *) &buf);
	return (d / (std::string("unnamed-") + bufptr + ".kte.swp")).string();
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
SwapManager::write_header(JournalCtx &ctx)
{
	if (ctx.fd < 0)
		return false;
	// Write a simple 64-byte header
	std::uint8_t hdr[64];
	std::memset(hdr, 0, sizeof(hdr));
	std::memcpy(hdr, MAGIC, 8);
	std::uint32_t ver = VERSION;
	std::memcpy(hdr + 8, &ver, sizeof(ver));
	std::uint64_t ts = static_cast<std::uint64_t>(std::time(nullptr));
	std::memcpy(hdr + 16, &ts, sizeof(ts));
	ssize_t w = ::write(ctx.fd, hdr, sizeof(hdr));
	return (w == (ssize_t) sizeof(hdr));
}


bool
SwapManager::open_ctx(JournalCtx &ctx)
{
	if (ctx.fd >= 0)
		return true;
	if (!ensure_parent_dir(ctx.path))
		return false;
	// Create or open with 0600 perms
	int fd = ::open(ctx.path.c_str(), O_CREAT | O_RDWR, 0600);
	if (fd < 0)
		return false;
	// Detect if file is new/empty to write header
	struct stat st{};
	if (fstat(fd, &st) != 0) {
		::close(fd);
		return false;
	}
	ctx.fd   = fd;
	ctx.file = fdopen(fd, "ab");
	if (!ctx.file) {
		::close(fd);
		ctx.fd = -1;
		return false;
	}
	if (st.st_size == 0) {
		ctx.header_ok = write_header(ctx);
	} else {
		ctx.header_ok = true; // trust existing file for stage 1
		// Seek to end to append
		::lseek(ctx.fd, 0, SEEK_END);
	}
	return ctx.header_ok;
}


void
SwapManager::close_ctx(JournalCtx &ctx)
{
	if (ctx.file) {
		std::fflush((FILE *) ctx.file);
		::fsync(ctx.fd);
		std::fclose((FILE *) ctx.file);
		ctx.file = nullptr;
	}
	if (ctx.fd >= 0) {
		::close(ctx.fd);
		ctx.fd = -1;
	}
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
				c  = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[i] = c;
		}
		inited = true;
	}
	std::uint32_t c = ~seed;
	for (std::size_t i = 0; i < len; ++i)
		c          = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
	return ~c;
}


void
SwapManager::put_varu64(std::vector<std::uint8_t> &out, std::uint64_t v)
{
	while (v >= 0x80) {
		out.push_back(static_cast<std::uint8_t>(v) | 0x80);
		v >>= 7;
	}
	out.push_back(static_cast<std::uint8_t>(v));
}


void
SwapManager::put_u24(std::uint8_t dst[3], std::uint32_t v)
{
	dst[0] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
	dst[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
	dst[2] = static_cast<std::uint8_t>(v & 0xFF);
}


void
SwapManager::enqueue(Pending &&p)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		queue_.emplace_back(std::move(p));
	}
	cv_.notify_one();
}


void
SwapManager::RecordInsert(Buffer &buf, int row, int col, std::string_view text)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		if (journals_[&buf].suspended)
			return;
	}
	Pending p;
	p.buf  = &buf;
	p.type = SwapRecType::INS;
	// payload: varint row, varint col, varint len, bytes
	put_varu64(p.payload, static_cast<std::uint64_t>(std::max(0, row)));
	put_varu64(p.payload, static_cast<std::uint64_t>(std::max(0, col)));
	put_varu64(p.payload, static_cast<std::uint64_t>(text.size()));
	p.payload.insert(p.payload.end(), reinterpret_cast<const std::uint8_t *>(text.data()),
	                 reinterpret_cast<const std::uint8_t *>(text.data()) + text.size());
	enqueue(std::move(p));
}


void
SwapManager::RecordDelete(Buffer &buf, int row, int col, std::size_t len)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		if (journals_[&buf].suspended)
			return;
	}
	Pending p;
	p.buf  = &buf;
	p.type = SwapRecType::DEL;
	put_varu64(p.payload, static_cast<std::uint64_t>(std::max(0, row)));
	put_varu64(p.payload, static_cast<std::uint64_t>(std::max(0, col)));
	put_varu64(p.payload, static_cast<std::uint64_t>(len));
	enqueue(std::move(p));
}


void
SwapManager::RecordSplit(Buffer &buf, int row, int col)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		if (journals_[&buf].suspended)
			return;
	}
	Pending p;
	p.buf  = &buf;
	p.type = SwapRecType::SPLIT;
	put_varu64(p.payload, static_cast<std::uint64_t>(std::max(0, row)));
	put_varu64(p.payload, static_cast<std::uint64_t>(std::max(0, col)));
	enqueue(std::move(p));
}


void
SwapManager::RecordJoin(Buffer &buf, int row)
{
	{
		std::lock_guard<std::mutex> lg(mtx_);
		if (journals_[&buf].suspended)
			return;
	}
	Pending p;
	p.buf  = &buf;
	p.type = SwapRecType::JOIN;
	put_varu64(p.payload, static_cast<std::uint64_t>(std::max(0, row)));
	enqueue(std::move(p));
}


void
SwapManager::writer_loop()
{
	while (running_.load()) {
		std::vector<Pending> batch;
		{
			std::unique_lock<std::mutex> lk(mtx_);
			if (queue_.empty()) {
				cv_.wait_for(lk, std::chrono::milliseconds(cfg_.flush_interval_ms));
			}
			if (!queue_.empty()) {
				batch.swap(queue_);
			}
		}
		if (batch.empty())
			continue;

		// Group by buffer path to minimize fsyncs
		for (const Pending &p: batch) {
			process_one(p);
		}

		// Throttled fsync: best-effort
		// Iterate unique contexts and fsync if needed
		// For stage 1, fsync all once per interval
		std::uint64_t now = now_ns();
		for (auto &kv: journals_) {
			JournalCtx &ctx = kv.second;
			if (ctx.fd >= 0) {
				if (ctx.last_fsync_ns == 0 || (now - ctx.last_fsync_ns) / 1000000ULL >= cfg_.
				    fsync_interval_ms) {
					::fsync(ctx.fd);
					ctx.last_fsync_ns = now;
				}
			}
		}
	}
}


void
SwapManager::process_one(const Pending &p)
{
	Buffer &buf = *p.buf;
	// Resolve context by path derived from buffer
	std::string path = ComputeSidecarPath(buf);
	// Get or create context keyed by this buffer pointer (stage 1 simplification)
	JournalCtx &ctx = journals_[p.buf];
	if (ctx.path.empty())
		ctx.path = path;
	if (!open_ctx(ctx))
		return;

	// Build record: [type u8][len u24][payload][crc32 u32]
	std::uint8_t len3[3];
	put_u24(len3, static_cast<std::uint32_t>(p.payload.size()));

	std::uint8_t head[4];
	head[0] = static_cast<std::uint8_t>(p.type);
	head[1] = len3[0];
	head[2] = len3[1];
	head[3] = len3[2];

	std::uint32_t c = 0;
	c               = crc32(head, sizeof(head), c);
	if (!p.payload.empty())
		c = crc32(p.payload.data(), p.payload.size(), c);

	// Write
	(void) ::write(ctx.fd, head, sizeof(head));
	if (!p.payload.empty())
		(void) ::write(ctx.fd, p.payload.data(), p.payload.size());
	(void) ::write(ctx.fd, &c, sizeof(c));
}
} // namespace kte