// SyscallWrappers.h - EINTR-safe syscall wrappers for kte
#pragma once

#include <string>
#include <cstddef>
#include <sys/stat.h>

namespace kte {
namespace syscall {
// EINTR-safe wrapper for open(2).
// Returns file descriptor on success, -1 on failure (errno set).
// Automatically retries on EINTR.
int Open(const char *path, int flags, mode_t mode = 0);

// Wrapper for close(2).
// Returns 0 on success, -1 on failure (errno set).
// EINTR is treated as success and never retried: the descriptor has already
// been released on Linux, and retrying could close an unrelated, reused fd.
int Close(int fd);

// EINTR-safe wrapper for fsync(2).
// Returns 0 on success, -1 on failure (errno set).
// Automatically retries on EINTR.
int Fsync(int fd);

// EINTR-safe wrapper for fstat(2).
// Returns 0 on success, -1 on failure (errno set).
// Automatically retries on EINTR.
int Fstat(int fd, struct stat *buf);

// EINTR-safe wrapper for fchmod(2).
// Returns 0 on success, -1 on failure (errno set).
// Automatically retries on EINTR.
int Fchmod(int fd, mode_t mode);

// EINTR-safe wrapper for ftruncate(2).
// Returns 0 on success, -1 on failure (errno set).
// Automatically retries on EINTR.
int Ftruncate(int fd, off_t length);

// EINTR-safe wrapper for mkstemp(3).
// Returns file descriptor on success, -1 on failure (errno set).
// Automatically retries on EINTR.
// Note: template_str must be a mutable buffer ending in "XXXXXX".
int Mkstemp(char *template_str);

// Note: rename(2) and unlink(2) are not wrapped because they operate on
// filesystem metadata and typically complete atomically without EINTR.
// If interrupted, they either succeed or fail without partial state.
} // namespace syscall
} // namespace kte