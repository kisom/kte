#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <limits>
#include <cerrno>
#include <cstring>
#include <string_view>

#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "Buffer.h"
#include "SwapRecorder.h"
#include "UndoSystem.h"
#include "UndoTree.h"
#include "ErrorHandler.h"
#include "SyscallWrappers.h"
#include "ErrorRecovery.h"
// For reconstructing highlighter state on copies
#include "syntax/HighlighterRegistry.h"
#include "syntax/NullHighlighter.h"


Buffer::Buffer()
{
	// Initialize undo system per buffer
	undo_tree_ = std::make_unique<UndoTree>();
	undo_sys_  = std::make_unique<UndoSystem>(*this, *undo_tree_);
}


bool
Buffer::stat_identity(const std::string &path, FileIdentity &out)
{
	struct stat st{};
	if (::stat(path.c_str(), &st) != 0) {
		out.valid = false;
		return false;
	}
	out.valid = true;
	// Use nanosecond timestamp when available.
	std::uint64_t ns = 0;
#if defined(__APPLE__)
	ns = static_cast<std::uint64_t>(st.st_mtimespec.tv_sec) * 1000000000ull
	     + static_cast<std::uint64_t>(st.st_mtimespec.tv_nsec);
#else
	ns = static_cast<std::uint64_t>(st.st_mtim.tv_sec) * 1000000000ull
	     + static_cast<std::uint64_t>(st.st_mtim.tv_nsec);
#endif
	out.mtime_ns = ns;
	out.size     = static_cast<std::uint64_t>(st.st_size);
	out.dev      = static_cast<std::uint64_t>(st.st_dev);
	out.ino      = static_cast<std::uint64_t>(st.st_ino);
	return true;
}


bool
Buffer::current_disk_identity(FileIdentity &out) const
{
	if (!is_file_backed_ || filename_.empty()) {
		out.valid = false;
		return false;
	}
	return stat_identity(filename_, out);
}


bool
Buffer::ExternallyModifiedOnDisk() const
{
	if (!is_file_backed_ || filename_.empty())
		return false;
	FileIdentity now{};
	if (!current_disk_identity(now)) {
		// If the file vanished, treat as modified when we previously had an identity.
		return on_disk_identity_.valid;
	}
	if (!on_disk_identity_.valid)
		return false;
	return now.mtime_ns != on_disk_identity_.mtime_ns
	       || now.size != on_disk_identity_.size
	       || now.dev != on_disk_identity_.dev
	       || now.ino != on_disk_identity_.ino;
}


void
Buffer::RefreshOnDiskIdentity()
{
	FileIdentity id{};
	if (current_disk_identity(id))
		on_disk_identity_ = id;
}


static bool
write_all_fd(int fd, const char *data, std::size_t len, std::string &err)
{
	std::size_t off = 0;
	while (off < len) {
		ssize_t n = ::write(fd, data + off, len - off);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			err = std::string("Write failed: ") + std::strerror(errno);
			return false;
		}
		off += static_cast<std::size_t>(n);
	}
	return true;
}


static void
best_effort_fsync_dir(const std::string &path)
{
	try {
		std::filesystem::path p(path);
		std::filesystem::path dir = p.parent_path();
		if (dir.empty())
			return;
		int dfd = kte::syscall::Open(dir.c_str(), O_RDONLY);
		if (dfd < 0)
			return;
		(void) kte::syscall::Fsync(dfd);
		(void) kte::syscall::Close(dfd);
	} catch (...) {
		// best-effort
	}
}


// The process umask, read once during static initialisation (before any
// thread exists): reading it later needs umask(0) + umask(old), which briefly
// changes it for the swap writer thread creating files concurrently.
static const mode_t g_process_umask = [] {
	const mode_t m = ::umask(0);
	(void) ::umask(m);
	return m;
}();


// Rewrite `path` in place (truncate, write, fsync). Used for files with
// several hard links, which a temp-file-and-rename save would split.
static bool
write_in_place(const std::string &path, const char *data, std::size_t len, std::string &err)
{
	int flags = O_WRONLY | O_TRUNC;
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
	const int fd = kte::syscall::Open(path.c_str(), flags);
	if (fd < 0) {
		err = std::string("Failed to open file for writing: ") + std::strerror(errno);
		return false;
	}
	bool ok = write_all_fd(fd, data, len, err);
	if (ok && kte::syscall::Fsync(fd) != 0) {
		err = std::string("fsync failed: ") + std::strerror(errno);
		ok  = false;
	}
	(void) kte::syscall::Close(fd);
	return ok;
}


// Create a new file named from `tmpl` (a mkstemp template, which receives
// the chosen name) holding the content, fsynced.
static bool
write_new_file(std::string &tmpl, const char *data, std::size_t len, std::string &err, int &create_errno)
{
	create_errno = 0;
	std::vector<char> name(tmpl.begin(), tmpl.end());
	name.push_back('\0');
	const int fd = kte::syscall::Mkstemp(name.data());
	if (fd < 0) {
		create_errno = errno;
		err = "Failed to create " + tmpl + ": " + std::strerror(create_errno);
		return false;
	}
	tmpl.assign(name.data());
	const std::string &path = tmpl;
	bool ok = write_all_fd(fd, data, len, err);
	if (ok && kte::syscall::Fsync(fd) != 0) {
		err = std::string("fsync failed: ") + std::strerror(errno);
		ok  = false;
	}
	(void) kte::syscall::Close(fd);
	if (!ok)
		(void) ::unlink(path.c_str());
	return ok;
}


// Whether a failure to create a file beside the target means only that the
// directory will not take a new name (not writable, name too long), so the
// target itself may still be rewritten in place. Anything else (ENOSPC,
// EDQUOT, EIO, ...) would most likely hit the in-place write too, after it
// had already truncated the file.
static bool
may_write_in_place_after(int create_errno)
{
	return create_errno == EACCES || create_errno == EPERM || create_errno == ENAMETOOLONG;
}


static bool
atomic_write_file(const std::string &path_in, const char *data, std::size_t len, std::string &err)
{
	// Write through a symlink to its target: renaming over the link itself
	// would replace the link with a regular file.
	std::string path = path_in;
	try {
		if (std::filesystem::is_symlink(path_in))
			path = std::filesystem::weakly_canonical(path_in).string();
	} catch (...) {
		// Fall back to the given path.
	}

	struct stat dst_st{};
	const bool dst_exists = ::stat(path.c_str(), &dst_st) == 0;
	// Never replace a directory, device node, FIFO or socket with a file.
	if (dst_exists && !S_ISREG(dst_st.st_mode)) {
		err = "Refusing to overwrite a non-regular file: " + path;
		return false;
	}

	// A file with other hard links must be rewritten in place, or those
	// names would keep the old content.
	if (dst_exists && S_ISREG(dst_st.st_mode) && dst_st.st_nlink > 1) {
		// Truncating in place risks the only copy if the write then fails
		// (ENOSPC, EIO, crash), so first write and sync a copy beside it. If
		// the directory will not take a new file (not writable, name too
		// long), write in place anyway rather than refuse to save; any
		// other failure (out of space, I/O error) refuses the save.
		std::string copy = path + ".kte-save.XXXXXX";
		std::string copy_err;
		int copy_errno       = 0;
		const bool have_copy = write_new_file(copy, data, len, copy_err, copy_errno);
		if (!have_copy && !may_write_in_place_after(copy_errno)) {
			err = "Save failed, file left unchanged: " + copy_err;
			return false;
		}
		if (!write_in_place(path, data, len, err)) {
			if (have_copy)
				err += " (the new content is in " + copy + ")";
			return false;
		}
		if (have_copy)
			(void) ::unlink(copy.c_str());
		best_effort_fsync_dir(path);
		return true;
	}

	// Create a temp file in the same directory so rename() is atomic.
	std::filesystem::path p(path);
	std::filesystem::path dir  = p.parent_path();
	std::string base           = p.filename().string();
	std::filesystem::path tmpl = dir / ("." + base + ".kte.tmp.XXXXXX");
	std::string tmpl_s         = tmpl.string();

	// mkstemp requires a mutable buffer.
	std::vector<char> buf(tmpl_s.begin(), tmpl_s.end());
	buf.push_back('\0');

	// Retry on transient errors for temp file creation
	int fd          = -1;
	auto mkstemp_fn = [&]() -> bool {
		// Reset buffer for each retry attempt
		buf.assign(tmpl_s.begin(), tmpl_s.end());
		buf.push_back('\0');
		fd = kte::syscall::Mkstemp(buf.data());
		return fd >= 0;
	};

	if (!kte::RetryOnTransientError(mkstemp_fn, kte::RetryPolicy::Aggressive(), err)) {
		const int saved_errno = errno;
		// No temp file in the directory because the directory is not
		// writable (the file is): an existing regular file can still be
		// written in place, as editors traditionally do. Not on ENOSPC and
		// the like: the in-place write would truncate the file and then fail.
		if (fd < 0 && dst_exists && S_ISREG(dst_st.st_mode) && may_write_in_place_after(saved_errno)) {
			err.clear();
			return write_in_place(path, data, len, err);
		}
		if (fd < 0) {
			err = std::string("Failed to create temp file for save: ") + std::strerror(saved_errno) + err;
		}
		return false;
	}
	std::string tmp_path(buf.data());

	if (dst_exists) {
		// Carry over ownership (best effort; needs privilege to give a file
		// away) and then permissions: chown clears setuid/setgid, so the
		// mode has to be applied after it.
		if (::fchown(fd, dst_st.st_uid, dst_st.st_gid) != 0) {
			// Expected without privilege when the owner differs; keep ours.
		}
		(void) kte::syscall::Fchmod(fd, dst_st.st_mode & 07777);
	} else {
		// mkstemp creates 0600; a new file gets the usual 0666 & ~umask.
		(void) kte::syscall::Fchmod(fd, 0666 & ~g_process_umask);
	}

	bool ok = write_all_fd(fd, data, len, err);
	// Never retry fsync: after a writeback error Linux may mark the pages
	// clean, so a second fsync can succeed although the data was lost, and
	// the rename below would then replace a good file with a bad one.
	if (ok && kte::syscall::Fsync(fd) != 0) {
		err = std::string("fsync failed: ") + std::strerror(errno);
		ok  = false;
	}
	(void) kte::syscall::Close(fd);

	if (ok) {
		if (::rename(tmp_path.c_str(), path.c_str()) != 0) {
			err = std::string("rename failed: ") + std::strerror(errno);
			ok  = false;
		}
	}

	if (!ok) {
		(void) ::unlink(tmp_path.c_str());
		return false;
	}
	best_effort_fsync_dir(path);
	return true;
}


Buffer::Buffer(const std::string &path)
{
	std::string err;
	OpenFromFile(path, err);
}


// Copy constructor/assignment: perform a deep copy of core fields; reinitialize undo for the new buffer.
Buffer::Buffer(const Buffer &other)
{
	curx_             = other.curx_;
	cury_             = other.cury_;
	nrows_            = other.nrows_;
	rowoffs_          = other.rowoffs_;
	coloffs_          = other.coloffs_;
	rows_             = other.rows_;
	content_          = other.content_;
	rows_cache_dirty_ = other.rows_cache_dirty_;
	filename_         = other.filename_;
	is_file_backed_   = other.is_file_backed_;
	is_virtual_       = other.is_virtual_;
	dirty_            = other.dirty_;
	read_only_        = other.read_only_;
	mark_set_         = other.mark_set_;
	mark_curx_        = other.mark_curx_;
	mark_cury_        = other.mark_cury_;
	// Copy edit mode + syntax/highlighting flags
	edit_mode_          = other.edit_mode_;
	edit_mode_detected_ = other.edit_mode_detected_;
	version_            = other.version_;
	syntax_enabled_        = other.syntax_enabled_;
	syntax_user_override_  = other.syntax_user_override_;
	filetype_           = other.filetype_;
	// Fresh undo system for the copy
	undo_tree_ = std::make_unique<UndoTree>();
	undo_sys_  = std::make_unique<UndoSystem>(*this, *undo_tree_);

	// Recreate a highlighter engine for this copy based on filetype/syntax state
	if (syntax_enabled_) {
		// Allocate engine and install an appropriate highlighter
		highlighter_ = std::make_unique<kte::HighlighterEngine>();
		if (!filetype_.empty()) {
			auto hl = kte::HighlighterRegistry::CreateFor(filetype_);
			if (hl) {
				highlighter_->SetHighlighter(std::move(hl));
			} else {
				// Unsupported filetype -> NullHighlighter keeps syntax pipeline active
				highlighter_->SetHighlighter(std::make_unique<kte::NullHighlighter>());
			}
		} else {
			// No filetype -> keep syntax enabled but use NullHighlighter
			highlighter_->SetHighlighter(std::make_unique<kte::NullHighlighter>());
		}
		// Fresh engine has empty caches; nothing to invalidate
	}
}


Buffer &
Buffer::operator=(const Buffer &other)
{
	if (this == &other)
		return *this;
	curx_             = other.curx_;
	cury_             = other.cury_;
	nrows_            = other.nrows_;
	rowoffs_          = other.rowoffs_;
	coloffs_          = other.coloffs_;
	rows_             = other.rows_;
	content_          = other.content_;
	rows_cache_dirty_ = other.rows_cache_dirty_;
	filename_         = other.filename_;
	is_file_backed_   = other.is_file_backed_;
	is_virtual_       = other.is_virtual_;
	id_               = NextBufferId(); // a copy is a different document
	dirty_            = other.dirty_;
	read_only_        = other.read_only_;
	mark_set_         = other.mark_set_;
	mark_curx_          = other.mark_curx_;
	mark_cury_          = other.mark_cury_;
	edit_mode_          = other.edit_mode_;
	edit_mode_detected_ = other.edit_mode_detected_;
	version_            = other.version_;
	syntax_enabled_        = other.syntax_enabled_;
	syntax_user_override_  = other.syntax_user_override_;
	filetype_           = other.filetype_;
	// Recreate undo system for this instance
	undo_tree_ = std::make_unique<UndoTree>();
	undo_sys_  = std::make_unique<UndoSystem>(*this, *undo_tree_);

	// Recreate highlighter engine consistent with syntax settings
	highlighter_.reset();
	if (syntax_enabled_) {
		highlighter_ = std::make_unique<kte::HighlighterEngine>();
		if (!filetype_.empty()) {
			auto hl = kte::HighlighterRegistry::CreateFor(filetype_);
			if (hl) {
				highlighter_->SetHighlighter(std::move(hl));
			} else {
				highlighter_->SetHighlighter(std::make_unique<kte::NullHighlighter>());
			}
		} else {
			highlighter_->SetHighlighter(std::make_unique<kte::NullHighlighter>());
		}
	}
	return *this;
}


// Move constructor: move all fields and update UndoSystem's buffer reference
Buffer::Buffer(Buffer &&other) noexcept
	: curx_(other.curx_),
	  cury_(other.cury_),
	  nrows_(other.nrows_),
	  rowoffs_(other.rowoffs_),
	  coloffs_(other.coloffs_),
	  rows_(std::move(other.rows_)),
	  filename_(std::move(other.filename_)),
	  is_file_backed_(other.is_file_backed_),
	  is_virtual_(other.is_virtual_),
	  id_(other.id_),
	  dirty_(other.dirty_),
	  read_only_(other.read_only_),
	  mark_set_(other.mark_set_),
	  mark_curx_(other.mark_curx_),
	  mark_cury_(other.mark_cury_),
	  visual_line_active_(other.visual_line_active_),
	  visual_line_anchor_y_(other.visual_line_anchor_y_),
	  visual_line_active_y_(other.visual_line_active_y_),
	  undo_tree_(std::move(other.undo_tree_)),
	  undo_sys_(std::move(other.undo_sys_))
{
	// Move edit mode + syntax/highlighting state
	edit_mode_          = other.edit_mode_;
	edit_mode_detected_ = other.edit_mode_detected_;
	version_            = other.version_;
	syntax_enabled_        = other.syntax_enabled_;
	syntax_user_override_  = other.syntax_user_override_;
	filetype_           = std::move(other.filetype_);
	highlighter_        = std::move(other.highlighter_);
	content_            = std::move(other.content_);
	rows_cache_dirty_   = other.rows_cache_dirty_;
	on_disk_identity_   = other.on_disk_identity_;
	// Non-owning: the recorder object itself is owned by SwapManager and outlives
	// this move. The caller (Editor) is responsible for calling SwapManager::Rehome()
	// so the journal's Buffer* key follows this object to its new address.
	swap_rec_       = other.swap_rec_;
	other.swap_rec_ = nullptr;
	// Update UndoSystem's buffer reference to point to this object
	if (undo_sys_) {
		undo_sys_->UpdateBufferReference(*this);
	}
}


// Move assignment: move all fields and update UndoSystem's buffer reference
Buffer &
Buffer::operator=(Buffer &&other) noexcept
{
	if (this == &other)
		return *this;

	curx_                 = other.curx_;
	cury_                 = other.cury_;
	nrows_                = other.nrows_;
	rowoffs_              = other.rowoffs_;
	coloffs_              = other.coloffs_;
	rows_                 = std::move(other.rows_);
	filename_             = std::move(other.filename_);
	is_file_backed_       = other.is_file_backed_;
	is_virtual_           = other.is_virtual_;
	id_                   = other.id_;
	dirty_                = other.dirty_;
	read_only_            = other.read_only_;
	mark_set_             = other.mark_set_;
	mark_curx_            = other.mark_curx_;
	mark_cury_            = other.mark_cury_;
	visual_line_active_   = other.visual_line_active_;
	visual_line_anchor_y_ = other.visual_line_anchor_y_;
	visual_line_active_y_ = other.visual_line_active_y_;
	undo_tree_            = std::move(other.undo_tree_);
	undo_sys_             = std::move(other.undo_sys_);

	// Move edit mode + syntax/highlighting state
	edit_mode_          = other.edit_mode_;
	edit_mode_detected_ = other.edit_mode_detected_;
	version_            = other.version_;
	syntax_enabled_        = other.syntax_enabled_;
	syntax_user_override_  = other.syntax_user_override_;
	filetype_           = std::move(other.filetype_);
	highlighter_        = std::move(other.highlighter_);
	content_            = std::move(other.content_);
	rows_cache_dirty_   = other.rows_cache_dirty_;
	on_disk_identity_   = other.on_disk_identity_;
	// Non-owning: the recorder object itself is owned by SwapManager and outlives
	// this move. The caller (Editor) is responsible for calling SwapManager::Rehome()
	// so the journal's Buffer* key follows this object to its new address.
	swap_rec_       = other.swap_rec_;
	other.swap_rec_ = nullptr;
	// Update UndoSystem's buffer reference to point to this object
	if (undo_sys_) {
		undo_sys_->UpdateBufferReference(*this);
	}

	return *this;
}


bool
Buffer::OpenFromFile(const std::string &path, std::string &err)
{
	auto normalize_path = [](const std::string &in) -> std::string {
		std::string expanded = in;
		// Expand leading '~' to HOME
		if (!expanded.empty() && expanded[0] == '~') {
			const char *home = std::getenv("HOME");
			if (home && expanded.size() >= 2 && (expanded[1] == '/' || expanded[1] == '\\')) {
				expanded = std::string(home) + expanded.substr(1);
			} else if (home && expanded.size() == 1) {
				expanded = std::string(home);
			}
		}
		try {
			std::filesystem::path p(expanded);
			if (std::filesystem::exists(p)) {
				return std::filesystem::canonical(p).string();
			}
			return std::filesystem::absolute(p).string();
		} catch (...) {
			// On any error, fall back to input
			return expanded;
		}
	};

	const std::string norm = normalize_path(path);
	// If the file doesn't exist, initialize an empty, non-file-backed buffer
	// with the provided filename. Do not touch the filesystem until Save/SaveAs.
	std::error_code exists_ec;
	const bool exists = std::filesystem::exists(norm, exists_ec);
	if (exists_ec) {
		// e.g. name too long, or a parent directory we may not search
		err = "Cannot open " + norm + ": " + exists_ec.message();
		kte::ErrorHandler::Instance().Error("Buffer", err, norm);
		return false;
	}
	if (!exists) {
		rows_.clear();
		nrows_          = 0;
		filename_       = norm;
		is_file_backed_ = false;
		is_virtual_     = false;
		dirty_          = false;

		// Reset cursor/viewport state
		curx_      = cury_    = 0;
		rowoffs_   = coloffs_ = 0;
		mark_set_  = false;
		mark_curx_ = mark_cury_ = 0;

		// Empty PieceTable
		content_.Clear();
		rows_cache_dirty_ = true;
		// History recorded against the previous content no longer applies.
		if (undo_sys_)
			undo_sys_->clear();
		MarkContentChanged();

		return true;
	}

	// Open non-blocking so a named pipe cannot hang the editor, then accept
	// regular files only: a directory reads as garbage (or throws), a FIFO
	// blocks, and saving over a device node would replace it with a file.
	int oflags = O_RDONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
	oflags |= O_CLOEXEC;
#endif
	const int fd = kte::syscall::Open(norm.c_str(), oflags);
	if (fd < 0) {
		err = "Failed to open file: " + norm + ": " + std::strerror(errno);
		kte::ErrorHandler::Instance().Error("Buffer", err, norm);
		return false;
	}
	struct stat st{};
	if (kte::syscall::Fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		err = S_ISDIR(st.st_mode) ? ("Is a directory: " + norm) : ("Not a regular file: " + norm);
		(void) kte::syscall::Close(fd);
		kte::ErrorHandler::Instance().Error("Buffer", err, norm);
		return false;
	}

	// Read to EOF in chunks (the file may change size while we read).
	std::string data;
	try {
		data.reserve(static_cast<std::size_t>(st.st_size));
		char chunk[1 << 16];
		for (;;) {
			const ssize_t n = ::read(fd, chunk, sizeof(chunk));
			if (n > 0) {
				data.append(chunk, static_cast<std::size_t>(n));
			} else if (n == 0) {
				break;
			} else if (errno != EINTR) {
				err = "Failed to read file: " + norm + ": " + std::strerror(errno);
				(void) kte::syscall::Close(fd);
				kte::ErrorHandler::Instance().Error("Buffer", err, norm);
				return false;
			}
		}
	} catch (const std::bad_alloc &) {
		(void) kte::syscall::Close(fd);
		err = "File too large to load: " + norm;
		kte::ErrorHandler::Instance().Error("Buffer", err, norm);
		return false;
	}
	(void) kte::syscall::Close(fd);
	// Build the new content aside and swap it in only once complete: if the
	// copy runs out of memory, the buffer keeps its old text (clearing first
	// left an empty buffer that a later save wrote over the file).
	PieceTable fresh;
	try {
		if (!data.empty())
			fresh.Append(data.data(), data.size());
		std::string().swap(data);
	} catch (const std::bad_alloc &) {
		err = "File too large to load: " + norm;
		kte::ErrorHandler::Instance().Error("Buffer", err, norm);
		return false;
	}
	content_ = std::move(fresh);
	rows_cache_dirty_ = true;
	nrows_            = 0; // not used under PieceTable
	filename_         = norm;
	is_file_backed_   = true;
	is_virtual_       = false;
	dirty_            = false;
	RefreshOnDiskIdentity();

	// Reset/initialize undo system for this loaded file
	if (!undo_tree_)
		undo_tree_ = std::make_unique<UndoTree>();
	if (!undo_sys_)
		undo_sys_ = std::make_unique<UndoSystem>(*this, *undo_tree_);
	// Clear any existing history for a fresh load
	undo_sys_->clear();
	MarkContentChanged();

	// Reset cursor/viewport state
	curx_      = cury_    = 0;
	rowoffs_   = coloffs_ = 0;
	mark_set_  = false;
	mark_curx_ = mark_cury_ = 0;

	return true;
}


bool
Buffer::Save(std::string &err) const
{
	if (!is_file_backed_ || filename_.empty()) {
		err = "Buffer is not file-backed; use SaveAs()";
		return false;
	}
	const std::size_t sz = content_.Size();
	const char *data     = sz ? content_.Data() : nullptr;
	if (sz && !data) {
		err = "Internal error: buffer materialization failed";
		return false;
	}
	if (!atomic_write_file(filename_, data ? data : "", sz, err)) {
		kte::ErrorHandler::Instance().Error("Buffer", err, filename_);
		return false;
	}
	// Update observed on-disk identity after a successful save.
	const_cast<Buffer *>(this)->RefreshOnDiskIdentity();
	// Note: const method cannot change dirty_. Intentionally const to allow UI code
	// to decide when to flip dirty flag after successful save.
	return true;
}


bool
Buffer::SaveAs(const std::string &path, std::string &err)
{
	// Normalize output path first
	std::string out_path;
	try {
		std::filesystem::path p(path);
		// Do a light expansion of '~'
		std::string expanded = path;
		if (!expanded.empty() && expanded[0] == '~') {
			const char *home = std::getenv("HOME");
			if (home && expanded.size() >= 2 && (expanded[1] == '/' || expanded[1] == '\\'))
				expanded = std::string(home) + expanded.substr(1);
			else if (home && expanded.size() == 1)
				expanded = std::string(home);
		}
		std::filesystem::path ep(expanded);
		out_path = std::filesystem::absolute(ep).string();
	} catch (...) {
		out_path = path;
	}

	const std::size_t sz = content_.Size();
	const char *data     = sz ? content_.Data() : nullptr;
	if (sz && !data) {
		err = "Internal error: buffer materialization failed";
		return false;
	}
	if (!atomic_write_file(out_path, data ? data : "", sz, err)) {
		kte::ErrorHandler::Instance().Error("Buffer", err, out_path);
		return false;
	}

	filename_       = out_path;
	is_file_backed_ = true;
	is_virtual_     = false;
	dirty_          = false;
	RefreshOnDiskIdentity();
	return true;
}


std::string
Buffer::AsString() const
{
	std::stringstream ss;
	ss << "Buffer<" << this->filename_;
	if (this->Dirty()) {
		ss << "*";
	}
	ss << ">: " << content_.LineCount() << " lines";
	return ss.str();
}


// --- Raw editing APIs (no undo recording, cursor untouched) ---
void
Buffer::insert_text(int row, int col, std::string_view text)
{
	if (row < 0)
		row = 0;
	if (col < 0)
		col = 0;
	const std::size_t off = content_.LineColToByteOffset(static_cast<std::size_t>(row),
	                                                     static_cast<std::size_t>(col));
	if (!text.empty()) {
		content_.Insert(off, text.data(), text.size());
		rows_cache_dirty_ = true;
		edited_at_(row);
		if (swap_rec_)
			swap_rec_->OnInsert(row, col, text);
	}
}


// ===== Adapter helpers for PieceTable-backed Buffer =====
std::string_view
Buffer::GetLineView(std::size_t row) const
{
	// Get byte range for the logical line and return a view into materialized data
	auto range       = content_.GetLineRange(row); // [start,end) in bytes
	const char *base = content_.Data(); // materializes if needed
	if (!base)
		return std::string_view();
	const std::size_t start = range.first;
	const std::size_t len   = (range.second > range.first) ? (range.second - range.first) : 0;
	return std::string_view(base + start, len);
}


void
Buffer::ensure_rows_cache() const
{
	std::lock_guard<std::mutex> lock(buffer_mutex_);
	if (!rows_cache_dirty_)
		return;
	rows_.clear();
	const std::size_t lc = content_.LineCount();
	rows_.reserve(lc);
	for (std::size_t i = 0; i < lc; ++i) {
		rows_.emplace_back(content_.GetLine(i));
	}
	// Keep nrows_ in sync for any legacy code that still reads it
	const_cast<Buffer *>(this)->nrows_ = rows_.size();
	rows_cache_dirty_                  = false;
}


std::size_t
Buffer::content_LineCount_() const
{
	return content_.LineCount();
}


#if defined(KTE_TESTS)
std::string
Buffer::BytesForTests() const
{
	const std::size_t sz = content_.Size();
	if (sz == 0)
		return std::string();
	const char *data = content_.Data();
	if (!data)
		return std::string();
	return std::string(data, data + sz);
}
#endif


void
Buffer::delete_text(int row, int col, std::size_t len)
{
	if (len == 0)
		return;
	if (row < 0)
		row = 0;
	if (col < 0)
		col = 0;

	// `len` counts each newline as one character, so the deletion is simply
	// the next `len` bytes from the (clamped) start, capped at end of buffer.
	const std::size_t start = content_.LineColToByteOffset(static_cast<std::size_t>(row),
	                                                       static_cast<std::size_t>(col));
	const std::size_t total  = content_.Size();
	const std::size_t actual = (total > start) ? std::min(len, total - start) : 0;
	if (actual == 0)
		return;
	content_.Delete(start, actual);
	rows_cache_dirty_ = true;
	edited_at_(row);
	if (swap_rec_)
		swap_rec_->OnDelete(row, col, actual);
}


void
Buffer::split_line(int row, const int col)
{
	int c = col;
	if (row < 0)
		row = 0;
	if (c < 0)
		c = 0;
	const std::size_t off = content_.LineColToByteOffset(static_cast<std::size_t>(row),
	                                                     static_cast<std::size_t>(c));
	const char nl = '\n';
	content_.Insert(off, &nl, 1);
	rows_cache_dirty_ = true;
	edited_at_(row);
	if (swap_rec_)
		swap_rec_->OnInsert(row, c, std::string_view("\n", 1));
}


void
Buffer::join_lines(int row)
{
	if (row < 0)
		row = 0;
	std::size_t r = static_cast<std::size_t>(row);
	if (r + 1 >= content_.LineCount())
		return;
	const int col = static_cast<int>(content_.GetLine(r).size());
	// Delete the newline between line r and r+1
	std::size_t end_of_line = content_.LineColToByteOffset(r, std::numeric_limits<std::size_t>::max());
	// end_of_line now equals line end (clamped before newline). The newline should be exactly at this position.
	content_.Delete(end_of_line, 1);
	rows_cache_dirty_ = true;
	edited_at_(row);
	if (swap_rec_)
		swap_rec_->OnDelete(row, col, 1);
}


void
Buffer::insert_row(int row, const std::string_view text)
{
	if (row < 0)
		row = 0;
	std::size_t off = content_.LineColToByteOffset(static_cast<std::size_t>(row), 0);
	if (!text.empty())
		content_.Insert(off, text.data(), text.size());
	const char nl = '\n';
	content_.Insert(off + text.size(), &nl, 1);
	rows_cache_dirty_ = true;
	edited_at_(row);
	if (swap_rec_) {
		// One record: the first of two could trigger a journal checkpoint,
		// whose snapshot already holds the newline the second then added
		// again on replay.
		std::string line;
		line.reserve(text.size() + 1);
		line.append(text);
		line.push_back('\n');
		swap_rec_->OnInsert(row, 0, line);
	}
}


void
Buffer::delete_row(int row)
{
	if (row < 0)
		row = 0;
	std::size_t r = static_cast<std::size_t>(row);
	if (r >= content_.LineCount())
		return;
	auto range = content_.GetLineRange(r); // [start,end)
	// If not last line, ensure we include the separating newline by using end as-is (which points to next line start)
	// If last line, end may equal total_size_. We still delete [start,end) which removes the last line content.
	const std::size_t start  = range.first;
	const std::size_t end    = range.second;
	const std::size_t actual = (end > start) ? (end - start) : 0;
	if (actual == 0)
		return;
	content_.Delete(start, actual);
	rows_cache_dirty_ = true;
	edited_at_(row);
	if (swap_rec_)
		swap_rec_->OnDelete(row, 0, actual);
}


void
Buffer::replace_all_bytes(const std::string_view bytes)
{
	content_.Clear();
	if (!bytes.empty())
		content_.Append(bytes.data(), bytes.size());
	rows_cache_dirty_ = true;
	if (undo_sys_)
		undo_sys_->clear();
	MarkContentChanged();
}


void
Buffer::insert_spans(int row, int col, const std::vector<TextSpan> &spans)
{
	if (row < 0)
		row = 0;
	if (col < 0)
		col = 0;
	const std::size_t off = content_.LineColToByteOffset(static_cast<std::size_t>(row),
	                                                     static_cast<std::size_t>(col));
	content_.InsertSpans(off, spans);
	rows_cache_dirty_ = true;
	edited_at_(row);
	if (swap_rec_) {
		// The journal needs the bytes as one record (see insert_row).
		std::string text;
		content_.VisitSpans(spans, [&](const char *d, std::size_t n) {
			text.append(d, n);
		});
		if (!text.empty())
			swap_rec_->OnInsert(row, col, text);
	}
}


std::vector<TextSpan>
Buffer::SpansAt(int row, int col, std::size_t len) const
{
	const std::size_t off = content_.LineColToByteOffset(static_cast<std::size_t>(std::max(row, 0)),
	                                                     static_cast<std::size_t>(std::max(col, 0)));
	return content_.SpansInRange(off, len);
}


std::vector<TextSpan>
Buffer::TakeDeletedSpans(int row, int col, std::size_t len)
{
	const std::size_t off = content_.LineColToByteOffset(static_cast<std::size_t>(std::max(row, 0)),
	                                                     static_cast<std::size_t>(std::max(col, 0)));
	return content_.TakeDeletedSpans(off, len);
}


// Undo system accessors
UndoSystem *
Buffer::Undo()
{
	return undo_sys_.get();
}


const UndoSystem *
Buffer::Undo() const
{
	return undo_sys_.get();
}